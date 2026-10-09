#pragma once

#include "bazalt/engine/graph/NodeGraph.h"
#include "bazalt/engine/graph/NodeFactory.h"
#include "bazalt/engine/nodes/SvfFilterNode.h"
#include "bazalt/engine/nodes/NoiseBurstNode.h"
#include "bazalt/engine/nodes/DelayNode.h"
#include "bazalt/engine/nodes/OnePoleFilterNode.h"
#include "bazalt/engine/nodes/ConstantNode.h"
#include "bazalt/engine/nodes/MacroNode.h"
#include "bazalt/engine/nodes/RerouteNode.h"
#include "bazalt/engine/nodes/MapNode.h"
#include "bazalt/engine/nodes/AddNode.h"
#include "bazalt/engine/nodes/MultiplyNode.h"
#include "bazalt/engine/nodes/RoundNode.h"
#include "bazalt/engine/nodes/ListenNode.h"
#include "bazalt/engine/nodes/ViewNodes.h"
#include "bazalt/engine/nodes/ViewCycleNode.h"
#include "bazalt/engine/nodes/ViewRippleNode.h"
#include "bazalt/engine/nodes/ViewCountNode.h"
#include "bazalt/engine/nodes/ViewScopeNode.h"
#include "bazalt/engine/nodes/OutputNode.h"
#include "bazalt/engine/nodes/ThresholdNode.h"
#include "bazalt/engine/nodes/PitchFrequencyNodes.h"
#include "bazalt/engine/nodes/GateLengthNode.h"
#include "bazalt/engine/nodes/DownmixNode.h"
#include "bazalt/engine/nodes/InstanceVoiceNode.h"
#include "bazalt/engine/nodes/InstanceSwarmPopulationNode.h"
#include "bazalt/engine/nodes/InstanceSwarmTransientNode.h"
#include "bazalt/engine/nodes/InstanceTriggerNode.h"
#include "bazalt/engine/nodes/InstanceMixNode.h"
#include "bazalt/engine/nodes/IoNoteInNode.h"
#include "bazalt/engine/nodes/SubtractNode.h"
#include "bazalt/engine/nodes/DivideNode.h"
#include "bazalt/engine/nodes/AbsNode.h"
#include "bazalt/engine/nodes/MinMaxNode.h"
#include "bazalt/engine/nodes/PowerNode.h"
#include "bazalt/engine/nodes/ModuloNode.h"
#include "bazalt/engine/nodes/SlewNode.h"
#include "bazalt/engine/nodes/BlendNode.h"
#include "bazalt/engine/nodes/LogicNotNode.h"
#include "bazalt/engine/nodes/LogicToggleNode.h"
#include "bazalt/engine/nodes/LogicGateNodes.h"
#include "bazalt/engine/nodes/LogicEventNodes.h"
#include "bazalt/engine/nodes/LogicSwitchNode.h"
#include "bazalt/engine/nodes/LogicCompareNode.h"
#include "bazalt/engine/nodes/SampleHoldNode.h"
#include "bazalt/engine/nodes/IoAudioInNode.h"
#include "bazalt/engine/nodes/IoControlNode.h"
#include "bazalt/engine/nodes/IoTransportNode.h"
#include "bazalt/engine/nodes/DcBlockNode.h"
#include "bazalt/engine/nodes/PeakFilterNode.h"
#include "bazalt/engine/nodes/ShelfFilterNode.h"
#include "bazalt/engine/nodes/AllpassFilterNode.h"
#include "bazalt/engine/nodes/LadderFilterNode.h"
#include "bazalt/engine/nodes/RandomSteppedNode.h"
#include "bazalt/engine/nodes/RandomDriftNode.h"
#include "bazalt/engine/nodes/PanNode.h"
#include "bazalt/engine/nodes/WidthNode.h"
#include "bazalt/engine/nodes/DiffuserNode.h"
#include "bazalt/engine/nodes/ReverbNode.h"
#include "bazalt/engine/nodes/NoiseColoredNode.h"
#include "bazalt/engine/nodes/NoiseDustNode.h"
#include "bazalt/engine/nodes/ShapeNodes.h"
#include "bazalt/engine/nodes/AnalysisLevelNode.h"
#include "bazalt/engine/nodes/DynamicsNodes.h"
#include "bazalt/engine/nodes/FreqShiftNode.h"
#include "bazalt/engine/nodes/ViewTuneNode.h"
#include "bazalt/engine/nodes/DecorationNodes.h"
#include "bazalt/engine/nodes/StereoSplitNode.h"
#include "bazalt/engine/nodes/StereoCombineNode.h"
#include "bazalt/engine/nodes/ClockPulseNode.h"
#include "bazalt/engine/nodes/ClockDivideNode.h"
#include "bazalt/engine/nodes/ClockCounterNode.h"
#include "bazalt/engine/nodes/SeqStepsNode.h"
#include "bazalt/engine/nodes/SeqEuclidNode.h"
#include "bazalt/engine/nodes/DataScaleNode.h"
#include "bazalt/engine/nodes/DataCurveNode.h"
#include "bazalt/engine/nodes/DataWavetableNode.h"
#include "bazalt/engine/nodes/SineOscillatorNode.h"
#include "bazalt/engine/nodes/CurvePlayerNode.h"
#include "bazalt/engine/nodes/DataLookupNode.h"
#include "bazalt/engine/nodes/NoteGateNode.h"
#include "bazalt/engine/nodes/NoteValueNode.h"
#include "bazalt/engine/nodes/NoteTransposeNode.h"
#include "bazalt/engine/nodes/NoteFilterNode.h"
#include "bazalt/engine/nodes/NoteHumanizeNode.h"
#include "bazalt/engine/nodes/NoteAssembleNode.h"
#include "bazalt/engine/nodes/NoteQuantizeNode.h"
#include "bazalt/engine/nodes/ExciteImpulseNode.h"
#include "bazalt/engine/nodes/ResonatorCombNode.h"
#include "bazalt/engine/nodes/DataMaterialNode.h"
#include "bazalt/engine/nodes/ResonatorModalNode.h"
#include "bazalt/engine/nodes/ExcitePluckNode.h"
#include "bazalt/engine/nodes/ResonatorStringNode.h"
#include "bazalt/engine/nodes/ExciteMalletNode.h"
#include "bazalt/engine/nodes/ResonatorPlateNode.h"
#include "bazalt/engine/nodes/ShapeClipNode.h"

namespace bazalt::engine
{
    /** Registers every M1/M2 DSP node type, the M7 utility node types
        (NODE_EDITOR.md §2), M16's adapter nodes, and M17's
        instance.allocate.voice/instance.sum (superseding util.voiceSum) under
        their stable type ids. Shared by render-cli and the Catch2 suite
        so the two hardcoded proof graphs below only need to be built once.
    */
    inline NodeFactory buildDefaultNodeFactory()
    {
        NodeFactory factory;
        factory.registerType ("filter.svf", [] { return std::make_unique<nodes::SvfFilterNode>(); });
        factory.registerType ("excite.burst", [] { return std::make_unique<nodes::NoiseBurstNode>(); });
        factory.registerType ("time.delay", [] { return std::make_unique<nodes::DelayNode>(); });
        factory.registerType ("filter.onepole", [] { return std::make_unique<nodes::OnePoleFilterNode>(); });
        factory.registerType ("util.constant", [] { return std::make_unique<nodes::ConstantNode>(); });
        factory.registerType ("util.macro", [] { return std::make_unique<nodes::MacroNode>(); });
        factory.registerType ("deco.reroute", [] { return std::make_unique<nodes::RerouteNode>(); });
        factory.registerType ("math.map", [] { return std::make_unique<nodes::MapNode>(); });
        factory.registerType ("math.add", [] { return std::make_unique<nodes::AddNode>(); });
        factory.registerType ("math.multiply", [] { return std::make_unique<nodes::MultiplyNode>(); });
        factory.registerType ("math.round", [] { return std::make_unique<nodes::RoundNode>(); });
        factory.registerType ("view.listen", [] { return std::make_unique<nodes::ListenNode>(); });
        factory.registerType ("view.spectrum", [] { return std::make_unique<nodes::ViewSpectrumNode>(); });
        factory.registerType ("view.meter", [] { return std::make_unique<nodes::ViewMeterNode>(); });
        factory.registerType ("view.cycle", [] { return std::make_unique<nodes::ViewCycleNode>(); }); // phase-locked viewer, replaces view.scope/view.glance
        factory.registerType ("view.ripple", [] { return std::make_unique<nodes::ViewRippleNode>(); });
        factory.registerType ("view.count", [] { return std::make_unique<nodes::ViewCountNode>(); }); // design/Visualization/Count.png
        factory.registerType ("view.scope", [] { return std::make_unique<nodes::ViewScopeNode>(); }); // one Scope for any value (wiki/plans/DataAndWavetable.md §2)
        factory.registerType ("io.output", [] { return std::make_unique<nodes::OutputNode>(); });
        factory.registerType ("logic.threshold", [] { return std::make_unique<nodes::ThresholdNode>(); });
        // Audio -> Control Bridge (wiki/plans/AudioControlBridge.md) — the
        // mechanical adapter canConnect auto-inserts for Audio -> Control.
        // Boolean -> Control (direct feedback: "bool not being pluggable
        // into control and ints... annoying").
        // Exact Pitch<->Frequency conversion (direct feedback found the
        // generic adapt.map fallback was quietly wrong for this pair —
        // linear where the real relationship is exponential).
        factory.registerType ("math.pitchToFrequency", [] { return std::make_unique<nodes::PitchToFrequencyNode>(); });
        factory.registerType ("math.frequencyToPitch", [] { return std::make_unique<nodes::FrequencyToPitchNode>(); });
        // Trigger -> timed Boolean gate (direct feedback: "duration for the
        // note held... using 2 clocks... too complicated").
        factory.registerType ("time.gateLength", [] { return std::make_unique<nodes::GateLengthNode>(); });
        factory.registerType ("channels.downmix", [] { return std::make_unique<nodes::DownmixNode>(); });
        factory.registerType ("life.voice", [] { return std::make_unique<nodes::InstanceVoiceNode>(); }); // 09-28-InstanceAllocator.3 — renamed from "instance.allocator"; 09-29-AddMenu.1 — renamed again from "instance.voice"
        factory.registerType ("life.swarmPopulation", [] { return std::make_unique<nodes::InstanceSwarmPopulationNode>(); }); // Domain Extensions batch
        factory.registerType ("life.swarmTransient", [] { return std::make_unique<nodes::InstanceSwarmTransientNode>(); }); // Domain Extensions batch
        factory.registerType ("life.trigger", [] { return std::make_unique<nodes::InstanceTriggerNode>(); }); // Domain Extensions batch
        factory.registerType ("life.merge", [] { return std::make_unique<nodes::InstanceMixNode>(); }); // DomainRedesign.md Batch 1b — renamed from "instance.mix" (C++ class name unchanged)
        factory.registerType ("io.noteIn", [] { return std::make_unique<nodes::IoNoteInNode>(); });
        // M21 Batch A, wave 1 — fixed-arity nodes needing no new infrastructure.
        factory.registerType ("math.subtract", [] { return std::make_unique<nodes::SubtractNode>(); });
        factory.registerType ("math.divide", [] { return std::make_unique<nodes::DivideNode>(); });
        factory.registerType ("math.abs", [] { return std::make_unique<nodes::AbsNode>(); });
        factory.registerType ("math.minmax", [] { return std::make_unique<nodes::MinMaxNode>(); });
        factory.registerType ("math.power", [] { return std::make_unique<nodes::PowerNode>(); });
        factory.registerType ("math.modulo", [] { return std::make_unique<nodes::ModuloNode>(); });
        factory.registerType ("time.slew", [] { return std::make_unique<nodes::SlewNode>(); });
        factory.registerType ("math.blend", [] { return std::make_unique<nodes::BlendNode>(); }); // replaces mix.crossfade and logic.select (D8)
        factory.registerType ("logic.not", [] { return std::make_unique<nodes::LogicNotNode>(); });
        factory.registerType ("logic.toggle", [] { return std::make_unique<nodes::LogicToggleNode>(); });
        factory.registerType ("logic.and", [] { return std::make_unique<nodes::LogicAndNode>(); });
        factory.registerType ("logic.or", [] { return std::make_unique<nodes::LogicOrNode>(); });
        factory.registerType ("logic.xor", [] { return std::make_unique<nodes::LogicXorNode>(); });
        factory.registerType ("logic.eventGroup", [] { return std::make_unique<nodes::LogicEventGroupNode>(); });
        factory.registerType ("logic.edge", [] { return std::make_unique<nodes::LogicEdgeNode>(); });
        factory.registerType ("logic.latch", [] { return std::make_unique<nodes::LogicLatchNode>(); });
        factory.registerType ("logic.switch", [] { return std::make_unique<nodes::LogicSwitchNode>(); }); // A/B comparison (D8)
        factory.registerType ("logic.compare", [] { return std::make_unique<nodes::LogicCompareNode>(); });
        factory.registerType ("time.sampleHold", [] { return std::make_unique<nodes::SampleHoldNode>(); });
        factory.registerType ("io.audioIn", [] { return std::make_unique<nodes::IoAudioInNode>(); });
        factory.registerType ("io.control", [] { return std::make_unique<nodes::IoControlNode>(); });
        factory.registerType ("io.transport", [] { return std::make_unique<nodes::IoTransportNode>(); });
        // M22 (Basic synthesis), wave 1 — cheap wins needing no new DSP primitive.
        factory.registerType ("filter.dcBlock", [] { return std::make_unique<nodes::DcBlockNode>(); });
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
        // wiki/plans/Reverb.md
        factory.registerType ("space.diffuser", [] { return std::make_unique<nodes::DiffuserNode>(); });
        factory.registerType ("space.reverb", [] { return std::make_unique<nodes::ReverbNode>(); });
        // wiki/plans/SoundPalette.md
        factory.registerType ("source.noise", [] { return std::make_unique<nodes::NoiseColoredNode>(); });
        factory.registerType ("source.dust", [] { return std::make_unique<nodes::NoiseDustNode>(); });
        factory.registerType ("shape.rectify", [] { return std::make_unique<nodes::ShapeRectifyNode>(); });
        factory.registerType ("shape.crush", [] { return std::make_unique<nodes::ShapeCrushNode>(); });
        factory.registerType ("shape.waveshaper", [] { return std::make_unique<nodes::ShapeWaveshaperNode>(); });
        factory.registerType ("shape.fold", [] { return std::make_unique<nodes::ShapeFoldNode>(); });
        factory.registerType ("analysis.level", [] { return std::make_unique<nodes::AnalysisLevelNode>(); });
        factory.registerType ("dynamics.compress", [] { return std::make_unique<nodes::DynCompressNode>(); });
        factory.registerType ("dynamics.gate", [] { return std::make_unique<nodes::DynGateNode>(); });
        factory.registerType ("spectrum.freqShift", [] { return std::make_unique<nodes::FreqShiftNode>(); });
        factory.registerType ("view.tune", [] { return std::make_unique<nodes::ViewTuneNode>(); }); // design/Visualization/Tune.png
        // wiki/plans/Decorations.md — canvas-only, never compiled
        factory.registerDecoration ("deco.header", [] { return std::make_unique<nodes::DecorationNode> ("Header", "heading"); });
        factory.registerDecoration ("deco.comment", [] { return std::make_unique<nodes::DecorationNode> ("Comment", "comment"); });
        factory.registerDecoration ("deco.box", [] { return std::make_unique<nodes::DecorationNode> ("Box", "box"); });
        factory.registerDecoration ("deco.image", [] { return std::make_unique<nodes::DecorationNode> ("Image", "image"); });
        factory.registerType ("channels.split", [] { return std::make_unique<nodes::StereoSplitNode>(); });
        factory.registerType ("channels.combine", [] { return std::make_unique<nodes::StereoCombineNode>(); });
        // Clock+Seq batch (wiki/NODES.Status.md's own build-next order, step 1).
        factory.registerType ("time.clock", [] { return std::make_unique<nodes::ClockPulseNode>(); });
        factory.registerType ("time.divide", [] { return std::make_unique<nodes::ClockDivideNode>(); });
        factory.registerType ("time.counter", [] { return std::make_unique<nodes::ClockCounterNode>(); });
        factory.registerType ("time.steps", [] { return std::make_unique<nodes::SeqStepsNode>(); });
        factory.registerType ("time.euclid", [] { return std::make_unique<nodes::SeqEuclidNode>(); });
        // Data Foundations batch — the first real Data-producing/consuming nodes.
        factory.registerType ("data.scale", [] { return std::make_unique<nodes::DataScaleNode>(); });
        factory.registerType ("source.oscillator", [] { return std::make_unique<nodes::CurveOscillatorNode>(); }); // one curve player, two entries (DataAndWavetable.md D5/D6)
        factory.registerType ("source.envelope", [] { return std::make_unique<nodes::CurveEnvelopeNode>(); });
        factory.registerType ("data.curve", [] { return std::make_unique<nodes::DataCurveNode>(); }); // the curve factory (wiki/plans/DataAndWavetable.md 1b)
        factory.registerType ("data.wavetable", [] { return std::make_unique<nodes::DataWavetableNode>(); }); // keyframed wavetable (1c)
        // The fixed classic shapes, back by direct instruction (2026-10-09).
        factory.registerType ("source.sine", [] { return std::make_unique<nodes::SineOscillatorNode>(); });
        factory.registerType ("source.saw", [] { return std::make_unique<nodes::SawOscillatorNode>(); });
        factory.registerType ("source.square", [] { return std::make_unique<nodes::SquareOscillatorNode>(); });
        factory.registerType ("source.triangle", [] { return std::make_unique<nodes::TriangleOscillatorNode>(); });
        factory.registerType ("data.lookup", [] { return std::make_unique<nodes::DataLookupNode>(); });
        // Note Stream batch — note.hold/note.select/note.chord deferred (a
        // real, documented engine limit: ExecutionPlan::BlockStep supports
        // only one Note output per node, see NoteFilterNode.h's own comment).
        factory.registerType ("note.gate", [] { return std::make_unique<nodes::NoteGateNode>(); });
        factory.registerType ("note.value", [] { return std::make_unique<nodes::NoteValueNode>(); });
        factory.registerType ("note.transpose", [] { return std::make_unique<nodes::NoteTransposeNode>(); });
        factory.registerType ("note.filter", [] { return std::make_unique<nodes::NoteFilterNode>(); });
        factory.registerType ("note.humanize", [] { return std::make_unique<nodes::NoteHumanizeNode>(); });
        // Note Stream follow-up (direct feedback: "no way to create a Note
        // from scratch") — the one node in the family that PRODUCES a Note
        // stream rather than reshaping an existing one.
        factory.registerType ("note.assemble", [] { return std::make_unique<nodes::NoteAssembleNode>(); });
        factory.registerType ("note.quantize", [] { return std::make_unique<nodes::NoteQuantizeNode>(); });
        // PM Core batch (wiki/NODES.Status.md's own build-next order, step 6) —
        // the first real physical-modelling nodes.
        factory.registerType ("excite.impulse", [] { return std::make_unique<nodes::ExciteImpulseNode>(); });
        factory.registerType ("resonator.comb", [] { return std::make_unique<nodes::ResonatorCombNode>(); });
        // PM Core batch 2 — the Data(modal-set) producer/consumer pair.
        factory.registerType ("data.material", [] { return std::make_unique<nodes::DataMaterialNode>(); });
        factory.registerType ("resonator.modal", [] { return std::make_unique<nodes::ResonatorModalNode>(); });
        // PM Core batch 3 — the flagship: excite.pluck (the one-node shortcut
        // into resonator.string) + resonator.string (the playable Karplus-Strong).
        factory.registerType ("excite.pluck", [] { return std::make_unique<nodes::ExcitePluckNode>(); });
        factory.registerType ("resonator.string", [] { return std::make_unique<nodes::ResonatorStringNode>(); });
        // PM Core batch 4 — closes the batch: excite.mallet (the first real
        // production exercise of a cross-node per-sample feedback cycle,
        // via resonator.string's own "motion" output) + resonator.plate.
        factory.registerType ("excite.mallet", [] { return std::make_unique<nodes::ExciteMalletNode>(); });
        factory.registerType ("resonator.plate", [] { return std::make_unique<nodes::ResonatorPlateNode>(); });
        // Direct feedback: a real, wireable, mid-chain safety limiter -
        // the catalog's own "the node you put in a feedback loop so a
        // slider can't destroy a speaker," complementing (not replacing)
        // the plugin's own always-on master-output OutputLimiter.
        factory.registerType ("shape.clip", [] { return std::make_unique<nodes::ShapeClipNode>(); });
        // wiki/plans/PropsAndMacroRedesign.md Batch D: explicit, manual-
        // placement-only converters replacing the three nodes' old
        // Unipolar/Bipolar selector parameters - never auto-inserted by
        // connectWithAutoAdapt (adapt.map's own generic fallback already
        // covers a Unipolar<->Bipolar quantity mismatch).
        return factory;
    }

    /** M22 — the Init Patch (NODE_CATALOG.md's Part B one-liner: `io.noteIn`
        `-> instance.allocate.voice -> osc.analog x2 -> filter.ladder -> env.adsr
        -> instance.sum -> space.reverb`), built for real and made
        `GraphEditController`'s constructor default (replacing
        `buildVoiceProofGraph()` there — that graph stays registered and
        tested, just no longer what a fresh instance opens with). Two
        detuned saw oscillators through a resonant ladder filter with its
        OWN envelope (so the tone brightens and settles independently of the
        amp envelope — what actually makes a subtractive synth feel alive,
        not just "does it compile"), key-tracked so higher notes stay
        proportionally bright, into a VCA, into `life.merge` — the first
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
    /** A node carrying a curve as its content (a source.oscillator, a
        source.envelope, a data.curve). */
    inline NodeInstance withContent (NodeInstance node, const CurveDocument& curve)
    {
        node.content = curve.toVar();
        return node;
    }

    inline NodeGraph buildInitPatchGraph()
    {
        NodeGraph graph;

        graph.addNode ({ "noteIn", "io.noteIn", { 40.0f, 260.0f }, {}, {} });
        graph.addNode ({ "allocator", "life.voice", { 340.0f, 260.0f }, {}, {} });

        graph.addNode ({ "pitch1", "math.pitchToFrequency", { 640.0f, 40.0f }, {}, {} });
        graph.addNode (withContent ({ "osc1", "source.oscillator", { 940.0f, 40.0f }, {}, {} }, CurveDocument::saw()));
        graph.addNode ({ "detuneConst", "util.constant", { 340.0f, 460.0f }, { { "util.constant.value", 0.07f } }, {} });
        graph.addNode ({ "detuneSum", "math.add", { 640.0f, 460.0f }, {}, {} });
        graph.addNode ({ "pitch2", "math.pitchToFrequency", { 790.0f, 460.0f }, {}, {} });
        graph.addNode (withContent ({ "osc2", "source.oscillator", { 940.0f, 460.0f }, {}, {} }, CurveDocument::saw())); // detuned
        graph.addNode ({ "oscMix", "math.add", { 1240.0f, 250.0f }, {}, {} });

        graph.addNode (withContent ({ "filterEnv", "source.envelope", { 640.0f, 640.0f }, {}, {} },
                                    CurveDocument::adsr (0.005f, 0.3f, 0.3f, 0.3f, 0.0f)));
        graph.addNode ({ "baseCutoff", "util.constant", { 940.0f, 720.0f }, { { "util.constant.value", 300.0f } }, {} });
        graph.addNode ({ "cutoffMap", "math.map", { 940.0f, 850.0f },
                          { { "math.map.min", 0.0f }, { "math.map.max", 5000.0f } }, {} });
        graph.addNode ({ "cutoffSum", "math.add", { 1240.0f, 780.0f }, {}, {} });

        graph.addNode ({ "ladder", "filter.ladder", { 1540.0f, 250.0f },
                          { { "filter.ladder.resonance", 0.25f }, { "filter.ladder.keyTrack", 0.3f } }, {} });

        graph.addNode (withContent ({ "ampEnv", "source.envelope", { 1540.0f, 640.0f }, {}, {} },
                                    CurveDocument::adsr (0.005f, 0.15f, 0.8f, 0.3f, 0.0f)));
        graph.addNode ({ "ampVCA", "math.multiply", { 1840.0f, 250.0f }, {}, {} });

        graph.addNode ({ "voiceMix", "life.merge", { 2140.0f, 250.0f }, {}, {} });
        // Milestone 0.2 (wiki/NODES.System.md §9): a real stereo signal path
        // at last, closing archive_docs/CLEANUP.md P1 #6's long-logged gap —
        // centered/full-width by default (law defaults to constant power,
        // pan/width both default to their own identity values), so this is
        // audibly identical to the old mono-duplicated output until the pan
        // or width is actually moved.
        graph.addNode ({ "pan", "space.pan", { 2340.0f, 250.0f }, {}, {} });
        graph.addNode ({ "masterOut", "io.output", { 2540.0f, 250.0f }, {}, {} });

        graph.addConnection ({ "noteIn", "notes", "allocator", "spawn" });

        graph.addConnection ({ "allocator", "pitch", "pitch1", "pitch" });
        graph.addConnection ({ "pitch1", "frequency", "osc1", "source.oscillator.frequency" });
        graph.addConnection ({ "allocator", "pitch", "detuneSum", "in.0" });
        graph.addConnection ({ "detuneConst", "out", "detuneSum", "in.1" });
        graph.addConnection ({ "detuneSum", "out", "pitch2", "pitch" });
        graph.addConnection ({ "pitch2", "frequency", "osc2", "source.oscillator.frequency" });
        graph.addConnection ({ "osc1", "out", "oscMix", "in.0" });
        graph.addConnection ({ "osc2", "out", "oscMix", "in.1" });
        graph.addConnection ({ "oscMix", "out", "ladder", "in" });

        graph.addConnection ({ "allocator", "gate", "filterEnv", "gate" });
        graph.addConnection ({ "filterEnv", "out", "cutoffMap", "in" });
        graph.addConnection ({ "baseCutoff", "out", "cutoffSum", "in.0" });
        graph.addConnection ({ "cutoffMap", "out", "cutoffSum", "in.1" });
        graph.addConnection ({ "cutoffSum", "out", "ladder", "filter.ladder.cutoff" });
        graph.addConnection ({ "allocator", "pitch", "ladder", "filter.ladder.keyPitch" });

        graph.addConnection ({ "ladder", "out", "ampVCA", "in.0" });
        graph.addConnection ({ "allocator", "gate", "ampEnv", "gate" });
        graph.addConnection ({ "ampEnv", "out", "ampVCA", "in.1" });

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
        schedules as an ordinary block-rate step. `life.voice`'s
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
        graph.addNode ({ "allocator", "life.voice", { 340.0f, 40.0f }, {}, {} });
        // juce::ADSR's own defaults — what env.adsr played here before 1b.
        graph.addNode (withContent ({ "env", "source.envelope", { 640.0f, 40.0f }, {}, {} }, CurveDocument::adsr (0.1f, 0.1f, 1.0f, 0.1f, 0.0f)));
        graph.addNode ({ "pitchToFreq", "math.pitchToFrequency", { 190.0f, 420.0f }, {}, {} });
        graph.addNode (withContent ({ "osc", "source.oscillator", { 340.0f, 420.0f }, {}, {} }, CurveDocument::saw()));
        graph.addNode ({ "svf", "filter.svf", { 640.0f, 420.0f }, { { "filter.svf.cutoff", 3000.0f }, { "filter.svf.resonance", 0.9f } }, {} });
        graph.addNode ({ "amp", "math.multiply", { 940.0f, 230.0f }, {}, {} });

        graph.addConnection ({ "noteIn", "notes", "allocator", "spawn" });
        graph.addConnection ({ "allocator", "pitch", "pitchToFreq", "pitch" });
        graph.addConnection ({ "pitchToFreq", "frequency", "osc", "source.oscillator.frequency" });
        graph.addConnection ({ "allocator", "gate", "env", "gate" });
        graph.addConnection ({ "osc", "out", "svf", "in" });
        graph.addConnection ({ "svf", "out", "amp", "in.0" });
        graph.addConnection ({ "env", "out", "amp", "in.1" });

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
        graph.addNode ({ "mix", "math.add", {}, {}, {} });
        graph.addNode ({ "delay", "time.delay", {}, { { "time.delay.samples", 200.0f } }, {} });
        graph.addNode ({ "damp", "filter.onepole", {}, { { "filter.onepole.coefficient", 0.5f } }, {} });

        graph.addConnection ({ "excite", "out", "mix", "in.0" });
        graph.addConnection ({ "damp", "out", "mix", "in.1" });
        graph.addConnection ({ "mix", "out", "delay", "in" });
        graph.addConnection ({ "delay", "out", "damp", "in" });

        graph.setOutput ("damp", "out");

        return graph;
    }
}
