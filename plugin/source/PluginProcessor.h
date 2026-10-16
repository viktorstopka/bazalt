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
#include "LiveParameterEdits.h"
#include "GraphEditController.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace bazalt::engine::nodes { class IoNoteInNode; }

namespace bazalt
{
    /** M3: real bus layout (main stereo in/out + 4 aux stereo sidechains),
        the hardcoded voice graph (ARCHITECTURE.md §3.4) driven by actual
        MIDI input through a fixed VoiceManager-backed pool, the macro
        parameter pool, and patch save/load. Plugin state IS the patch
        (§4.4) — getStateInformation/setStateInformation are thin wrappers
        around PatchSerializer.

        Up to `maxOrigins` simultaneous Populations ("life.population"), each
        with its own VoiceManager (`OriginBundle`); the whole patch is one
        plan (wiki/plans/InstanceAxis.md). Historical (DomainRedesign.md
        Batch 2): this used to be N physical ExecutionPlans per origin (§10.1's
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
        each instance's own `io.noteIn` copy, found by type at compile time
        — InstanceGroupRuntime::noteInNodes), and everything downstream of that is
        ordinary graph wiring through a real `Note`-typed port/buffer.
    */
    class BazaltAudioProcessor final : public juce::AudioProcessor,
                                        private juce::Timer,
                                        private bazalt::engine::InstanceBirthSink
    {
    public:
        /** Every allocator's pool is this big (its count parameter narrows
            it); the plan holds only the instances the count asks for. */
        static constexpr int maxInstances = bazalt::engine::maxInstancesPerGroup;
        static constexpr int numAuxBuses = 4;

        /** wiki/plans/DomainRedesign.md's own small, fixed ceiling — mirrors
            MultiplicityResolver::maxOrigins exactly (this alias exists so
            plugin-layer code never has to spell out the engine namespace
            just to size an array).
        */
        static constexpr int maxOrigins = bazalt::engine::MultiplicityResolver::maxOrigins;

        /** One allocator's runtime: the VoiceManager that decides which of
            its instances are alive (wiki/plans/InstanceAxis.md — the instances
            themselves live in the one plan, as a group of it). Slot
            assignment (which bundle an origin id occupies) is decided by
            GraphEditController and kept stable across an edit that doesn't
            remove the origin, so voices mid-note survive a plain edit.
        */
        struct OriginBundle
        {
            bazalt::engine::VoiceManager voiceManager;
            juce::String originNodeId;
            bool active = false;

            int activeVoiceCountThisBlock = 0; // instances alive after the last range
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
        LiveParameterEdits& getLiveParameterEdits() noexcept { return liveParameterEdits; }

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
        bool isAnalysisRunningForTesting() const noexcept { return analysisThread.isThreadRunning(); }
        /** Tests: the plan holding origin `slot`'s first voice — the one plan
            (getNodeById() answers an instanced node with instance 0's copy). */
        bazalt::engine::ExecutionPlan* peekFirstVoicePlanForTesting (int slot)
        {
            juce::ignoreUnused (slot);
            return globalPlanSwapper.peekCurrentPlan();
        }

        // ---- Origin bundle slot management (GraphEditController-facing) ----
        // Message-thread only. See OriginBundle's own doc comment on why
        // slot assignment is kept stable across an edit.
        bool isOriginBundleActive (int index) const noexcept { return originBundles[(size_t) index].active; }
        const juce::String& getOriginBundleOriginId (int index) const noexcept { return originBundles[(size_t) index].originNodeId; }
        OriginBundle& getOriginBundle (int index) noexcept { return originBundles[(size_t) index]; }

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

        /** A Population's Size, enforced (VoiceManager::setMaxActiveVoices) —
            called once per recompile. */
        void setOriginMaxVoices (int bundleIndex, int maxVoices) noexcept
        {
            originBundles[(size_t) bundleIndex].voiceManager.setMaxActiveVoices (maxVoices);
        }

        /** The instance-count badge's two numbers for this origin bundle —
            message-thread safe (both are atomics on VoiceManager). */
        int getOriginActiveVoiceCount (int bundleIndex) const noexcept
        {
            return originBundles[(size_t) bundleIndex].voiceManager.getActiveVoiceCount();
        }
        int getOriginMaxVoices (int bundleIndex) const noexcept
        {
            return originBundles[(size_t) bundleIndex].voiceManager.getMaxActiveVoices();
        }

        /** M20: subscribes a visualization tap for a real node's output
            port in the one plan. A node that runs per instance taps every
            instance's buffer, and the plan pushes only the instance its
            group shows (the most recently played one —
            InstanceGroupRuntime::previewInstance).
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
            a freshly compiled plan (message thread). Re-attaches every live
            subscription to it. The plan isn't visible to the audio thread
            yet, so there is nothing to race with. A subscription that no longer resolves (its node
            was deleted, say) is kept and simply doesn't attach - the UI
            unsubscribes when its preview unmounts.
        */
        void applyPreviewSubscriptions (bazalt::engine::ExecutionPlan* plan);

        /** Where a Population born from inside the patch asks for members
            (GraphEditController hands it to every group of a new plan). */
        bazalt::engine::InstanceBirthSink* getBirthSink() noexcept { return this; }

        /** Population's When full, for this bundle (message thread, per recompile). */
        void setOriginStealPolicy (int bundleIndex, bazalt::engine::VoiceManager::StealPolicy policy) noexcept
        {
            originBundles[(size_t) bundleIndex].voiceManager.setStealPolicy (policy);
        }

    private:
        // InstanceBirthSink: a birth inside the plan, mid-process, on the audio thread.
        int born (bazalt::engine::InstanceGroupRuntime& group, int offset, float pitch, float velocity, float durationSeconds,
                  int parentInstance) noexcept override;
        /** 2b.5: a member starting again or ending takes the members of every
            Population nested inside it with it. Audio thread. */
        void endNestedMembers (bazalt::engine::ExecutionPlan& plan, int group, int instance) noexcept;
        void release (bazalt::engine::InstanceGroupRuntime& group, int instance, int offset) noexcept override;
        bazalt::engine::VoiceManager::NoteId nextBirthNoteId = firstBirthNoteId;
        // MIDI note ids are (channel << 8) | note, below this.
        static constexpr bazalt::engine::VoiceManager::NoteId firstBirthNoteId = 0x10000;

        static BusesProperties makeBusLayout();

        void handleMidiEvent (const juce::MidiMessage& message);

        // M21 host boundary (engine/graph/HostInputs.h): what io.audioIn, io.control
        // and io.transport read. All audio-thread only, allocation-free.
        void captureHostInput (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;
        void beginTransportForBlock (int numSamples) noexcept;
        void prepareHostInputsForRange (int startSample) noexcept;
        void processPlanRange (bazalt::engine::ExecutionPlan* plan, int startSample, int numSamples) noexcept;

        // ---- The instance axis (wiki/plans/InstanceAxis.md) ----------------
        // One plan per patch; each allocator is a group of it whose instances
        // its bundle's VoiceManager allocates.
        void handleNoteEvent (const juce::MidiMessage& message, bazalt::engine::ExecutionPlan* plan) noexcept;
        void renderRange (bazalt::engine::ExecutionPlan* plan, int startSample, int numSamples) noexcept;
        void startInstance (bazalt::engine::ExecutionPlan& plan, int group, int instance, float pitch, float velocity, bool viaAllocator) noexcept;
        void writeOutput (juce::AudioBuffer<float>& output, int numSamples) noexcept;
        void updateAuxLevels (int numSamples); // aux meters + taps; never mixed into the output

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
        juce::uint32 lastProcessBlockLogMs = 0;

        // One runtime per allocator (its VoiceManager) and the one plan the
        // whole patch compiles into (wiki/plans/InstanceAxis.md).
        std::array<OriginBundle, maxOrigins> originBundles;
        bazalt::engine::PlanSwapper globalPlanSwapper;

        MacroParameters macroParameters;
        LiveParameterEdits liveParameterEdits; // values moving while a slider is dragged
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

        // The plan's output for the whole block, range by range (a block is
        // split at every MIDI event and every steal that completes).
        juce::AudioBuffer<float> renderScratchBuffer;

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

        bazalt::engine::ExecutionPlan* planThisBlock = nullptr; // fetched once per block (PlanSwapper's contract)
        long long internalTransportSamples = 0;

        // Declared after everything it depends on (nodeFactory, the
        // swappers) so its constructor — which only stores a reference —
        // never sees them in a not-yet-constructed state.
        GraphEditController graphEditController { *this };

        std::array<std::atomic<float>, (size_t) numAuxBuses> auxPeakLevels {};
        std::atomic<float> mainInputPeakLevel { 0.0f }; // diagnostic log only

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

        /** Attaches one subscription's tap to the plan: the node's own buffer,
            or every instance's buffer for a node that runs per instance.
            Returns whether it resolved. */
        bool attachPreviewSubscription (PreviewSubscription& subscription, bazalt::engine::ExecutionPlan* plan);

        double currentSampleRate = 44100.0;
        int currentBlockSize = 512;
        bool hasBeenPrepared = false; // see prepareToPlay()'s own "redundant repeat" guard

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BazaltAudioProcessor)
    };
}
