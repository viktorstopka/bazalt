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
    CHECK_FALSE (result.adapterChain[0].seedFromSourceRange);

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

TEST_CASE ("canConnect: two different real quantities are rejected, not guessed at", "[engine][CanConnect]")
{
    const auto result = canConnect (controlPort (Quantity::Frequency), controlPort (Quantity::Time));
    CHECK (result.outcome == ConnectionOutcome::Reject);
    CHECK (result.reason.isNotEmpty());
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
    // Audio -> Control (Envelope Follower) isn't shipped until M20.
    const auto result = canConnect (audioPort(), controlPort());
    CHECK (result.outcome == ConnectionOutcome::Reject);
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
