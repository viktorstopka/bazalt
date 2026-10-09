#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/LogicCompareNode.h"
#include "bazalt/engine/nodes/BlendNode.h"
#include "bazalt/engine/nodes/SampleHoldNode.h"
#include "bazalt/engine/nodes/ViewCycleNode.h"
#include <cmath>
#include <limits>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    // GraphCompiler's "this fallback port is unconnected" sentinel.
    const float unwired = std::numeric_limits<float>::quiet_NaN();

    PortDescriptor sourcePort (SignalType type, Quantity quantity = Quantity::Dimensionless)
    {
        PortDescriptor port { "src", type };
        port.quantity = quantity;
        return port;
    }

    // A Control source that declares a real quantity, so quantity inheritance
    // has something to inherit.
    class QuantitySourceNode : public Node
    {
    public:
        explicit QuantitySourceNode (Quantity q) : quantity (q) {}
        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            PortDescriptor port { .id = "out", .type = SignalType::Signal };
            port.quantity = quantity;
            return { port };
        }
        void setParameter (const juce::String& id, float value) override
        {
            if (id == "value")
                constantValue = value;
        }
        void processSample (const float*, float* outputs) noexcept override { outputs[0] = constantValue; }

    private:
        Quantity quantity;
        float constantValue = 0.0f;
    };

    NodeFactory makeFactory()
    {
        auto factory = buildDefaultNodeFactory();
        factory.registerType ("test.frequency", [] { return std::make_unique<QuantitySourceNode> (Quantity::Frequency); });
        factory.registerType ("test.pitch", [] { return std::make_unique<QuantitySourceNode> (Quantity::Pitch); });
        return factory;
    }

    float finalOutput (CompileResult& result)
    {
        result.plan.process (8);
        return result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0)[7];
    }

    const PortDescriptor& portNamed (const std::vector<PortDescriptor>& ports, const juce::String& id)
    {
        for (const auto& port : ports)
            if (port.id == id)
                return port;
        FAIL ("no port " << id);
        return ports.front();
    }
}

// ---- math.blend (was logic.select's role) --------------------------------

TEST_CASE ("math.blend picks A at Amount 0 and B at Amount 1, and blends between them",
           "[engine][nodes][math][inheriting]")
{
    BlendNode node; // not prepared: no smoothing, each sample lands on its Amount
    float out = 0.0f;

    auto blend = [&] (float a, float b, float amount)
    {
        const float inputs[3] = { a, b, amount };
        node.processSample (inputs, &out);
        return out;
    };

    CHECK (blend (10.0f, 20.0f, 0.0f) == 10.0f);
    CHECK (blend (10.0f, 20.0f, 1.0f) == 20.0f); // a Boolean Amount is the old Select
    CHECK (blend (10.0f, 20.0f, 0.5f) == Catch::Approx (15.0f));
    CHECK (blend (10.0f, 20.0f, unwired) == Catch::Approx (15.0f)); // unwired: the stored 0.5
}

TEST_CASE ("math.blend's A, B and Out adopt the wired type and quantity; Amount stays a 0..1 value",
           "[engine][nodes][math][inheriting]")
{
    BlendNode node;
    REQUIRE (node.hasPolymorphicPorts());
    CHECK (portNamed (node.getInputPorts(), "a").quantity == Quantity::Audio); // unconnected default

    node.resolveIncomingPort ("a", sourcePort (SignalType::Signal, Quantity::Frequency));
    for (const auto* id : { "a", "b" })
    {
        CHECK (portNamed (node.getInputPorts(), id).type == SignalType::Signal);
        CHECK (portNamed (node.getInputPorts(), id).quantity == Quantity::Frequency);
    }
    CHECK (portNamed (node.getOutputPorts(), "out").quantity == Quantity::Frequency);
    CHECK (portNamed (node.getInputPorts(), "math.blend.amount").quantity == Quantity::Unipolar);

    // A cable on Amount is never a source of the blended type.
    BlendNode other;
    other.resolveIncomingPort ("math.blend.amount", sourcePort (SignalType::Signal, Quantity::Boolean));
    CHECK (portNamed (other.getInputPorts(), "a").quantity == Quantity::Audio);
}

TEST_CASE ("math.blend resolves by port priority, not by the order sources are offered",
           "[engine][nodes][math][inheriting]")
{
    for (const auto bFirst : { true, false })
    {
        DYNAMIC_SECTION ("b offered " << (bFirst ? "first" : "second"))
        {
            BlendNode node;
            const auto audio = sourcePort (SignalType::Signal, Quantity::Audio);
            const auto control = sourcePort (SignalType::Signal, Quantity::Frequency);
            if (bFirst)
            {
                node.resolveIncomingPort ("b", control);
                node.resolveIncomingPort ("a", audio);
            }
            else
            {
                node.resolveIncomingPort ("a", audio);
                node.resolveIncomingPort ("b", control);
            }
            CHECK (portNamed (node.getOutputPorts(), "out").type == SignalType::Signal);
            CHECK (portNamed (node.getOutputPorts(), "out").quantity == Quantity::Audio);
        }
    }

    for (const auto type : { SignalType::Note, SignalType::Data, SignalType::Spectral })
    {
        BlendNode node;
        node.resolveIncomingPort ("a", sourcePort (type));
        CHECK (portNamed (node.getOutputPorts(), "out").quantity == Quantity::Audio);
    }
}

TEST_CASE ("math.blend smooths a jump in Amount instead of stepping, at any sample rate",
           "[engine][nodes][math]")
{
    for (const auto sampleRate : { 44100.0, 96000.0 })
    {
        BlendNode node;
        node.prepare ({ sampleRate, 64 });
        float out = 0.0f;
        float inputs[3] = { 0.0f, 1.0f, 0.0f };
        node.processSample (inputs, &out);
        CHECK (out == 0.0f); // starts where it is asked to

        inputs[2] = 1.0f; // a Boolean flipping
        node.processSample (inputs, &out);
        CHECK (out > 0.0f);
        CHECK (out < 0.1f); // no step

        // ~5 ms time constant: well past 99% after 30 ms.
        for (int i = 0; i < (int) (0.03 * sampleRate); ++i)
            node.processSample (inputs, &out);
        CHECK (out == Catch::Approx (1.0f).margin (0.01f));
    }
}

// ---- logic.compare --------------------------------------------------------

TEST_CASE ("logic.compare applies each op, with tolerance only on = and !=", "[engine][nodes][logic][M21][inheriting]")
{
    auto compare = [&] (float opValue, float a, float b, float tolerance = unwired)
    {
        LogicCompareNode node;
        node.setParameter ("logic.compare.op", opValue);
        const float inputs[3] = { a, b, tolerance };
        float out = -1.0f;
        node.processSample (inputs, &out);
        return out;
    };

    // > >= = != <= <  (ops 0..5)
    CHECK (compare (0.0f, 2.0f, 1.0f) == 1.0f);
    CHECK (compare (0.0f, 1.0f, 1.0f) == 0.0f);
    CHECK (compare (1.0f, 1.0f, 1.0f) == 1.0f);
    CHECK (compare (1.0f, 0.5f, 1.0f) == 0.0f);
    CHECK (compare (4.0f, 1.0f, 1.0f) == 1.0f);
    CHECK (compare (4.0f, 2.0f, 1.0f) == 0.0f);
    CHECK (compare (5.0f, 0.5f, 1.0f) == 1.0f);
    CHECK (compare (5.0f, 1.0f, 1.0f) == 0.0f);

    // = / != use the tolerance (default 0.001), not exact float equality.
    CHECK (compare (2.0f, 1.0f, 1.0005f) == 1.0f);
    CHECK (compare (2.0f, 1.0f, 1.01f) == 0.0f);
    CHECK (compare (3.0f, 1.0f, 1.0005f) == 0.0f);
    CHECK (compare (3.0f, 1.0f, 1.01f) == 1.0f);
    CHECK (compare (2.0f, 1.0f, 1.5f, 1.0f) == 1.0f);  // a wired tolerance overrides the stored one
    CHECK (compare (2.0f, 1.0f, 1.5f, 0.1f) == 0.0f);

    // ...but the ordering ops stay exact: 1.0005 is greater than 1 even though "equal" under the tolerance.
    CHECK (compare (0.0f, 1.0005f, 1.0f) == 1.0f);
}

TEST_CASE ("logic.compare never reports a NaN comparison as true, in either direction",
           "[engine][nodes][logic][M21][inheriting]")
{
    for (const auto op : { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f })
    {
        LogicCompareNode node;
        node.setParameter ("logic.compare.op", op);
        const float inputs[3] = { unwired, 1.0f, unwired };
        float out = -1.0f;
        node.processSample (inputs, &out);
        CHECK (out == 0.0f);
    }
}

TEST_CASE ("logic.compare inherits one quantity across a, b and tolerance, but always stays Control",
           "[engine][nodes][logic][M21][inheriting]")
{
    LogicCompareNode node;
    node.resolveIncomingPort ("a", sourcePort (SignalType::Signal, Quantity::Frequency));

    for (const auto* id : { "a", "b", "logic.compare.tolerance" })
        CHECK (portNamed (node.getInputPorts(), id).quantity == Quantity::Frequency);
    CHECK (portNamed (node.getOutputPorts(), "out").quantity == Quantity::Boolean);

    // a outranks b: a later, different quantity on b does not override it.
    node.resolveIncomingPort ("b", sourcePort (SignalType::Signal, Quantity::Pitch));
    CHECK (portNamed (node.getInputPorts(), "b").quantity == Quantity::Frequency);

    // An Audio source is a plain value too, but the compared type is fixed.
    LogicCompareNode audioFed;
    audioFed.resolveIncomingPort ("a", sourcePort (SignalType::Signal, Quantity::Audio));
    CHECK (portNamed (audioFed.getInputPorts(), "a").type == SignalType::Signal);
}

TEST_CASE ("logic.compare through the real compiler: matching quantities compile, a Frequency-vs-Pitch mix-up is rejected",
           "[engine][nodes][logic][M21][inheriting][GraphCompiler]")
{
    const auto factory = makeFactory();

    auto compileWith = [&] (const juce::String& aType, const juce::String& bType)
    {
        NodeGraph graph;
        graph.addNode ({ "a", aType, {}, { { "value", 440.0f } }, {} });
        graph.addNode ({ "b", bType, {}, { { "value", 440.0f } }, {} });
        graph.addNode ({ "cmp", "logic.compare", {}, { { "logic.compare.op", 2.0f } }, {} }); // =
        graph.addConnection ({ "a", "out", "cmp", "a" });
        graph.addConnection ({ "b", "out", "cmp", "b" });
        graph.setOutput ("cmp", "out");
        return GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    };

    auto same = compileWith ("test.frequency", "test.frequency");
    REQUIRE (same.success);
    CHECK (finalOutput (same) == 1.0f); // 440 = 440

    // b inherits Frequency from a, so a Pitch cable into b needs an adapter it
    // doesn't have — caught at compile time instead of comparing 440 with 69.
    const auto mixedUp = compileWith ("test.frequency", "test.pitch");
    CHECK_FALSE (mixedUp.success);
    CHECK (mixedUp.errorMessage.isNotEmpty());
}

// ---- math.blend through a Reroute chain ----------------------------------

TEST_CASE ("math.blend resolves through a Reroute chain declared sink-first, and switches the value",
           "[engine][nodes][math][inheriting][GraphCompiler]")
{
    const auto factory = makeFactory();

    NodeGraph graph;
    // Declared out of dependency order on purpose: blend, rr2, rr1, sources.
    graph.addNode ({ "sel", "math.blend", {}, {}, {} });
    graph.addNode ({ "rr2", "deco.reroute", {}, {}, {} });
    graph.addNode ({ "rr1", "deco.reroute", {}, {}, {} });
    graph.addNode ({ "hz", "test.frequency", {}, { { "value", 440.0f } }, {} });
    graph.addNode ({ "other", "test.frequency", {}, { { "value", 220.0f } }, {} });
    graph.addNode ({ "cond", "logic.not", {}, {}, {} }); // unwired input reads false, so its NOT is true
    graph.addConnection ({ "hz", "out", "rr1", "in" });
    graph.addConnection ({ "rr1", "out", "rr2", "in" });
    graph.addConnection ({ "rr2", "out", "sel", "b" });
    graph.addConnection ({ "other", "out", "sel", "a" });
    graph.addConnection ({ "cond", "out", "sel", "math.blend.amount" });
    graph.setOutput ("sel", "out");

    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);

    const auto& blend = *result.plan.nodes[(size_t) result.plan.nodeIdToSlot.at ("sel")];
    CHECK (portNamed (blend.getOutputPorts(), "out").quantity == Quantity::Frequency); // carried through two Reroutes
    CHECK (portNamed (blend.getOutputPorts(), "out").type == SignalType::Signal);
    CHECK (finalOutput (result) == 440.0f); // Amount true -> B
}

// ---- adapt.sampleHold -----------------------------------------------------

TEST_CASE ("adapt.sampleHold samples on each trigger and holds between them", "[engine][nodes][adapt][M21][inheriting]")
{
    SampleHoldNode node;
    float out = -1.0f;

    auto step = [&] (float in, float trigger)
    {
        const float inputs[3] = { in, trigger, unwired };
        node.processSample (inputs, &out);
        return out;
    };

    CHECK (step (0.7f, 0.0f) == 0.0f); // nothing sampled yet
    CHECK (step (0.7f, 1.0f) == 0.7f); // trigger: takes the value
    CHECK (step (0.2f, 0.0f) == 0.7f); // holds while the input moves
    CHECK (step (0.9f, 0.0f) == 0.7f);
    CHECK (step (0.9f, 1.0f) == 0.9f); // next trigger
    CHECK (step (0.1f, 0.0f) == 0.9f);
}

TEST_CASE ("adapt.sampleHold glides to a new held value over `glide`, at any sample rate; the first trigger jumps",
           "[engine][nodes][adapt][M21][inheriting]")
{
    const auto runOneGlide = [] (double sampleRate)
    {
        SampleHoldNode node;
        NodePrepareInfo info;
        info.sampleRate = sampleRate;
        node.prepare (info);
        node.setParameter ("time.sampleHold.glide", 0.010f);

        float out = 0.0f;
        float inputs[3] = { 1.0f, 1.0f, unwired };
        node.processSample (inputs, &out);
        REQUIRE (out == 1.0f); // the very first trigger jumps: no glide up from 0

        inputs[0] = 0.0f; // sample a new value of 0 ...
        node.processSample (inputs, &out);
        inputs[1] = 0.0f; // ... then let it glide for one time constant of real time
        const auto numSamples = (int) std::lround (0.010 * sampleRate);
        for (int i = 0; i < numSamples; ++i)
            node.processSample (inputs, &out);
        return out;
    };

    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        CHECK (runOneGlide (rate) == Catch::Approx (std::exp (-1.0f)).margin (0.01));
}

TEST_CASE ("adapt.sampleHold ignores a non-finite input at a trigger, and reset() clears it",
           "[engine][nodes][adapt][M21][inheriting]")
{
    SampleHoldNode node;
    float out = -1.0f;

    float inputs[3] = { 0.5f, 1.0f, unwired };
    node.processSample (inputs, &out);
    REQUIRE (out == 0.5f);

    inputs[0] = std::numeric_limits<float>::infinity();
    node.processSample (inputs, &out);
    CHECK (out == 0.5f); // not held: one bad sample must not poison the output
    inputs[0] = unwired;
    node.processSample (inputs, &out);
    CHECK (out == 0.5f);

    node.reset();
    inputs[0] = 0.3f;
    inputs[1] = 0.0f;
    node.processSample (inputs, &out);
    CHECK (out == 0.0f); // back to "nothing sampled yet"
}

TEST_CASE ("time.sampleHold's in and out share the source's quantity; trigger and glide do not influence it",
           "[engine][nodes][adapt][M21][inheriting]")
{
    SampleHoldNode node;
    node.resolveIncomingPort ("trigger", sourcePort (SignalType::Event));
    node.resolveIncomingPort ("time.sampleHold.glide", sourcePort (SignalType::Signal, Quantity::Time));
    CHECK (portNamed (node.getOutputPorts(), "out").quantity == Quantity::Dimensionless);

    node.resolveIncomingPort ("in", sourcePort (SignalType::Signal, Quantity::Frequency));
    CHECK (portNamed (node.getInputPorts(), "in").quantity == Quantity::Frequency);
    CHECK (portNamed (node.getOutputPorts(), "out").quantity == Quantity::Frequency);
    CHECK (portNamed (node.getInputPorts(), "trigger").type == SignalType::Event);
}

// ---- math.clamp ------------------------------------------------------------

TEST_CASE ("view.cycle adopts the wired type and quantity on both in and out, and passes the value through unchanged",
           "[engine][nodes][view][M0.6][inheriting]")
{
    ViewCycleNode node;
    REQUIRE (node.hasPolymorphicPorts());

    // Unconnected default.
    CHECK (portNamed (node.getInputPorts(), "in").quantity == Quantity::Audio);
    CHECK (portNamed (node.getOutputPorts(), "out").quantity == Quantity::Audio);

    node.resolveIncomingPort ("in", sourcePort (SignalType::Signal, Quantity::Pitch));
    CHECK (portNamed (node.getInputPorts(), "in").type == SignalType::Signal);
    CHECK (portNamed (node.getInputPorts(), "in").quantity == Quantity::Pitch);
    CHECK (portNamed (node.getOutputPorts(), "out").type == SignalType::Signal);
    CHECK (portNamed (node.getOutputPorts(), "out").quantity == Quantity::Pitch);

    float in = 0.42f;
    float out = 0.0f;
    node.processSample (&in, &out);
    CHECK (out == in);
}

TEST_CASE ("view.cycle through the real compiler: splices into a Control cable, passing the live value through unchanged",
           "[engine][nodes][view][M0.6][inheriting][GraphCompiler]")
{
    auto factory = makeFactory();

    NodeGraph graph;
    graph.addNode ({ "src", "test.frequency", {}, { { "value", 220.0f } }, {} });
    graph.addNode ({ "glance", "view.cycle", {}, {}, {} });
    graph.addConnection ({ "src", "out", "glance", "in" });
    graph.setOutput ("glance", "out");

    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);
    CHECK (finalOutput (result) == Catch::Approx (220.0f));
}
