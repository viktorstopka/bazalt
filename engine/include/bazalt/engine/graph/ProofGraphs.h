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
#include "bazalt/engine/nodes/RoundNode.h"
#include "bazalt/engine/nodes/ClampNode.h"
#include "bazalt/engine/nodes/RemapNode.h"
#include "bazalt/engine/nodes/ListenNode.h"
#include "bazalt/engine/nodes/ViewNodes.h"
#include "bazalt/engine/nodes/OutputNode.h"
#include "bazalt/engine/nodes/NormaliseNode.h"
#include "bazalt/engine/nodes/ThresholdNode.h"
#include "bazalt/engine/nodes/DownmixNode.h"
#include "bazalt/engine/nodes/InstanceAllocatorNode.h"
#include "bazalt/engine/nodes/InstanceMixNode.h"
#include "bazalt/engine/nodes/IoNoteInNode.h"
#include "bazalt/engine/nodes/SubtractNode.h"
#include "bazalt/engine/nodes/DivideNode.h"
#include "bazalt/engine/nodes/AbsNode.h"
#include "bazalt/engine/nodes/MinMaxNode.h"
#include "bazalt/engine/nodes/PowerNode.h"
#include "bazalt/engine/nodes/ModuloNode.h"
#include "bazalt/engine/nodes/SlewNode.h"
#include "bazalt/engine/nodes/CrossfadeNode.h"
#include "bazalt/engine/nodes/LogicNotNode.h"
#include "bazalt/engine/nodes/LogicToggleNode.h"
#include "bazalt/engine/nodes/LogicBooleanNode.h"
#include "bazalt/engine/nodes/LogicSelectNode.h"
#include "bazalt/engine/nodes/LogicCompareNode.h"
#include "bazalt/engine/nodes/SampleHoldNode.h"
#include "bazalt/engine/nodes/IoAudioInNode.h"
#include "bazalt/engine/nodes/IoControlNode.h"
#include "bazalt/engine/nodes/IoTransportNode.h"
#include "bazalt/engine/nodes/SineOscillatorNode.h"
#include "bazalt/engine/nodes/DcBlockNode.h"
#include "bazalt/engine/nodes/EnvelopeFollowerNode.h"
#include "bazalt/engine/nodes/PeakFilterNode.h"
#include "bazalt/engine/nodes/ShelfFilterNode.h"
#include "bazalt/engine/nodes/AllpassFilterNode.h"
#include "bazalt/engine/nodes/LadderFilterNode.h"

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
        factory.registerType ("math.round", [] { return std::make_unique<nodes::RoundNode>(); });
        factory.registerType ("math.clamp", [] { return std::make_unique<nodes::ClampNode>(); });
        factory.registerType ("adapt.remap", [] { return std::make_unique<nodes::RemapNode>(); });
        factory.registerType ("view.listen", [] { return std::make_unique<nodes::ListenNode>(); });
        factory.registerType ("view.scope", [] { return std::make_unique<nodes::ViewScopeNode>(); });
        factory.registerType ("view.spectrum", [] { return std::make_unique<nodes::ViewSpectrumNode>(); });
        factory.registerType ("view.meter", [] { return std::make_unique<nodes::ViewMeterNode>(); });
        factory.registerType ("io.output", [] { return std::make_unique<nodes::OutputNode>(); });
        factory.registerType ("adapt.normalise", [] { return std::make_unique<nodes::NormaliseNode>(); });
        factory.registerType ("adapt.threshold", [] { return std::make_unique<nodes::ThresholdNode>(); });
        factory.registerType ("mix.downmix", [] { return std::make_unique<nodes::DownmixNode>(); });
        factory.registerType ("instance.allocator", [] { return std::make_unique<nodes::InstanceAllocatorNode>(); });
        factory.registerType ("instance.mix", [] { return std::make_unique<nodes::InstanceMixNode>(); });
        factory.registerType ("io.noteIn", [] { return std::make_unique<nodes::IoNoteInNode>(); });
        // M21 Batch A, wave 1 — fixed-arity nodes needing no new infrastructure.
        factory.registerType ("math.subtract", [] { return std::make_unique<nodes::SubtractNode>(); });
        factory.registerType ("math.divide", [] { return std::make_unique<nodes::DivideNode>(); });
        factory.registerType ("math.abs", [] { return std::make_unique<nodes::AbsNode>(); });
        factory.registerType ("math.minmax", [] { return std::make_unique<nodes::MinMaxNode>(); });
        factory.registerType ("math.power", [] { return std::make_unique<nodes::PowerNode>(); });
        factory.registerType ("math.modulo", [] { return std::make_unique<nodes::ModuloNode>(); });
        factory.registerType ("math.slew", [] { return std::make_unique<nodes::SlewNode>(); });
        factory.registerType ("mix.crossfade", [] { return std::make_unique<nodes::CrossfadeNode>(); });
        factory.registerType ("logic.not", [] { return std::make_unique<nodes::LogicNotNode>(); });
        factory.registerType ("logic.toggle", [] { return std::make_unique<nodes::LogicToggleNode>(); });
        factory.registerType ("logic.boolean", [] { return std::make_unique<nodes::LogicBooleanNode>(); });
        factory.registerType ("logic.select", [] { return std::make_unique<nodes::LogicSelectNode>(); });
        factory.registerType ("logic.compare", [] { return std::make_unique<nodes::LogicCompareNode>(); });
        factory.registerType ("adapt.sampleHold", [] { return std::make_unique<nodes::SampleHoldNode>(); });
        factory.registerType ("io.audioIn", [] { return std::make_unique<nodes::IoAudioInNode>(); });
        factory.registerType ("io.control", [] { return std::make_unique<nodes::IoControlNode>(); });
        factory.registerType ("io.transport", [] { return std::make_unique<nodes::IoTransportNode>(); });
        // M22 (Basic synthesis), wave 1 — cheap wins needing no new DSP primitive.
        factory.registerType ("osc.sine", [] { return std::make_unique<nodes::SineOscillatorNode>(); });
        factory.registerType ("filter.dcBlock", [] { return std::make_unique<nodes::DcBlockNode>(); });
        factory.registerType ("env.follower", [] { return std::make_unique<nodes::EnvelopeFollowerNode>(); });
        // M22 wave 2 — the Biquad family.
        factory.registerType ("filter.peak", [] { return std::make_unique<nodes::PeakFilterNode>(); });
        factory.registerType ("filter.shelf", [] { return std::make_unique<nodes::ShelfFilterNode>(); });
        factory.registerType ("filter.allpass", [] { return std::make_unique<nodes::AllpassFilterNode>(); });
        // M22 wave 3 — filter.ladder, the one node NODE_CATALOG.md itself
        // flags numerically delicate.
        factory.registerType ("filter.ladder", [] { return std::make_unique<nodes::LadderFilterNode>(); });
        return factory;
    }

    /** ARCHITECTURE.md §3.4's example voice path, M18-rewired (ADR-0024):
        MIDI -> io.noteIn -> instance.allocator -> PolyBLEP osc (pitch) ->
        SVF -> ADSR-gated amp (gate) -> out. Purely acyclic — every node
        schedules as an ordinary block-rate step. `instance.allocator`'s
        "spawn" input is a real Note-typed connection now, not the inert
        placeholder M17 shipped it with; `osc`/`env`'s "pitch"/"gate" ports
        are real too — `PluginProcessor::triggerVoiceNote`/`handleMidiEvent`
        poke `noteIn` directly (the one remaining direct C++ poke) and
        everything downstream of it is ordinary graph wiring.
    */
    inline NodeGraph buildVoiceProofGraph()
    {
        NodeGraph graph;

        // Hand-placed, not left at the (0,0) NodePosition default every one
        // of these would otherwise share — a real gap found via actual
        // hands-on testing (M19): with no position, every node in the
        // default graph rendered exactly stacked on top of every other,
        // and every cable had to converge on that same single point,
        // reading as a chaotic mess of crossing lines with nothing to do
        // with a rendering bug. Roughly follows the signal flow left to
        // right: noteIn/allocator feed osc (pitch) and env (gate) below/
        // beside them; osc -> svf -> amp; env -> amp.
        graph.addNode ({ "noteIn", "io.noteIn", { 40.0f, 40.0f }, {}, {} });
        graph.addNode ({ "allocator", "instance.allocator", { 340.0f, 40.0f }, {}, {} });
        graph.addNode ({ "env", "env.adsr", { 640.0f, 40.0f }, {}, {} });
        graph.addNode ({ "osc", "osc.analog", { 340.0f, 420.0f }, {}, {} });
        graph.addNode ({ "svf", "filter.svf", { 640.0f, 420.0f }, { { "filter.svf.cutoff", 3000.0f }, { "filter.svf.resonance", 0.9f } }, {} });
        graph.addNode ({ "amp", "mix.gain", { 940.0f, 230.0f }, {}, {} });

        graph.addConnection ({ "noteIn", "notes", "allocator", "spawn" });
        graph.addConnection ({ "allocator", "pitch", "osc", "pitch" });
        graph.addConnection ({ "allocator", "gate", "env", "gate" });
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

        graph.addConnection ({ "excite", "out", "mix", "in.0" });
        graph.addConnection ({ "damp", "out", "mix", "in.1" });
        graph.addConnection ({ "mix", "out", "delay", "in" });
        graph.addConnection ({ "delay", "out", "damp", "in" });

        graph.setOutput ("damp", "out");

        return graph;
    }
}
