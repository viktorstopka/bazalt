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

namespace bazalt::engine
{
    /** Registers every M2 node type under its stable type id. Shared by
        render-cli and the Catch2 suite so the two hardcoded proof graphs
        below only need to be built once.
    */
    inline NodeFactory buildDefaultNodeFactory()
    {
        NodeFactory factory;
        factory.registerType ("osc.basic", [] { return std::make_unique<nodes::OscillatorNode>(); });
        factory.registerType ("filter.svf", [] { return std::make_unique<nodes::SvfFilterNode>(); });
        factory.registerType ("env.adsr", [] { return std::make_unique<nodes::AdsrNode>(); });
        factory.registerType ("amp.vca", [] { return std::make_unique<nodes::GainNode>(); });
        factory.registerType ("noise.burst", [] { return std::make_unique<nodes::NoiseBurstNode>(); });
        factory.registerType ("mix.add2", [] { return std::make_unique<nodes::MixNode>(); });
        factory.registerType ("delay.basic", [] { return std::make_unique<nodes::DelayNode>(); });
        factory.registerType ("filter.onepole", [] { return std::make_unique<nodes::OnePoleFilterNode>(); });
        return factory;
    }

    /** ARCHITECTURE.md §3.4's example voice path: note -> PolyBLEP osc ->
        SVF -> ADSR-gated amp -> out. Purely acyclic — every node schedules
        as an ordinary block-rate step.
    */
    inline NodeGraph buildVoiceProofGraph()
    {
        NodeGraph graph;

        graph.addNode ({ "osc", "osc.basic", {} });
        graph.addNode ({ "svf", "filter.svf", { { "filter.svf.cutoff", 3000.0f }, { "filter.svf.resonance", 0.9f } } });
        graph.addNode ({ "env", "env.adsr", {} });
        graph.addNode ({ "amp", "amp.vca", {} });

        graph.addConnection ({ "osc", 0, "svf", 0 });
        graph.addConnection ({ "svf", 0, "amp", 0 });
        graph.addConnection ({ "env", 0, "amp", 1 });

        graph.setOutput ("amp", 0);

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

        graph.addNode ({ "excite", "noise.burst", {} });
        graph.addNode ({ "mix", "mix.add2", {} });
        graph.addNode ({ "delay", "delay.basic", { { "delay.basic.samples", 200.0f } } });
        graph.addNode ({ "damp", "filter.onepole", { { "filter.onepole.coefficient", 0.5f } } });

        graph.addConnection ({ "excite", 0, "mix", 0 });
        graph.addConnection ({ "damp", 0, "mix", 1 });
        graph.addConnection ({ "mix", 0, "delay", 0 });
        graph.addConnection ({ "delay", 0, "damp", 0 });

        graph.setOutput ("damp", 0);

        return graph;
    }
}
