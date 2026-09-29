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
#include "bazalt/engine/nodes/ViewGlanceNode.h"
#include "bazalt/engine/nodes/OutputNode.h"
#include "bazalt/engine/nodes/NormaliseNode.h"
#include "bazalt/engine/nodes/ThresholdNode.h"
#include "bazalt/engine/nodes/AudioToControlNode.h"
#include "bazalt/engine/nodes/DownmixNode.h"
#include "bazalt/engine/nodes/InstanceVoiceNode.h"
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
#include "bazalt/engine/nodes/RandomSteppedNode.h"
#include "bazalt/engine/nodes/RandomDriftNode.h"
#include "bazalt/engine/nodes/PanNode.h"
#include "bazalt/engine/nodes/WidthNode.h"
#include "bazalt/engine/nodes/StereoSplitNode.h"
#include "bazalt/engine/nodes/StereoCombineNode.h"
#include "bazalt/engine/nodes/ClockPulseNode.h"
#include "bazalt/engine/nodes/ClockDivideNode.h"
#include "bazalt/engine/nodes/ClockCounterNode.h"
#include "bazalt/engine/nodes/SeqStepsNode.h"
#include "bazalt/engine/nodes/SeqEuclidNode.h"
#include "bazalt/engine/nodes/DataScaleNode.h"
#include "bazalt/engine/nodes/DataTableNode.h"
#include "bazalt/engine/nodes/DataLookupNode.h"
#include "bazalt/engine/nodes/NoteGateNode.h"
#include "bazalt/engine/nodes/NoteValueNode.h"
#include "bazalt/engine/nodes/NoteTransposeNode.h"
#include "bazalt/engine/nodes/NoteFilterNode.h"
#include "bazalt/engine/nodes/NoteHumanizeNode.h"
#include "bazalt/engine/nodes/NoteQuantizeNode.h"

namespace bazalt::engine
{
    /** Registers every M1/M2 DSP node type, the M7 utility node types
        (NODE_EDITOR.md §2), M16's adapter nodes, and M17's
        instance.allocate.voice/instance.mix (superseding util.voiceSum) under
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
        factory.registerType ("view.glance", [] { return std::make_unique<nodes::ViewGlanceNode>(); });
        factory.registerType ("io.output", [] { return std::make_unique<nodes::OutputNode>(); });
        factory.registerType ("adapt.normalise", [] { return std::make_unique<nodes::NormaliseNode>(); });
        factory.registerType ("adapt.threshold", [] { return std::make_unique<nodes::ThresholdNode>(); });
        // Audio -> Control Bridge (wiki/plans/AudioControlBridge.md) — the
        // mechanical adapter canConnect auto-inserts for Audio -> Control.
        factory.registerType ("adapt.audioToControl", [] { return std::make_unique<nodes::AudioToControlNode>(); });
        factory.registerType ("mix.downmix", [] { return std::make_unique<nodes::DownmixNode>(); });
        factory.registerType ("instance.allocate.voice", [] { return std::make_unique<nodes::InstanceVoiceNode>(); }); // 09-28-InstanceAllocator.3 — renamed from "instance.allocator"; 09-29-AddMenu.1 — renamed again from "instance.voice"
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
        // M22 wave 4 — random.stepped, random.drift.
        factory.registerType ("random.stepped", [] { return std::make_unique<nodes::RandomSteppedNode>(); });
        factory.registerType ("random.drift", [] { return std::make_unique<nodes::RandomDriftNode>(); });
        // M22 wave 5 — space.pan, space.width; ADR-0023 Amendment (M22)
        // settles the left/right-vs-Channels::Stereo question these first
        // real stereo nodes raised.
        factory.registerType ("space.pan", [] { return std::make_unique<nodes::PanNode>(); });
        factory.registerType ("space.width", [] { return std::make_unique<nodes::WidthNode>(); });
        factory.registerType ("stereo.split", [] { return std::make_unique<nodes::StereoSplitNode>(); });
        factory.registerType ("stereo.combine", [] { return std::make_unique<nodes::StereoCombineNode>(); });
        // Clock+Seq batch (wiki/NODES.Status.md's own build-next order, step 1).
        factory.registerType ("clock.pulse", [] { return std::make_unique<nodes::ClockPulseNode>(); });
        factory.registerType ("clock.divide", [] { return std::make_unique<nodes::ClockDivideNode>(); });
        factory.registerType ("clock.counter", [] { return std::make_unique<nodes::ClockCounterNode>(); });
        factory.registerType ("seq.steps", [] { return std::make_unique<nodes::SeqStepsNode>(); });
        factory.registerType ("seq.euclid", [] { return std::make_unique<nodes::SeqEuclidNode>(); });
        // Data Foundations batch — the first real Data-producing/consuming nodes.
        factory.registerType ("data.scale", [] { return std::make_unique<nodes::DataScaleNode>(); });
        factory.registerType ("data.table", [] { return std::make_unique<nodes::DataTableNode>(); });
        factory.registerType ("data.lookup", [] { return std::make_unique<nodes::DataLookupNode>(); });
        // Note Stream batch — note.hold/note.select/note.chord deferred (a
        // real, documented engine limit: ExecutionPlan::BlockStep supports
        // only one Note output per node, see NoteFilterNode.h's own comment).
        factory.registerType ("note.gate", [] { return std::make_unique<nodes::NoteGateNode>(); });
        factory.registerType ("note.value", [] { return std::make_unique<nodes::NoteValueNode>(); });
        factory.registerType ("note.transpose", [] { return std::make_unique<nodes::NoteTransposeNode>(); });
        factory.registerType ("note.filter", [] { return std::make_unique<nodes::NoteFilterNode>(); });
        factory.registerType ("note.humanize", [] { return std::make_unique<nodes::NoteHumanizeNode>(); });
        factory.registerType ("note.quantize", [] { return std::make_unique<nodes::NoteQuantizeNode>(); });
        return factory;
    }

    /** M22 — the Init Patch (NODE_CATALOG.md's Part B one-liner: `io.noteIn`
        `-> instance.allocate.voice -> osc.analog x2 -> filter.ladder -> env.adsr
        -> instance.mix -> space.reverb`), built for real and made
        `GraphEditController`'s constructor default (replacing
        `buildVoiceProofGraph()` there — that graph stays registered and
        tested, just no longer what a fresh instance opens with). Two
        detuned saw oscillators through a resonant ladder filter with its
        OWN envelope (so the tone brightens and settles independently of the
        amp envelope — what actually makes a subtractive synth feel alive,
        not just "does it compile"), key-tracked so higher notes stay
        proportionally bright, into a VCA, into `instance.mix` — the first
        time this node is exercised by a real, non-synthetic graph rather
        than a test-only one built just to exercise `hasGlobalDomain`.

        No `space.reverb` tail: it's M28's node, doesn't exist yet — a
        forced, expected scope boundary (M22 can't build M28's node), not an
        oversight.

        This M22-era comment originally noted "no `space.pan` tail either" —
        genuinely mono end to end, since neither `io.output` nor
        `PluginProcessor` tracked a second channel at all back then. That
        gap closed for real in two steps: a narrow point-fix (superseded
        Milestone 0.2), then the real stereo cable redesign
        (`wiki/NODES.System.md` §9) — `space.pan`'s output is now one real
        `Channels::Stereo` port, and this patch wires it straight into
        `masterOut`'s equally-real stereo `in` as one cable. A fresh plugin
        instance now opens playing genuinely panned stereo, not mono
        duplicated to both speakers.

        `detuneOffset`/`baseCutoff` are `util.constant` feeding `math.add`,
        not initial parameter values on `osc2`/`filter.ladder` directly —
        `osc.analog`'s "pitch" port has no `setParameter` case at all (it's
        wire-only, see `OscillatorNode.h`), and `filter.ladder.cutoff`,
        once wired to the modulation sum below, ignores whatever its own
        stored/initial parameter says (a wired port's live value always
        wins) — so injecting the "base" amount has to happen INSIDE the
        modulation chain, not on the destination node itself.
    */
    inline NodeGraph buildInitPatchGraph()
    {
        NodeGraph graph;

        graph.addNode ({ "noteIn", "io.noteIn", { 40.0f, 260.0f }, {}, {} });
        graph.addNode ({ "allocator", "instance.allocate.voice", { 340.0f, 260.0f }, {}, {} });

        graph.addNode ({ "osc1", "osc.analog", { 640.0f, 40.0f }, { { "osc.analog.shape", 1.0f } }, {} }); // saw
        graph.addNode ({ "detuneConst", "util.constant", { 340.0f, 460.0f }, { { "util.constant.value", 0.07f } }, {} });
        graph.addNode ({ "detuneSum", "math.add", { 640.0f, 460.0f }, {}, {} });
        graph.addNode ({ "osc2", "osc.analog", { 940.0f, 460.0f }, { { "osc.analog.shape", 1.0f } }, {} }); // saw, detuned
        graph.addNode ({ "oscMix", "mix.sum", { 1240.0f, 250.0f }, {}, {} });

        graph.addNode ({ "filterEnv", "env.adsr", { 640.0f, 640.0f },
                          { { "env.adsr.attack", 0.005f }, { "env.adsr.decay", 0.3f },
                            { "env.adsr.sustain", 0.3f }, { "env.adsr.release", 0.3f } }, {} });
        graph.addNode ({ "baseCutoff", "util.constant", { 940.0f, 720.0f }, { { "util.constant.value", 300.0f } }, {} });
        graph.addNode ({ "cutoffMap", "adapt.map", { 940.0f, 850.0f },
                          { { "adapt.map.min", 0.0f }, { "adapt.map.max", 5000.0f } }, {} });
        graph.addNode ({ "cutoffSum", "math.add", { 1240.0f, 780.0f }, {}, {} });

        graph.addNode ({ "ladder", "filter.ladder", { 1540.0f, 250.0f },
                          { { "filter.ladder.resonance", 0.25f }, { "filter.ladder.keyTrack", 0.3f } }, {} });

        graph.addNode ({ "ampEnv", "env.adsr", { 1540.0f, 640.0f },
                          { { "env.adsr.attack", 0.005f }, { "env.adsr.decay", 0.15f },
                            { "env.adsr.sustain", 0.8f }, { "env.adsr.release", 0.3f } }, {} });
        graph.addNode ({ "ampVCA", "mix.gain", { 1840.0f, 250.0f }, {}, {} });

        graph.addNode ({ "voiceMix", "instance.mix", { 2140.0f, 250.0f }, {}, {} });
        // Milestone 0.2 (wiki/NODES.System.md §9): a real stereo signal path
        // at last, closing archive_docs/CLEANUP.md P1 #6's long-logged gap —
        // centered/full-width by default (law defaults to constant power,
        // pan/width both default to their own identity values), so this is
        // audibly identical to the old mono-duplicated output until the pan
        // or width is actually moved.
        graph.addNode ({ "pan", "space.pan", { 2340.0f, 250.0f }, {}, {} });
        graph.addNode ({ "masterOut", "io.output", { 2540.0f, 250.0f }, {}, {} });

        graph.addConnection ({ "noteIn", "notes", "allocator", "spawn" });

        graph.addConnection ({ "allocator", "pitch", "osc1", "pitch" });
        graph.addConnection ({ "allocator", "pitch", "detuneSum", "in.0" });
        graph.addConnection ({ "detuneConst", "out", "detuneSum", "in.1" });
        graph.addConnection ({ "detuneSum", "out", "osc2", "pitch" });
        graph.addConnection ({ "osc1", "out", "oscMix", "in.0" });
        graph.addConnection ({ "osc2", "out", "oscMix", "in.1" });
        graph.addConnection ({ "oscMix", "out", "ladder", "in" });

        graph.addConnection ({ "allocator", "gate", "filterEnv", "gate" });
        graph.addConnection ({ "filterEnv", "out", "cutoffMap", "in" });
        graph.addConnection ({ "baseCutoff", "out", "cutoffSum", "in.0" });
        graph.addConnection ({ "cutoffMap", "out", "cutoffSum", "in.1" });
        graph.addConnection ({ "cutoffSum", "out", "ladder", "filter.ladder.cutoff" });
        graph.addConnection ({ "allocator", "pitch", "ladder", "filter.ladder.keyPitch" });

        graph.addConnection ({ "ladder", "out", "ampVCA", "audio" });
        graph.addConnection ({ "allocator", "gate", "ampEnv", "gate" });
        graph.addConnection ({ "ampEnv", "out", "ampVCA", "gain" });

        graph.addConnection ({ "ampVCA", "out", "voiceMix", "in" });
        graph.addConnection ({ "voiceMix", "out", "pan", "in" });
        graph.addConnection ({ "pan", "out", "masterOut", "in" }); // one real stereo cable (wiki/NODES.System.md §9)

        graph.setOutput ("masterOut", "out");

        return graph;
    }

    /** The current `GraphEditController` constructor default (0.x arc,
        2026-09-29 — replacing `buildInitPatchGraph()` there, on the user's
        own explicit instruction: a fresh instance should open on an empty
        canvas with just a real output to build onto, not a fully-wired
        subtractive synth in the way). A single `io.output` node, wired to
        nothing — the minimum graph `NodeGraph::setOutput()` needs a real
        output port to point at (see `io.output`'s own doc comment on why
        that port is compiler-only, never a wireable glyph in the editor).
        Silent until the user patches something into it, deliberately.
        `buildInitPatchGraph()` itself is untouched and stays registered and
        tested (`tests/InitPatchTests.cpp`, engine-level) — it's just no
        longer anyone's default, the same relationship `buildVoiceProofGraph()`
        already has below since M22.
    */
    inline NodeGraph buildMasterOutOnlyGraph()
    {
        NodeGraph graph;

        graph.addNode ({ "masterOut", "io.output", { 640.0f, 360.0f }, {}, {} });
        graph.setOutput ("masterOut", "out");

        return graph;
    }

    /** ARCHITECTURE.md §3.4's example voice path, M18-rewired (ADR-0024):
        MIDI -> io.noteIn -> instance.allocate.voice -> PolyBLEP osc (pitch) ->
        SVF -> ADSR-gated amp (gate) -> out. Purely acyclic — every node
        schedules as an ordinary block-rate step. `instance.allocate.voice`'s
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
        graph.addNode ({ "allocator", "instance.allocate.voice", { 340.0f, 40.0f }, {}, {} });
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
