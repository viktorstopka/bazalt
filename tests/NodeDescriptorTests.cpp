#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/NodeFactory.h"

using namespace bazalt::engine;

TEST_CASE ("NodeFactory::describeAll() returns a descriptor for every registered type, with real metadata",
           "[engine][NodeFactory][NODE_EDITOR]")
{
    auto factory = buildDefaultNodeFactory();
    const auto descriptors = factory.describeAll();

    REQUIRE (descriptors.size() == 35); // 8 M1/M2 DSP + 7 M7 utility (util.voiceSum removed M17) + 3 M16 adapters + 2 M17 instance types + 1 M18 io.noteIn + 3 M20 (math.round, math.clamp, adapt.remap) + 10 M21 wave 1 (math.subtract/divide/abs/minmax/power/modulo/slew, mix.crossfade, logic.not/toggle) + 1 M21 wave 2 (logic.boolean, the first new growable-port-group node)

    auto findByTypeId = [&] (const juce::String& typeId) -> const NodeDescriptor*
    {
        for (const auto& d : descriptors)
            if (d.typeId == typeId)
                return &d;
        return nullptr;
    };

    const auto* osc = findByTypeId ("osc.analog");
    REQUIRE (osc != nullptr);
    CHECK (osc->title == "Oscillator");
    CHECK (osc->category == "Generators");
    CHECK (osc->layoutVariant == NodeLayoutVariant::Standard);
    REQUIRE (osc->inputs.size() == 2); // "pitch" (M18, ADR-0024), "osc.analog.frequency" (M20)
    CHECK (osc->inputs[0].id == "pitch");
    CHECK (osc->inputs[0].type == SignalType::Control);
    CHECK (osc->inputs[1].id == "osc.analog.frequency");
    REQUIRE (osc->outputs.size() == 1);
    CHECK (osc->outputs[0].id == "out");
    CHECK (osc->outputs[0].type == SignalType::Audio);
    REQUIRE (osc->parameters.size() == 1); // "shape" only — "frequency" is a port now (M20)

    const auto* svf = findByTypeId ("filter.svf");
    REQUIRE (svf != nullptr);
    CHECK (svf->title == "SVF Filter");
    CHECK (svf->category == "Filters");
    REQUIRE (svf->inputs.size() == 3); // "in", "cutoff", "resonance" (M20)
    CHECK (svf->inputs[0].id == "in");

    const auto* amp = findByTypeId ("mix.gain");
    REQUIRE (amp != nullptr);
    CHECK (amp->title == "VCA");
    REQUIRE (amp->inputs.size() == 2);
    CHECK (amp->inputs[0].id == "audio");
    CHECK (amp->inputs[1].id == "gain");

    // Every descriptor must have a non-empty title (falls back to typeId)
    // and a category — this is what makes a usable Add menu possible.
    for (const auto& d : descriptors)
    {
        CHECK (d.title.isNotEmpty());
        CHECK (d.category.isNotEmpty());
    }
}

TEST_CASE ("A node with no title override falls back to its type id in the descriptor",
           "[engine][NodeFactory][NODE_EDITOR]")
{
    class UntitledNode : public Node
    {
    public:
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getOutputPorts() const override { return { { "out", SignalType::Audio } }; }
    };

    NodeFactory factory;
    factory.registerType ("test.untitled", [] { return std::make_unique<UntitledNode>(); });

    const auto descriptors = factory.describeAll();
    REQUIRE (descriptors.size() == 1);
    CHECK (descriptors[0].title == "test.untitled");
    CHECK (descriptors[0].category == "Uncategorized");
}
