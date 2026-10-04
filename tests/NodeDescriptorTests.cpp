#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/NodeFactory.h"

using namespace bazalt::engine;

TEST_CASE ("NodeFactory::describeAll() returns a descriptor for every registered type, with real metadata",
           "[engine][NodeFactory][NODE_EDITOR]")
{
    auto factory = buildDefaultNodeFactory();
    const auto descriptors = factory.describeAll();

    REQUIRE (descriptors.size() == 122); // + deco.header/comment/box/image (wiki/plans/Decorations.md) // + view.tune (design/Visualization/Tune.png) // + 11 Sound Palette nodes (wiki/plans/SoundPalette.md) + space.diffuser, space.reverb (wiki/plans/Reverb.md). 93 as of util.bipolarToUnipolar (see git history for the full running tally before this) + view.ripple (design/Visualization/Ripple.png) + view.count (design/Visualization/Count.png) + view.scope.control (design/Visualization/Scope1.png) + view.scope.modulation (ScopeMod.png) + view.gate (Gate.png) - adapt.remap (became adapt.map, replacing the old Map — design/Map.png) + osc.saw, osc.square, osc.triangle - view.scope - view.glance + view.cycle - logic.boolean + logic.and/or/xor/eventGroup/edge/latch

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
    REQUIRE (osc->inputs.size() == 6); // "pitch" (M18, ADR-0024), "osc.analog.frequency" (M20), fine, pulseWidth, phase, sync (SoundPalette.md Batch 2)
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
    CHECK (amp->title == "Gain"); // wiki/NODES_Gaps.md's jargon-naming finding: was "VCA"
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

TEST_CASE ("io.output's own output port is hidden from the editor - a terminal 'Master Out' node shouldn't offer a further wireable output",
           "[engine][NodeFactory][Stereo]")
{
    // PortDescriptor::hidden exists specifically (and, today, only) for
    // this: the port still fully exists for GraphCompiler's "Final output"
    // resolution (NodeGraph::setOutput() needs a real output port on the
    // designated node), but a user should never be offered it as a drag
    // target - caught live ("the master out still has an output").
    auto factory = bazalt::engine::buildDefaultNodeFactory();
    const auto descriptors = factory.describeAll();

    auto findByTypeId = [&] (const juce::String& typeId) -> const NodeDescriptor*
    {
        for (const auto& d : descriptors)
            if (d.typeId == typeId)
                return &d;
        return nullptr;
    };

    const auto* output = findByTypeId ("io.output");
    REQUIRE (output != nullptr);
    REQUIRE (output->outputs.size() == 1);
    CHECK (output->outputs[0].id == "out");
    CHECK (output->outputs[0].hidden);

    // Its input stays fully visible - only the dangling further-output is
    // the problem, not the node itself.
    REQUIRE (output->inputs.size() == 1);
    CHECK_FALSE (output->inputs[0].hidden);

    // Nothing else in the whole catalog should be hidden - this is a
    // narrow, deliberate exception for one node, not a general-purpose
    // mechanism anything else should reach for without its own reasoning.
    for (const auto& d : descriptors)
    {
        if (d.typeId == "io.output")
            continue;

        for (const auto& in : d.inputs)
            CHECK_FALSE (in.hidden);
        for (const auto& out : d.outputs)
            CHECK_FALSE (out.hidden);
    }
}

TEST_CASE ("09-28-InstanceAllocator.3: instance.allocate.voice (renamed from instance.allocator) no longer "
           "exposes a dead 'configuration' dropdown",
           "[engine][NodeFactory][InstanceAllocator]")
{
    // Three of the four "configuration" options (Voice/Swarm-population/
    // Swarm-transient/Trigger) never did anything - real UI surface for a
    // choice that wasn't one. Removed outright rather than left defaulted,
    // per wiki/reports/InstanceAllocator_2026-09-28.md's own recommendation:
    // Swarm/Trigger become their own real node types later, once actual
    // runtime machinery exists for them, not empty shells on this one.
    auto factory = bazalt::engine::buildDefaultNodeFactory();
    const auto descriptors = factory.describeAll();

    auto findByTypeId = [&] (const juce::String& typeId) -> const NodeDescriptor*
    {
        for (const auto& d : descriptors)
            if (d.typeId == typeId)
                return &d;
        return nullptr;
    };

    // Both old type ids are gone entirely - a graph still referencing either
    // fails to compile with a clear "unknown node type" error, not silently.
    CHECK (findByTypeId ("instance.allocator") == nullptr);
    CHECK (findByTypeId ("instance.voice") == nullptr); // 09-29-AddMenu.1's own rename

    const auto* voice = findByTypeId ("instance.allocate.voice");
    REQUIRE (voice != nullptr);
    CHECK (voice->title == "Voice");
    // "Domain/Allocate", not flat "Domain" - 09-29-AddMenu.1 nests Voice (and
    // its future Swarm/Trigger siblings) under an Add-menu flyout, one level
    // deeper than instance.sum, which deliberately stays flat "Domain".
    CHECK (voice->category == "Domain/Allocate");

    bool sawConfiguration = false;
    bool sawMaxInstances = false;
    bool sawSeed = false;
    for (const auto& p : voice->parameters)
    {
        if (p.id.containsIgnoreCase ("configuration")) sawConfiguration = true;
        if (p.id == "instance.allocate.voice.maxInstances") sawMaxInstances = true;
        if (p.id == "instance.allocate.voice.seed") sawSeed = true;
    }
    CHECK_FALSE (sawConfiguration);
    CHECK (sawMaxInstances);
    CHECK (sawSeed); // 09-28-InstanceAllocator.2: real patch-level determinism
    REQUIRE (voice->parameters.size() == 2); // maxInstances + seed
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

TEST_CASE ("09-29-AddMenu.3: no registered node's category is fragmented by the Add menu's "
           "'/' nesting delimiter",
           "[engine][NodeFactory][NODE_EDITOR]")
{
    // ui/src/graph/categoryTree.ts splits `category` on "/" to build the Add
    // menu's nested flyouts ("Domain/Allocate" -> Domain > Allocate). A
    // category that isn't meant to nest must not contain a literal "/" of
    // its own, or it silently fragments into a bogus multi-letter-turned-
    // one-letter-per-segment flyout - exactly what happened to "I/O" (split
    // into a top-level "I" category with a nested "O" flyout) before this
    // test existed. A real category segment is always more than one
    // character, so "every segment has length > 1" catches this class of
    // mistake generically, for every current and future node, without
    // hardcoding which categories are allowed to nest.
    auto factory = bazalt::engine::buildDefaultNodeFactory();
    const auto descriptors = factory.describeAll();

    for (const auto& d : descriptors)
    {
        for (const auto& segment : juce::StringArray::fromTokens (d.category, "/", {}))
        {
            INFO ("node " << d.typeId << " has category \"" << d.category << "\"");
            CHECK (segment.length() > 1);
        }
    }
}
