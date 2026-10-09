#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/CanConnect.h"

using namespace bazalt::engine;

namespace
{
    PortDescriptor audioPort (Channels channels = Channels::Mono)
    {
        PortDescriptor p { .id = "p", .type = SignalType::Signal, .quantity = Quantity::Audio };
        p.channels = channels;
        return p;
    }

    PortDescriptor controlPort (Quantity quantity = Quantity::Dimensionless, std::optional<float> minValue = {}, std::optional<float> maxValue = {})
    {
        PortDescriptor p { .id = "p", .type = SignalType::Signal };
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

    PortDescriptor boolean { .id = "b", .type = SignalType::Signal, .quantity = Quantity::Boolean };
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
    CHECK (result.adapterChain[0].typeId == "math.map");
    CHECK (result.adapterChain[0].seedFromDestinationRange);
    CHECK (result.adapterChain[0].seedFromSourceRange); // adapt.map seeds its input range too (design/Map.png)

    // Bipolar behaves the same way.
    CHECK (canConnect (controlPort (Quantity::Bipolar), to).outcome == ConnectionOutcome::NeedsAdapters);
}

TEST_CASE ("canConnect: a real quantity into Unipolar/Bipolar needs a Map, seeded from both sides",
           "[engine][CanConnect]")
{
    // Normalise is gone (wiki/plans/DataAndWavetable.md §2): a Map from the
    // source's range onto 0..1 / -1..1 is the same thing.
    const auto from = controlPort (Quantity::Time, 0.0f, 10.0f);
    const auto result = canConnect (from, controlPort (Quantity::Unipolar));

    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 1);
    CHECK (result.adapterChain[0].typeId == "math.map");
    CHECK (result.adapterChain[0].seedFromSourceRange);
    CHECK (result.adapterChain[0].seedFromDestinationRange);
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

    CHECK (result.adapterChain[0].typeId == "math.map");
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
    CHECK (toFrequency.adapterChain[0].typeId == "math.pitchToFrequency");
    CHECK (toFrequency.adapterChain[0].inputPortId == "pitch");
    CHECK (toFrequency.adapterChain[0].outputPortId == "frequency");
    // No seeding at all - the conversion is a fixed formula, not range-dependent.
    CHECK_FALSE (toFrequency.adapterChain[0].seedFromSourceRange);
    CHECK_FALSE (toFrequency.adapterChain[0].seedFromDestinationRange);

    const auto toPitch = canConnect (frequency, pitch);
    REQUIRE (toPitch.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (toPitch.adapterChain.size() == 1);
    CHECK (toPitch.adapterChain[0].typeId == "math.frequencyToPitch");
}

TEST_CASE ("canConnect: a Boolean is a plain 0/1 value - it wires straight into any value port",
           "[engine][CanConnect]")
{
    PortDescriptor boolPort { .id = "b", .type = SignalType::Signal, .quantity = Quantity::Boolean };
    CHECK (canConnect (boolPort, controlPort()).outcome == ConnectionOutcome::Ok);
    CHECK (canConnect (boolPort, controlPort (Quantity::Frequency, 20.0f, 20000.0f)).outcome == ConnectionOutcome::Ok);
    CHECK (canConnect (boolPort, audioPort()).outcome == ConnectionOutcome::Ok);
    // ...and any value reads as a boolean (non-zero is true).
    CHECK (canConnect (controlPort(), boolPort).outcome == ConnectionOutcome::Ok);
}

TEST_CASE ("canConnect: Control into Event needs Threshold, wired into 'by' not 'in'", "[engine][CanConnect]")
{
    const auto result = canConnect (controlPort(), PortDescriptor { "e", SignalType::Event });
    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 1);
    CHECK (result.adapterChain[0].typeId == "logic.threshold");
    CHECK (result.adapterChain[0].inputPortId == "by");
}

TEST_CASE ("canConnect: heterogeneous pairs with no adapter yet are Reject, never a chain that can't be fulfilled",
           "[engine][CanConnect]")
{
    // Note -> Control has no adapter of any kind yet.
    const auto result = canConnect (PortDescriptor { "n", SignalType::Note }, controlPort());
    CHECK (result.outcome == ConnectionOutcome::Reject);
}

TEST_CASE ("canConnect: Audio and modulation values meet directly - both are normalised ranges",
           "[engine][CanConnect]")
{
    // wiki/plans/DataAndWavetable.md D1: one numeric signal; To Mod / To Audio are gone.
    CHECK (canConnect (audioPort(), controlPort (Quantity::Bipolar)).outcome == ConnectionOutcome::Ok);
    CHECK (canConnect (audioPort(), controlPort (Quantity::Unipolar)).outcome == ConnectionOutcome::Ok);
    CHECK (canConnect (audioPort(), controlPort()).outcome == ConnectionOutcome::Ok);
    CHECK (canConnect (controlPort (Quantity::Bipolar), audioPort()).outcome == ConnectionOutcome::Ok);
    CHECK (canConnect (controlPort (Quantity::Unipolar), audioPort()).outcome == ConnectionOutcome::Ok);
    CHECK (canConnect (controlPort(), audioPort()).outcome == ConnectionOutcome::Ok);
}

TEST_CASE ("canConnect: Audio into a real-quantity port needs a Map, one step",
           "[engine][CanConnect]")
{
    const auto result = canConnect (audioPort(), controlPort (Quantity::Frequency, 20.0f, 20000.0f));

    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 1);
    CHECK (result.adapterChain[0].typeId == "math.map");
    CHECK (result.adapterChain[0].seedFromSourceRange); // the audio side reads as ±1
    CHECK (result.adapterChain[0].seedFromDestinationRange);
}

TEST_CASE ("canConnect: stereo into a mono value port asks for a Downmix, then Maps when needed",
           "[engine][CanConnect][channels]")
{
    const auto modulation = canConnect (audioPort (Channels::Stereo), controlPort());
    REQUIRE (modulation.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (modulation.adapterChain.size() == 1);
    CHECK (modulation.adapterChain[0].typeId == "channels.downmix");
    CHECK_FALSE (modulation.choices.empty());

    const auto realQuantity = canConnect (audioPort (Channels::Stereo), controlPort (Quantity::Frequency, 20.0f, 20000.0f));
    REQUIRE (realQuantity.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (realQuantity.adapterChain.size() == 2);
    CHECK (realQuantity.adapterChain[0].typeId == "channels.downmix");
    CHECK (realQuantity.adapterChain[1].typeId == "math.map");
    CHECK_FALSE (realQuantity.choices.empty());
}


TEST_CASE ("canConnect: a real-quantity value into Audio needs a Map onto ±1",
           "[engine][CanConnect]")
{
    const auto result = canConnect (controlPort (Quantity::Frequency, 20.0f, 20000.0f), audioPort());

    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 1);
    CHECK (result.adapterChain[0].typeId == "math.map");
    CHECK (result.adapterChain[0].seedFromSourceRange);
    CHECK (result.adapterChain[0].seedFromDestinationRange);
}

TEST_CASE ("canConnect: a mono value into a stereo Audio port broadcasts for free",
           "[engine][CanConnect][channels]")
{
    CHECK (canConnect (controlPort (Quantity::Bipolar), audioPort (Channels::Stereo)).outcome == ConnectionOutcome::Ok);
}

TEST_CASE ("canConnect: Audio channels, mono->mono, mono->stereo (free), stereo->stereo are Ok",
           "[engine][CanConnect][channels]")
{
    CHECK (canConnect (audioPort (Channels::Mono), audioPort (Channels::Mono)).outcome == ConnectionOutcome::Ok);
    CHECK (canConnect (audioPort (Channels::Mono), audioPort (Channels::Stereo)).outcome == ConnectionOutcome::Ok);
    CHECK (canConnect (audioPort (Channels::Stereo), audioPort (Channels::Stereo)).outcome == ConnectionOutcome::Ok);
}

TEST_CASE ("canConnect: stereo->mono needs mix.downmix, and the user's choice of how", "[engine][CanConnect][channels]")
{
    const auto result = canConnect (audioPort (Channels::Stereo), audioPort (Channels::Mono));
    REQUIRE (result.outcome == ConnectionOutcome::NeedsAdapters);
    REQUIRE (result.adapterChain.size() == 1);
    CHECK (result.adapterChain[0].typeId == "channels.downmix");
    CHECK (result.choices == std::vector<juce::String> { "mid", "left", "right", "side" });
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
