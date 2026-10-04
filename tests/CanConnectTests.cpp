#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/CanConnect.h"

using namespace bazalt::engine;

namespace
{
    PortDescriptor audioPort (Channels channels = Channels::Mono)
    {
        PortDescriptor p { "p", SignalType::Audio };
        p.channels = channels;
        return p;
    }

    PortDescriptor controlPort (Quantity quantity = Quantity::Dimensionless, std::optional<float> minValue = {}, std::optional<float> maxValue = {})
    {
        PortDescriptor p { "p", SignalType::Control };
        p.quantity = quantity;
        p.minValue = minValue;
        p.maxValue = maxValue;
        return p;
    }

    PortDescriptor dataPort (std::vector<DataTag> tags)
    {
        PortDescriptor p { "p", SignalType::Data };
        p.dataTags = std::move (tags);
        return p;
    }
}

TEST_CASE ("canConnect: same signal type, same or Dimensionless quantity is always Ok", "[engine][CanConnect]")
{
    CHECK (canConnect (audioPort(), audioPort()).outcome == ConnectionOutcome::Ok);
    CHECK (canConnect (controlPort (Quantity::Frequency), controlPort (Quantity::Frequency)).outcome == ConnectionOutcome::Ok);
    CHECK (canConnect (controlPort (Quantity::Dimensionless), controlPort (Quantity::Frequency)).outcome == ConnectionOutcome::Ok);
    CHECK (canConnect (controlPort (Quantity::Frequency), controlPort (Quantity::Dimensionless)).outcome == ConnectionOutcome::Ok);

    PortDescriptor event { "e", SignalType::Event };
    CHECK (canConnect (event, event).outcome == ConnectionOutcome::Ok);

    PortDescriptor note { "n", SignalType::Note };
    CHECK (canConnect (note, note).outcome == ConnectionOutcome::Ok);

    PortDescriptor boolean { "b", SignalType::Boolean };
    CHECK (canConnect (boolean, boolean).outcome == ConnectionOutcome::Ok);
}

TEST_CASE ("canConnect: Spectral is always rejected, reserved and unimplemented", "[engine][CanConnect]")
{
    PortDescriptor spectral { "s", SignalType::Spectral };
    const auto result = canConnect (spectral, spectral);
    CHECK (result.outcome == ConnectionOutcome::Reject);
}

TEST_CASE ("canConnect: Unipolar/Bipolar into a real quantity needs Map, seeded from the destination",
           "[engine][CanConnect]")
{
    const auto from = controlPort (Quantity::Unipolar);
    const auto to = controlPort (Quantity::Frequency, 20.0f, 20000.0f);
    const auto result = canConnect (from, to);

    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 1);
    CHECK (result.adapterChain[0].typeId == "adapt.map");
    CHECK (result.adapterChain[0].seedFromDestinationRange);
    CHECK (result.adapterChain[0].seedFromSourceRange); // adapt.map seeds its input range too (design/Map.png)

    // Bipolar behaves the same way.
    CHECK (canConnect (controlPort (Quantity::Bipolar), to).outcome == ConnectionOutcome::NeedsAdapters);
}

TEST_CASE ("canConnect: a real quantity into Unipolar/Bipolar needs Normalise, seeded from the source",
           "[engine][CanConnect]")
{
    const auto from = controlPort (Quantity::Time, 0.0f, 10.0f);
    const auto to = controlPort (Quantity::Unipolar);
    const auto result = canConnect (from, to);

    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 1);
    CHECK (result.adapterChain[0].typeId == "adapt.normalise");
    CHECK (result.adapterChain[0].seedFromSourceRange);
    CHECK_FALSE (result.adapterChain[0].seedFromDestinationRange);
}

TEST_CASE ("canConnect: two different real quantities insert adapt.map, seeded from both sides at once",
           "[engine][CanConnect][M20]")
{
    // Time<->Gain, not Pitch<->Frequency (this test's own original M20
    // example) - direct feedback caught that Pitch<->Frequency specifically
    // needs an exact exponential conversion, not a linear remap, so that
    // pair now has its own dedicated branch (see the AudioControlBridge
    // test group below) and no longer reaches this generic fallback.
    const auto from = controlPort (Quantity::Time, 0.0f, 10.0f);
    const auto to = controlPort (Quantity::Gain, 0.0f, 4.0f);
    const auto result = canConnect (from, to);

    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 1);

    CHECK (result.adapterChain[0].typeId == "adapt.map");
    CHECK (result.adapterChain[0].seedFromSourceRange);
    CHECK (result.adapterChain[0].seedFromDestinationRange);
    CHECK (result.reason.isNotEmpty());
}

TEST_CASE ("canConnect: Pitch<->Frequency gets the exact converter, not the generic linear remap",
           "[engine][CanConnect][AudioControlBridge]")
{
    const auto pitch = controlPort (Quantity::Pitch, 0.0f, 127.0f);
    const auto frequency = controlPort (Quantity::Frequency, 20.0f, 20000.0f);

    const auto toFrequency = canConnect (pitch, frequency);
    REQUIRE (toFrequency.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (toFrequency.adapterChain.size() == 1);
    CHECK (toFrequency.adapterChain[0].typeId == "adapt.pitchToFrequency");
    CHECK (toFrequency.adapterChain[0].inputPortId == "pitch");
    CHECK (toFrequency.adapterChain[0].outputPortId == "frequency");
    // No seeding at all - the conversion is a fixed formula, not range-dependent.
    CHECK_FALSE (toFrequency.adapterChain[0].seedFromSourceRange);
    CHECK_FALSE (toFrequency.adapterChain[0].seedFromDestinationRange);

    const auto toPitch = canConnect (frequency, pitch);
    REQUIRE (toPitch.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (toPitch.adapterChain.size() == 1);
    CHECK (toPitch.adapterChain[0].typeId == "adapt.frequencyToPitch");
}

TEST_CASE ("canConnect: Boolean into Control needs a From Bool adapter",
           "[engine][CanConnect][AudioControlBridge]")
{
    PortDescriptor boolPort { "b", SignalType::Boolean };
    const auto result = canConnect (boolPort, controlPort());

    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 1);
    CHECK (result.adapterChain[0].typeId == "adapt.boolToControl");
}

TEST_CASE ("canConnect: Control into Event needs Threshold, wired into 'by' not 'in'", "[engine][CanConnect]")
{
    const auto result = canConnect (controlPort(), PortDescriptor { "e", SignalType::Event });
    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 1);
    CHECK (result.adapterChain[0].typeId == "adapt.threshold");
    CHECK (result.adapterChain[0].inputPortId == "by");
}

TEST_CASE ("canConnect: heterogeneous pairs with no adapter yet are Reject, never a chain that can't be fulfilled",
           "[engine][CanConnect]")
{
    // Note -> Control has no adapter of any kind yet.
    const auto result = canConnect (PortDescriptor { "n", SignalType::Note }, controlPort());
    CHECK (result.outcome == ConnectionOutcome::Reject);
}

TEST_CASE ("canConnect: mono Audio into a modulation-quantity Control needs Audio to Modulation, one step",
           "[engine][CanConnect][AudioControlBridge]")
{
    const auto result = canConnect (audioPort(), controlPort (Quantity::Bipolar));

    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 1);
    CHECK (result.adapterChain[0].typeId == "adapt.audioToControl");
    CHECK (result.adapterChain[0].inputPortId == "in");

    // Dimensionless behaves the same as an explicit modulation quantity —
    // one step only, same as connectControl()'s own Dimensionless handling.
    CHECK (canConnect (audioPort(), controlPort()).outcome == ConnectionOutcome::NeedsAdapters);
}

TEST_CASE ("canConnect: mono Audio into a real-quantity Control needs Audio to Modulation, then Map",
           "[engine][CanConnect][AudioControlBridge]")
{
    const auto to = controlPort (Quantity::Frequency, 20.0f, 20000.0f);
    const auto result = canConnect (audioPort(), to);

    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 2);
    CHECK (result.adapterChain[0].typeId == "adapt.audioToControl");
    CHECK (result.adapterChain[1].typeId == "adapt.map");
    CHECK (result.adapterChain[1].seedFromDestinationRange);
    CHECK (result.adapterChain[1].seedFromSourceRange); // from adapt.audioToControl's Bipolar output
}

TEST_CASE ("canConnect: stereo Audio into Control is a hard reject, not a 3-step chain",
           "[engine][CanConnect][AudioControlBridge][channels]")
{
    const auto result = canConnect (audioPort (Channels::Stereo), controlPort());
    CHECK (result.outcome == ConnectionOutcome::Reject);

    const auto realQuantity = canConnect (audioPort (Channels::Stereo), controlPort (Quantity::Frequency, 20.0f, 20000.0f));
    CHECK (realQuantity.outcome == ConnectionOutcome::Reject);
}

TEST_CASE ("canConnect: a modulation-quantity Control into Audio needs To Audio, one step",
           "[engine][CanConnect][ControlToAudioBridge]")
{
    const auto result = canConnect (controlPort (Quantity::Bipolar), audioPort());

    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 1);
    CHECK (result.adapterChain[0].typeId == "adapt.controlToAudio");
    CHECK (result.adapterChain[0].inputPortId == "in");

    // Dimensionless behaves the same as an explicit modulation quantity -
    // one step only, same as connectControl()'s own Dimensionless handling.
    CHECK (canConnect (controlPort(), audioPort()).outcome == ConnectionOutcome::NeedsAdapters);
    CHECK (canConnect (controlPort (Quantity::Unipolar), audioPort()).outcome == ConnectionOutcome::NeedsAdapters);
}

TEST_CASE ("canConnect: a real-quantity Control into Audio needs Normalise, then To Audio",
           "[engine][CanConnect][ControlToAudioBridge]")
{
    const auto from = controlPort (Quantity::Frequency, 20.0f, 20000.0f);
    const auto result = canConnect (from, audioPort());

    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 2);
    CHECK (result.adapterChain[0].typeId == "adapt.normalise");
    CHECK (result.adapterChain[0].seedFromSourceRange);
    CHECK_FALSE (result.adapterChain[0].seedFromDestinationRange);
    CHECK (result.adapterChain[1].typeId == "adapt.controlToAudio");
}

TEST_CASE ("canConnect: Control into Audio has no stereo complication - Control has no Channels concept",
           "[engine][CanConnect][ControlToAudioBridge][channels]")
{
    // A Control source into a STEREO-destined Audio port still only ever
    // needs the one-step bridge - the existing mono->stereo free broadcast
    // (connectAudio()) handles the rest once adapt.controlToAudio's own
    // mono "out" reaches the real Audio<->Audio leg, with no second adapter
    // needed on THIS leg of the connection.
    const auto result = canConnect (controlPort (Quantity::Bipolar), audioPort (Channels::Stereo));
    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 1);
    CHECK (result.adapterChain[0].typeId == "adapt.controlToAudio");
}

TEST_CASE ("canConnect: Audio channels, mono->mono, mono->stereo (free), stereo->stereo are Ok",
           "[engine][CanConnect][channels]")
{
    CHECK (canConnect (audioPort (Channels::Mono), audioPort (Channels::Mono)).outcome == ConnectionOutcome::Ok);
    CHECK (canConnect (audioPort (Channels::Mono), audioPort (Channels::Stereo)).outcome == ConnectionOutcome::Ok);
    CHECK (canConnect (audioPort (Channels::Stereo), audioPort (Channels::Stereo)).outcome == ConnectionOutcome::Ok);
}

TEST_CASE ("canConnect: stereo->mono needs mix.downmix", "[engine][CanConnect][channels]")
{
    const auto result = canConnect (audioPort (Channels::Stereo), audioPort (Channels::Mono));
    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 1);
    CHECK (result.adapterChain[0].typeId == "mix.downmix");
}

TEST_CASE ("canConnect: Inherited channels are always compatible", "[engine][CanConnect][channels]")
{
    CHECK (canConnect (audioPort (Channels::Inherited), audioPort (Channels::Mono)).outcome == ConnectionOutcome::Ok);
    CHECK (canConnect (audioPort (Channels::Stereo), audioPort (Channels::Inherited)).outcome == ConnectionOutcome::Ok);
}

TEST_CASE ("canConnect: Data requires an exact tag match, never a wildcard, never an adapter",
           "[engine][CanConnect][Data]")
{
    CHECK (canConnect (dataPort ({ DataTag::ModalSet }), dataPort ({ DataTag::ModalSet })).outcome == ConnectionOutcome::Ok);

    const auto mismatch = canConnect (dataPort ({ DataTag::Scale }), dataPort ({ DataTag::ModalSet }));
    CHECK (mismatch.outcome == ConnectionOutcome::Reject);

    // A consumer that accepts several tags matches on any of them.
    const auto multi = canConnect (dataPort ({ DataTag::Curve }), dataPort ({ DataTag::Scale, DataTag::Curve }));
    CHECK (multi.outcome == ConnectionOutcome::Ok);

    // Never Data <-> non-Data, and never an adapter chain for Data.
    const auto crossType = canConnect (dataPort ({ DataTag::Curve }), controlPort());
    CHECK (crossType.outcome == ConnectionOutcome::Reject);
}
