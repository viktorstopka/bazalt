#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include "bazalt/engine/graph/ExecutionPlan.h"
#include "bazalt/engine/graph/PlanSwapper.h"
#include "bazalt/engine/graph/PreviewDescriptor.h"
#include "bazalt/engine/graph/VoiceManager.h"
#include "bazalt/engine/graph/MultiplicityResolver.h"
#include "bazalt/engine/graph/NodeFactory.h"
#include "bazalt/engine/patch/PatchDocument.h"
#include "bazalt/engine/NanGuard.h"
#include "bazalt/engine/OutputLimiter.h"
#include "bazalt/engine/telemetry/TelemetryHub.h"
#include "bazalt/engine/telemetry/AnalysisThread.h"
#include "MacroParameters.h"
#include "GraphEditController.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace bazalt::engine::nodes { class IoNoteInNode; class InstanceOriginNode; }

namespace bazalt
{
    /** M3: real bus layout (main stereo in/out + 4 aux stereo sidechains),
        the hardcoded voice graph (ARCHITECTURE.md §3.4) driven by actual
        MIDI input through a fixed VoiceManager-backed pool, the macro
        parameter pool, and patch save/load. Plugin state IS the patch
        (§4.4) — getStateInformation/setStateInformation are thin wrappers
        around PatchSerializer.

        wiki/plans/DomainRedesign.md Batch 2: up to `maxOrigins` simultaneous
        "instance.allocate.voice" origins, each with its own independent
        VoiceManager and 8 PlanSwappers (`OriginBundle`) — the runtime shape
        is still N independent physical ExecutionPlans per origin (§10.1's
        own key decision: a compile-time reclassification rewrite, not an
        ExecutionPlan-internals one), just no longer assumed singular.

        Note routes through real compiled-graph ports as of M18 (ADR-0024,
        see CLAUDE.md's "known interim simplifications" for the full
        mechanism) — this was true only through M3-M17, when MIDI was
        translated directly into setParameter()/noteOn()/noteOff() calls on
        each voice's compiled node instances with no Note-typed port
        involved at all; this comment was never updated when that changed.
        `handleMidiEvent` now makes exactly one direct poke
        (`IoNoteInNode::injectNoteOn`/`injectNoteOff`/`injectPitchBend` on
        each voice's own `io.noteIn` node, found by type via `findNoteIn`,
        not by a hardcoded id), and everything downstream of that is
        ordinary graph wiring through a real `Note`-typed port/buffer.
    */
    class BazaltAudioProcessor final : public juce::AudioProcessor,
                                        private juce::Timer
    {
    public:
        static constexpr int numVoices = 8;
        static constexpr int numAuxBuses = 4;

        /** wiki/plans/DomainRedesign.md's own small, fixed ceiling — mirrors
            MultiplicityResolver::maxOrigins exactly (this alias exists so
            plugin-layer code never has to spell out the engine namespace
            just to size an array).
        */
        static constexpr int maxOrigins = bazalt::engine::MultiplicityResolver::maxOrigins;

        /** One "instance.allocate.voice" origin's own runtime state — a
            fully independent VoiceManager + 8 PlanSwappers, exactly what a
            single-origin graph already had, just no longer assumed to be
            the only one. `originNodeId` is this bundle's current origin's
            own (stable, hand-assigned) node id; empty when `active` is
            false. Slot assignment (which numbered bundle a given origin id
            occupies) is decided by GraphEditController and kept STABLE
            across an edit that doesn't remove the origin — the same
            "state survives a recompile" property PlanSwapper's own
            per-node reuse already gives a single voice, now given to an
            origin's whole VoiceManager too (which voices are Active/Idle
            is real, valuable state that a plain graph edit — moving a
            node, tweaking a parameter — must not reset).

            This is a DIFFERENT, deliberately similarly-named struct from
            the engine-level `MultiplicityOrigin` (MultiplicityResolver.h,
            NodeGraph-shaped) — don't confuse the two.
        */
        struct OriginBundle
        {
            bazalt::engine::VoiceManager voiceManager;
            std::array<bazalt::engine::PlanSwapper, numVoices> voicePlanSwappers;
            juce::String originNodeId;
            bool active = false;

            // This origin's own per-block voice sum — DomainRedesign.md §4's
            // multiple independent voice regions means every active origin
            // needs its OWN accumulator, not one shared processor-level
            // buffer. Sized once in prepareToPlay() for every bundle,
            // regardless of whether it's active yet (so activating one
            // mid-session never needs an audio-thread allocation).
            juce::AudioBuffer<float> instanceMixScratchBuffer;

            // For this origin's own instance.sum "average" mode — was a
            // single processor-level field before Batch 2; each origin's
            // own reduction is independent now.
            int activeVoiceCountThisBlock = 0;

            /** Domain Extensions batch: -1 (default) means "an ordinary,
                demand-driven origin — VoiceManager's own Idle/Active/
                Releasing/Stealing stage machine decides what renders, same
                as always." >= 0 means "an instance.allocate.swarmPopulation
                origin — ignore VoiceManager's stage entirely for this
                bundle; slots [0, this value) always render, unconditionally,
                every block, with no noteOn()/noteOff() ever called at all
                (swarm-population has no spawn/release concept — 'fixed
                count, always live')." Written once per recompile, message
                thread (GraphEditController, mirroring setOriginMaxVoices's
                own exact pattern); read every block, audio thread
                (renderOriginVoiceRange) — atomic for that reason, same as
                every other cross-thread origin-bundle flag here.
            */
            std::atomic<int> swarmPopulationSize { -1 };
        };

        BazaltAudioProcessor();
        ~BazaltAudioProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override;

        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        // Thin, host-independent hooks used by PluginTests (which links
        // this processor directly, no host needed) — not part of the
        // AudioProcessor API surface.
        bazalt::engine::PatchDocument getCurrentPatchDocument() const;
        juce::String getStateAsJson() const;
        bool loadStateFromJson (const juce::String& json);
        float getAuxPeakLevel (int auxIndex) const noexcept;

        /** The editor's WebView resource provider reads published frames
            through this — ARCHITECTURE.md §6.3's pull-based transport.
        */
        bazalt::engine::TelemetryHub& getTelemetryHub() noexcept { return telemetryHub; }

        /** M5: lets the editor bind a macro to a WebSliderRelay without
            reaching into MacroParameters' storage directly. Index is
            0-based (macro 1 is index 0).
        */
        juce::AudioParameterFloat& getMacroParameter (int macroIndex) noexcept { return macroParameters.getParameter (macroIndex); }

        /** wiki/plans/UtilMacro.md: GraphEditController::recompileAndPublish()
            calls this after every successful compile, with the mappings it
            just derived by scanning the live graph for util.macro nodes and
            their claimed slots (never hand-curated, never persisted - see
            PatchDocument's dropped macroMappings field, schema v7). Replaces
            the old setDefaultMacroMappings()'s fixed 4-entry table, which
            targeted node ids ("osc"/"svf"/"env") that only ever existed in
            buildVoiceProofGraph() - dead since the master-out-only graph
            became the default. Message-thread only, same as
            recompileAndPublish() itself.
        */
        void setMacroMappings (std::vector<bazalt::engine::MacroMapping> mappings) noexcept
        {
            macroParameters.setMappings (std::move (mappings));
        }

        /** M7: the command bridge (GraphEditController, PluginEditor's
            withNativeFunction wiring) mutates the live graph through this.
            Message-thread only.
        */
        GraphEditController& getGraphEditController() noexcept { return graphEditController; }

        // The handful of accessors GraphEditController needs to compile
        // and publish plans without reaching into private members
        // directly — message-thread only, same as everything else on this
        // interface (GraphCompiler::compile allocates; never audio-thread).
        bazalt::engine::NodeFactory& getNodeFactory() noexcept { return nodeFactory; }
        bazalt::engine::PlanSwapper& getGlobalPlanSwapper() noexcept { return globalPlanSwapper; }

        /** A stable identity for THIS processor instance, generated once in
            the constructor — `PluginEditor.cpp` uses it to give each
            instance's own WebView2 a separate user-data folder
            (`%TEMP%/Bazalt/WebView2/<uuid>/`) instead of every instance
            sharing one fixed path. A real, DAW-relevant gap this closes:
            Microsoft's own WebView2 guidance is one user-data folder per
            running environment — a shared, fixed path was never exercised
            by the Standalone app (always exactly one instance) but a real
            host like Ableton routinely runs several instances of the same
            plugin at once (multiple tracks), and separately, the Standalone
            app and a DAW's own VST3 instance could easily be running
            concurrently as two unrelated OS processes pointing at the
            SAME folder — neither scenario is exotic enough to leave
            unaddressed just because nothing here has reproduced an actual
            crash from it yet.
        */
        const juce::Uuid& getInstanceId() const noexcept { return instanceId; }

        /** Testing-only escape hatch. The master-output safety limiter
            (`OutputLimiter`, `processBlock()`'s own final stage) is
            unconditionally ON for every real use — but a number of
            existing tests deliberately read a Control-rate value (a
            transport tempo, a MIDI CC's raw mapped 0..1 value, ...) back
            through the MAIN OUTPUT BUFFER as a generic numeric-inspection
            channel, a pre-existing, legitimate testing technique that
            predates the limiter and pushes values well outside the
            ordinary ±1 audio range it's designed to protect (a tempo of
            120 BPM reads back as a literal `2.0`, for instance). A real
            patch never needs this; only a test inspecting a non-audio
            value via that same buffer path does. Defaults to enabled
            (safe) — tests opt out explicitly, real use never does.
        */
        void setOutputLimiterEnabledForTesting (bool enabled) noexcept { outputLimiterEnabled = enabled; }
        void setHasGlobalDomain (bool hasIt) noexcept { hasGlobalDomain.store (hasIt, std::memory_order_release); }

        /** M21: true while the graph has no active origin at all (no
            "instance.allocate.voice", or a real instance.sum with nothing
            to reduce — MultiplicityResolver's own monoOnly/empty-origins
            cases) — the one compiled plan lives in the GLOBAL swapper and
            runs every block, voices are never allocated. Set by
            GraphEditController::recompileAndPublish(). Message-thread only.
        */
        void setMonoOnly (bool isMono) noexcept { monoOnlyGraph.store (isMono, std::memory_order_release); }

        /** DomainRedesign.md §10.1: which origin bundle's own voice-sum IS
            the final output, when hasGlobalDomain is false (the graph's
            designated output itself resolved Poly — the oldest, pre-M17
            "voice sum is final output" case, now keyed to a specific
            origin instead of assumed singular). -1 when hasGlobalDomain is
            true (the global plan's own output is what matters instead) or
            there is no active origin at all. Message-thread only to set;
            read on the audio thread by finalizeInstanceMixIntoOutput().
        */
        void setOutputOriginBundleIndex (int index) noexcept { outputOriginBundleIndex.store (index, std::memory_order_release); }

        // ---- Origin bundle slot management (GraphEditController-facing) ----
        // Message-thread only. See OriginBundle's own doc comment on why
        // slot assignment is kept stable across an edit.
        bool isOriginBundleActive (int index) const noexcept { return originBundles[(size_t) index].active; }
        const juce::String& getOriginBundleOriginId (int index) const noexcept { return originBundles[(size_t) index].originNodeId; }
        OriginBundle& getOriginBundle (int index) noexcept { return originBundles[(size_t) index]; }
        bazalt::engine::PlanSwapper& getOriginVoicePlanSwapper (int bundleIndex, int voiceIndex) noexcept
        {
            return originBundles[(size_t) bundleIndex].voicePlanSwappers[(size_t) voiceIndex];
        }

        /** Commits which origin id occupies each numbered bundle slot,
            after a full recompile has already succeeded (never before —
            CLAUDE.md rule 5's rollback contract means a rejected edit must
            leave every bundle's live state exactly as it was). A slot whose
            id is UNCHANGED from before keeps its VoiceManager untouched
            (voices mid-note survive the edit, same principle as
            GraphCompiler's own per-node DSP-state reuse); a slot whose id
            CHANGED (a different origin now occupies it) gets a freshly
            prepared VoiceManager; an empty id deactivates that slot.
        */
        void commitOriginBundleAssignments (const std::array<juce::String, maxOrigins>& originIdBySlot) noexcept;

        /** wiki/plans/DomainRedesign.md Batch 4: enforces
            "instance.allocate.voice.maxInstances" for real (VoiceManager::
            setMaxActiveVoices) — called once per recompile, after this
            bundle's own voice-slot-0 plan is compiled, with whatever that
            plan's real origin node reports via `InstanceOriginNode::
            getMaxInstances()` (Domain Extensions batch — generalized past
            `InstanceVoiceNode` specifically once Swarm-transient/Trigger
            needed the same capture).
        */
        void setOriginMaxVoices (int bundleIndex, int maxVoices) noexcept
        {
            originBundles[(size_t) bundleIndex].voiceManager.setMaxActiveVoices (maxVoices);
        }

        /** Domain Extensions batch: called once per recompile for EVERY
            origin bundle slot, right alongside setOriginMaxVoices — pass
            -1 for an ordinary (Voice/event-driven) origin, or the real
            populationSize for a swarmPopulation one. See OriginBundle::
            swarmPopulationSize's own doc comment for what this actually
            changes in renderOriginVoiceRange.
        */
        void setSwarmPopulationSize (int bundleIndex, int populationSize) noexcept
        {
            originBundles[(size_t) bundleIndex].swarmPopulationSize.store (populationSize, std::memory_order_relaxed);
        }

        /** The instance-count badge's own two numbers for this origin
            bundle — message-thread safe (both are atomics on VoiceManager).
        */
        /** Domain Extensions batch: a swarmPopulation origin never calls
            VoiceManager::noteOn() at all (see renderOriginVoiceRange's own
            comment), so its activeVoiceCount/maxActiveVoices would read a
            stale 0 forever — wrong for the UI's instance-count badge, which
            should show a healthy swarm as fully populated (e.g. "8/8"), not
            "0/anything". Reports populationSize for both halves instead
            whenever this bundle is one — active == max == populationSize,
            by definition, for an "always live" origin.
        */
        int getOriginActiveVoiceCount (int bundleIndex) const noexcept
        {
            const auto& bundle = originBundles[(size_t) bundleIndex];
            if (const auto populationSize = bundle.swarmPopulationSize.load (std::memory_order_relaxed); populationSize >= 0)
                return juce::jmin (populationSize, numVoices);
            return bundle.voiceManager.getActiveVoiceCount();
        }
        int getOriginMaxVoices (int bundleIndex) const noexcept
        {
            const auto& bundle = originBundles[(size_t) bundleIndex];
            if (const auto populationSize = bundle.swarmPopulationSize.load (std::memory_order_relaxed); populationSize >= 0)
                return juce::jmin (populationSize, numVoices);
            return bundle.voiceManager.getMaxActiveVoices();
        }

        /** M20: subscribes a visualization tap for a real node's output
            port, resolving whether it lives in the global domain (one
            plan) or SOME origin's own voice domain (8 independent plans
            per active origin, of which only that origin's most recently
            triggered voice is switched on — ExecutionPlan::previewTapsEnabled).
            Returns false only if no currently-compiled plan has a node with
            this port (input or output) at all. A port that exists but has
            nothing to tap yet - an unwired input on a view node, or an
            output buried inside a per-sample region (see
            ExecutionPlan::outputBufferIndexByNodeAndPort's comment) - is
            accepted as PENDING: the hub tap is claimed, nothing is pushed
            into it, and it starts the moment a recompile resolves the port.
            Message-thread only.

            ADR-0029: the subscription is remembered, and
            applyPreviewSubscriptions() re-attaches it to every freshly
            compiled plan, so it survives graph edits (a tap pointer lives on
            a plan, and every edit builds new plans). A port on a view node
            resolves to the buffer wired into it.
        */
        bool subscribeVisualizationTap (const juce::String& nodeId, const juce::String& portId, bazalt::engine::PreviewKind kind);
        void unsubscribeVisualizationTap (const juce::String& nodeId, const juce::String& portId);

        /** ADR-0029: called by GraphEditController just BEFORE it publishes
            freshly compiled plans (message thread). Re-attaches every live
            subscription to them and points each active origin's voice taps
            at that origin's own current voice. The plans aren't visible to
            the audio thread yet, so there is nothing to race with.
            `voicePlans` holds every ACTIVE origin's 8 plans concatenated (in
            bundle-slot order), or is empty for a mono graph; `globalPlan`
            may be null. A subscription that no longer resolves (its node
            was deleted, say) is kept and simply doesn't attach - the UI
            unsubscribes when its preview unmounts.
        */
        void applyPreviewSubscriptions (const std::vector<bazalt::engine::ExecutionPlan*>& voicePlans,
                                        bazalt::engine::ExecutionPlan* globalPlan);

    private:
        static BusesProperties makeBusLayout();

        using VoicePlanPtrs = std::array<bazalt::engine::ExecutionPlan*, numVoices>;

        void handleMidiEvent (const juce::MidiMessage& message, const std::array<VoicePlanPtrs, maxOrigins>& originVoicePlanPtrs);

        // M21 host boundary (engine/graph/HostInputs.h): what io.audioIn, io.control
        // and io.transport read. All audio-thread only, allocation-free.
        void captureHostInput (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;
        void beginTransportForBlock (int numSamples) noexcept;
        void prepareHostInputsForRange (int startSample) noexcept;
        void processPlanRange (bazalt::engine::ExecutionPlan* plan, int startSample, int numSamples) noexcept;
        void renderMonoRange (bazalt::engine::ExecutionPlan* plan, int startSample, int numSamples) noexcept;
        /** Switches preview taps on for exactly one voice plan of `bundle`
            (the one most recently triggered, or voice 0 before any note)
            and off for the rest. Cheap enough to run every block and on
            every note-on, which also heals the rare mismatch a note-on can
            cause mid-publish. */
        void pointVoiceTapsAtCurrentVoice (OriginBundle& bundle, const VoicePlanPtrs& voicePlans) noexcept;
        void renderOriginVoiceRange (OriginBundle& bundle, int startSample, int numSamples, const VoicePlanPtrs& voicePlans) noexcept;
        void triggerVoiceNote (bazalt::engine::ExecutionPlan* plan, float pitch, float velocity) noexcept;

        /** The voice's io.noteIn node, or nullptr. Audio-thread safe: it looks
            the node up with a String built once at construction. Passing a
            literal to ExecutionPlan::getNodeById instead builds a heap-
            allocated juce::String on every call (an M18 bug the RT trap caught
            in M21). */
        bazalt::engine::nodes::IoNoteInNode* findNoteIn (bazalt::engine::ExecutionPlan* plan) const noexcept;

        /** The origin's own allocator node, found by this bundle's own
            `originNodeId` (a member String, never a fresh literal — same
            RT-safety rule findNoteIn's own comment already states).
            DomainRedesign.md Batch 2's MIDI-independence mechanism reads
            gate/pitch/velocity off this directly, and pokes a DIFFERENT
            voice's own copy directly (bypassing io.noteIn, which this
            origin may not even have) when an internal trigger needs to
            move to a voice slot other than the one that detected it.

            Returns `InstanceOriginNode*`, not a concrete node type — Domain
            Extensions batch generalization, so this one dispatch site (and
            `renderOriginVoiceRange`'s internal-trigger-relay block below,
            the only caller) serves Voice, Swarm-transient, and Trigger
            alike with no branching on concrete type. Real MIDI dispatch
            (`handleMidiEvent`/`triggerVoiceNote`) stays untouched and never
            calls this at all — it only ever pokes `io.noteIn` directly, by
            design (MIDI only ever drives Voice).
        */
        bazalt::engine::nodes::InstanceOriginNode* findAllocatorNode (bazalt::engine::ExecutionPlan* plan, const juce::String& originNodeId) const noexcept;
        void triggerVoiceNoteViaAllocator (bazalt::engine::ExecutionPlan* plan, const juce::String& originNodeId, float pitch, float velocity) noexcept;

        // wiki/plans/UtilMacro.md Finding A: globalPlan is fetched exactly
        // once per process() call, in processBlock, alongside every other
        // plan fetch (PlanSwapper.h's own audio-thread contract) - never
        // re-fetched in here. nullptr whenever hasGlobalDomain is false (the
        // designated output resolved Poly, or this block ran monoOnly).
        void finalizeInstanceMixIntoOutput (juce::AudioBuffer<float>& output, int numSamples,
                                             bazalt::engine::ExecutionPlan* globalPlan) noexcept;
        void updateAuxLevelsAndPassthrough (juce::AudioBuffer<float>& mainOutput, int numSamples);

        // juce::Timer — periodic (~50ms, ARCHITECTURE.md §3.2) reclaim() on
        // every PlanSwapper below, message-thread only. engine/ has no
        // Timer (headless by design, CLAUDE.md rule 4); the plugin layer
        // owns scheduling it, exactly as PlanSwapper.h's own header comment
        // says it must.
        void timerCallback() override;

        bazalt::engine::NodeFactory nodeFactory;
        const juce::Uuid instanceId; // see getInstanceId()'s own doc comment

        // TEMPORARY diagnostic (2026-10-03) — real-host audio glitch
        // investigation ("retriggered constantly" / heavy delay, reported
        // only under Ableton, never reproduced under the Standalone app).
        // Logs every prepareToPlay() call (message thread — a real file
        // write there is fine) and a periodic processBlock() call-rate
        // summary (the counter itself is just an atomic increment, audio-
        // thread-safe; the actual file write happens on the EXISTING 50ms
        // timerCallback(), never on the audio thread). Remove once the
        // root cause is confirmed and fixed — not meant to ship.
        void logDiagnostic (const juce::String& line) const;
        int prepareToPlayCallCount = 0;
        std::atomic<uint64_t> processBlockCallCount { 0 };
        uint64_t lastLoggedProcessBlockCallCount = 0;

        // wiki/plans/DomainRedesign.md Batch 2: up to maxOrigins independent
        // origin bundles, replacing the single voiceManager + 8
        // voicePlanSwappers a graph used to be limited to. Plus one global
        // plan swapper, used whenever the graph has any real global content
        // (DomainRedesign.md §10.2 step 6 — supersedes the original
        // util.voiceSum boundary); hasGlobalDomain is read on the audio
        // thread, so it's atomic despite being set only from the message
        // thread.
        std::array<OriginBundle, maxOrigins> originBundles;
        bazalt::engine::PlanSwapper globalPlanSwapper;
        std::atomic<bool> hasGlobalDomain { false };
        std::atomic<bool> monoOnlyGraph { false };
        std::atomic<int> outputOriginBundleIndex { -1 };

        MacroParameters macroParameters;
        bazalt::engine::NanGuard outputGuard;
        bazalt::engine::OutputLimiter outputLimiter; // real safety ceiling — see OutputLimiter.h's own doc comment
        bool outputLimiterEnabled = true; // see setOutputLimiterEnabledForTesting()'s own doc comment

        // M17: fixed, sensible defaults for the generic per-voice silence
        // detector (VoiceManager::updateSilenceAndCheckFinished) — reading
        // a real instance.sum node's own threshold/hold-time parameters
        // per graph is a later integration (InstanceMixNode.h's own
        // comment), not required for the mechanism itself to be correct
        // and generic. -80dB is a linear ~0.0001; 200ms matches
        // InstanceMixNode's own parameter default.
        static constexpr float silenceThresholdLinear = 0.0001f;
        int silenceHoldTimeSamples = 0; // computed from sample rate in prepareToPlay

        // The monoOnly path's own scratch buffer — DomainRedesign.md Batch 2
        // gave each origin bundle its OWN instanceMixScratchBuffer (see
        // OriginBundle's own comment), so the mono/no-origin-at-all case
        // (which was never a "voice sum" to begin with) keeps a dedicated
        // one instead of borrowing an origin bundle's.
        juce::AudioBuffer<float> monoRenderScratchBuffer;

        // M21: the host's input, copied out before anything can overwrite it (the
        // main input and output share the host buffer's first two channels), one
        // row per (bus, channel) — sized once in prepareToPlay, so the audio
        // thread never allocates. hostBusPresent[bus][channel] is false for a bus
        // the host didn't enable, which the nodes read as silence.
        juce::AudioBuffer<float> hostInputScratch;
        std::array<std::array<bool, bazalt::engine::HostInputs::channelsPerBus>, bazalt::engine::HostInputs::numAudioBuses> hostBusPresent {};

        // Persistent controller/pressure/pitch-bend state (updated from MIDI) plus
        // the per-range audio pointers and transport, rebuilt by
        // prepareHostInputsForRange() before every process() call.
        bazalt::engine::HostInputs hostInputs;

        // The transport as of the START of the current block (from the host's
        // playhead, or the internal transport when there is none — Standalone),
        // advanced per sub-range in prepareHostInputsForRange().
        struct BlockTransport
        {
            bool playing = false;
            double bpm = 120.0;
            double ppq = 0.0;
            double seconds = 0.0;
        };
        BlockTransport blockTransport;

        // Whether THIS block ran a mono graph. finalizeInstanceMixIntoOutput must
        // not also fetch the global plan then: PlanSwapper allows one fetch per
        // process() call, and a graph-mode switch can land between the two
        // reads of monoOnlyGraph/hasGlobalDomain.
        bool monoRenderedThisBlock = false;
        long long internalTransportSamples = 0;

        // Declared after everything it depends on (nodeFactory, the
        // swappers) so its constructor — which only stores a reference —
        // never sees them in a not-yet-constructed state.
        GraphEditController graphEditController { *this };

        std::array<std::atomic<float>, (size_t) numAuxBuses> auxPeakLevels {};

        // ARCHITECTURE.md §6: taps wired to main output + the 4 sidechain
        // inputs. telemetryHub must be declared before analysisThread —
        // AnalysisThread's default member initializer binds a reference to
        // it, and member initialization follows declaration order.
        bazalt::engine::TelemetryHub telemetryHub;
        bazalt::engine::AnalysisThread analysisThread { telemetryHub };
        std::array<bazalt::engine::Tap*, (size_t) (1 + numAuxBuses)> tapPointers {}; // [0]=main, [1..4]=aux1..4 — cached once in prepareToPlay so processBlock never does a map lookup

        // M20/ADR-0029 — every live preview subscription. Message-thread only
        // (subscribe/unsubscribe and GraphEditController's publish all run
        // there); the audio thread never sees this, only the per-plan tap
        // pointers and enable flags it leads to.
        struct PreviewSubscription
        {
            juce::String nodeId, portId;
            bazalt::engine::PreviewKind kind = bazalt::engine::PreviewKind::Waveform;
            bazalt::engine::Tap* tap = nullptr; // as of the last attach; how unsubscribe finds it on every plan
        };
        std::vector<PreviewSubscription> previewSubscriptions;

        /** Attaches one subscription's tap to whichever of the given plans
            resolve it: the global plan first (a node lives in exactly one
            place, other than an origin's own trigger source, which
            genuinely can be duplicated into more than one origin — a tap on
            THAT specific node attaches to all of them, no different from
            attaching to several voice plans already), else every voice plan
            given. Returns whether it resolved. */
        bool attachPreviewSubscription (PreviewSubscription& subscription,
                                        bazalt::engine::ExecutionPlan* globalPlan,
                                        const std::vector<bazalt::engine::ExecutionPlan*>& voicePlans);

        double currentSampleRate = 44100.0;
        int currentBlockSize = 512;
        bool hasBeenPrepared = false; // see prepareToPlay()'s own "redundant repeat" guard

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BazaltAudioProcessor)
    };
}
