#pragma once

#include "bazalt/engine/graph/NodeGraph.h"
#include "bazalt/engine/graph/NodeFactory.h"
#include "bazalt/engine/nodes/OscillatorNode.h"
#include "bazalt/engine/nodes/SvfFilterNode.h"
#include "bazalt/engine/nodes/AdsrNode.h"
#include "bazalt/engine/nodes/GainNode.h"
#include "bazalt/engine/nodes/NoiseBurstNode.h"
#include "bazalt/engine/nodes/MixNode.h"
#include "bazalt/engine/nodes/DelayNode.h"
#include "bazalt/engine/nodes/OnePoleFilterNode.h"
#include "bazalt/engine/nodes/ConstantNode.h"
#include "bazalt/engine/nodes/RerouteNode.h"
#include "bazalt/engine/nodes/MapNode.h"
#include "bazalt/engine/nodes/AddNode.h"
#include "bazalt/engine/nodes/MultiplyNode.h"
#include "bazalt/engine/nodes/ListenNode.h"
#include "bazalt/engine/nodes/OutputNode.h"
#include "bazalt/engine/nodes/NormaliseNode.h"
#include "bazalt/engine/nodes/ThresholdNode.h"
#include "bazalt/engine/nodes/DownmixNode.h"
#include "bazalt/engine/nodes/InstanceAllocatorNode.h"
#include "bazalt/engine/nodes/InstanceMixNode.h"

namespace bazalt::engine
{
    /** Registers every M1/M2 DSP node type, the M7 utility node types
        (NODE_EDITOR.md §2), M16's adapter nodes, and M17's
        instance.allocator/instance.mix (superseding util.voiceSum) under
        their stable type ids. Shared by render-cli and the Catch2 suite
        so the two hardcoded proof graphs below only need to be built once.
    */
    inline NodeFactory buildDefaultNodeFactory()
    {
        NodeFactory factory;
        factory.registerType ("osc.analog", [] { return std::make_unique<nodes::OscillatorNode>(); });
        factory.registerType ("filter.svf", [] { return std::make_unique<nodes::SvfFilterNode>(); });
        factory.registerType ("env.adsr", [] { return std::make_unique<nodes::AdsrNode>(); });
        factory.registerType ("mix.gain", [] { return std::make_unique<nodes::GainNode>(); });
        factory.registerType ("excite.burst", [] { return std::make_unique<nodes::NoiseBurstNode>(); });
        factory.registerType ("mix.sum", [] { return std::make_unique<nodes::MixNode>(); });
        factory.registerType ("delay.line", [] { return std::make_unique<nodes::DelayNode>(); });
        factory.registerType ("filter.onepole", [] { return std::make_unique<nodes::OnePoleFilterNode>(); });
        factory.registerType ("util.constant", [] { return std::make_unique<nodes::ConstantNode>(); });
        factory.registerType ("util.reroute", [] { return std::make_unique<nodes::RerouteNode>(); });
        factory.registerType ("adapt.map", [] { return std::make_unique<nodes::MapNode>(); });
        factory.registerType ("math.add", [] { return std::make_unique<nodes::AddNode>(); });
        factory.registerType ("math.multiply", [] { return std::make_unique<nodes::MultiplyNode>(); });
        factory.registerType ("view.listen", [] { return std::make_unique<nodes::ListenNode>(); });
        factory.registerType ("io.output", [] { return std::make_unique<nodes::OutputNode>(); });
        factory.registerType ("adapt.normalise", [] { return std::make_unique<nodes::NormaliseNode>(); });
        factory.registerType ("adapt.threshold", [] { return std::make_unique<nodes::ThresholdNode>(); });
        factory.registerType ("mix.downmix", [] { return std::make_unique<nodes::DownmixNode>(); });
        factory.registerType ("instance.allocator", [] { return std::make_unique<nodes::InstanceAllocatorNode>(); });
        factory.registerType ("instance.mix", [] { return std::make_unique<nodes::InstanceMixNode>(); });
        return factory;
    }

    /** ARCHITECTURE.md §3.4's example voice path: note -> PolyBLEP osc ->
        SVF -> ADSR-gated amp -> out. Purely acyclic — every node schedules
        as an ordinary block-rate step.
    */
    inline NodeGraph buildVoiceProofGraph()
    {
        NodeGraph graph;

        graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
        graph.addNode ({ "svf", "filter.svf", {}, { { "filter.svf.cutoff", 3000.0f }, { "filter.svf.resonance", 0.9f } }, {} });
        graph.addNode ({ "env", "env.adsr", {}, {}, {} });
        graph.addNode ({ "amp", "mix.gain", {}, {}, {} });

        graph.addConnection ({ "osc", "out", "svf", "in" });
        graph.addConnection ({ "svf", "out", "amp", "audio" });
        graph.addConnection ({ "env", "out", "amp", "gain" });

        graph.setOutput ("amp", "out");

        return graph;
    }

    /** ARCHITECTURE.md §3.4's Karplus-Strong proof: a noise-burst
        excitation feeds a Mix node, whose output runs through a Delay
        (the pitch period) and a damping OnePoleFilter, which feeds back
        into the Mix node — one genuine feedback cycle, forcing the
        compiler to route {mix, delay, damp} through a per-sample region.
        The graph's own designated output (damp's output) is itself inside
        that region, which specifically exercises the "region's output is
        also the plan's final output" path in GraphCompiler.
    */
    inline NodeGraph buildKarplusStrongProofGraph()
    {
        NodeGraph graph;

        graph.addNode ({ "excite", "excite.burst", {}, {}, {} });
        graph.addNode ({ "mix", "mix.sum", {}, {}, {} });
        graph.addNode ({ "delay", "delay.line", {}, { { "delay.line.samples", 200.0f } }, {} });
        graph.addNode ({ "damp", "filter.onepole", {}, { { "filter.onepole.coefficient", 0.5f } }, {} });

        graph.addConnection ({ "excite", "out", "mix", "a" });
        graph.addConnection ({ "damp", "out", "mix", "b" });
        graph.addConnection ({ "mix", "out", "delay", "in" });
        graph.addConnection ({ "delay", "out", "damp", "in" });

        graph.setOutput ("damp", "out");

        return graph;
    }
}
