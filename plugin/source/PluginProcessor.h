#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include "bazalt/engine/graph/ExecutionPlan.h"
#include "bazalt/engine/graph/PlanSwapper.h"
#include "bazalt/engine/graph/PreviewDescriptor.h"
#include "bazalt/engine/graph/VoiceManager.h"
#include "bazalt/engine/graph/NodeFactory.h"
#include "bazalt/engine/patch/PatchDocument.h"
#include "bazalt/engine/NanGuard.h"
#include "bazalt/engine/telemetry/TelemetryHub.h"
#include "bazalt/engine/telemetry/AnalysisThread.h"
#include "MacroParameters.h"
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

        Note-port routing through the compiled graph is still deferred
        (see CLAUDE.md's "known interim simplifications"): MIDI is
        translated directly into setParameter()/noteOn()/noteOff() calls on
        each voice's compiled node instances, not through a Note-typed
        port. That's a real, deliberate scoping decision for M3, not an
        oversight — full Note-port signal routing is a bigger change that
        only earns its cost once a live graph editor needs it.
    */
    class BazaltAudioProcessor final : public juce::AudioProcessor,
                                        private juce::Timer
    {
    public:
        static constexpr int numVoices = 8;
        static constexpr int numAuxBuses = 4;

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
        bazalt::engine::PlanSwapper& getVoicePlanSwapper (int voiceIndex) noexcept { return voicePlanSwappers[(size_t) voiceIndex]; }
        bazalt::engine::PlanSwapper& getGlobalPlanSwapper() noexcept { return globalPlanSwapper; }
        void setHasGlobalDomain (bool hasIt) noexcept { hasGlobalDomain.store (hasIt, std::memory_order_release); }

        /** M21: true while the graph has no instance.allocator (DomainSplitter.h's
            monoOnly) — the one compiled plan lives in the GLOBAL swapper and
            runs every block, voices are never allocated. Set by
            GraphEditController::recompileAndPublish(). Message-thread only.
        */
        void setMonoOnly (bool isMono) noexcept { monoOnlyGraph.store (isMono, std::memory_order_release); }

        /** M20 — subscribes a visualization tap for a real node's output
            port, resolving whether it lives in the global domain (one
            plan, tapped once, never re-pointed) or the voice domain (8
            independent plans — tapped on whichever one
            VoiceManager::getMostRecentlyTriggeredVoice() currently names,
            re-pointed live as new notes trigger, see handleMidiEvent()).
            Returns false if no currently-compiled plan resolves this
            (nodeId, portId) to a real output buffer (it doesn't exist, or
            is per-sample-region-internal with no external copy — see
            ExecutionPlan::outputBufferIndexByNodeAndPort's own comment).
            Message-thread only.
        */
        bool subscribeVisualizationTap (const juce::String& nodeId, const juce::String& portId, bazalt::engine::PreviewKind kind);
        void unsubscribeVisualizationTap (const juce::String& nodeId, const juce::String& portId);

    private:
        static BusesProperties makeBusLayout();

        using VoicePlanPtrs = std::array<bazalt::engine::ExecutionPlan*, numVoices>;

        void handleMidiEvent (const juce::MidiMessage& message, const VoicePlanPtrs& voicePlans);

        // M21 host boundary (engine/graph/HostInputs.h): what io.audioIn, io.control
        // and io.transport read. All audio-thread only, allocation-free.
        void captureHostInput (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;
        void beginTransportForBlock (int numSamples) noexcept;
        void prepareHostInputsForRange (int startSample) noexcept;
        void processPlanRange (bazalt::engine::ExecutionPlan* plan, int startSample, int numSamples) noexcept;
        void renderMonoRange (bazalt::engine::ExecutionPlan* plan, int startSample, int numSamples) noexcept;
        void repointVoiceDomainTaps (int newVoiceIndex, const VoicePlanPtrs& voicePlans) noexcept;
        void renderVoiceRange (int startSample, int numSamples, const VoicePlanPtrs& voicePlans) noexcept;
        void triggerVoiceNote (bazalt::engine::ExecutionPlan* plan, float pitch, float velocity) noexcept;

        /** The voice's io.noteIn node, or nullptr. Audio-thread safe: it looks
            the node up with a String built once at construction. Passing a
            literal to ExecutionPlan::getNodeById instead builds a heap-
            allocated juce::String on every call (an M18 bug the RT trap caught
            in M21). */
        bazalt::engine::nodes::IoNoteInNode* findNoteIn (bazalt::engine::ExecutionPlan* plan) const noexcept;
        void finalizeInstanceMixIntoOutput (juce::AudioBuffer<float>& output, int numSamples) noexcept;
        void updateAuxLevelsAndPassthrough (juce::AudioBuffer<float>& mainOutput, int numSamples);
        void setDefaultMacroMappings();

        // juce::Timer — periodic (~50ms, ARCHITECTURE.md §3.2) reclaim() on
        // every PlanSwapper below, message-thread only. engine/ has no
        // Timer (headless by design, CLAUDE.md rule 4); the plugin layer
        // owns scheduling it, exactly as PlanSwapper.h's own header comment
        // says it must.
        void timerCallback() override;

        bazalt::engine::NodeFactory nodeFactory;

        // One independently-swappable plan per voice — M17: DSP state now
        // survives a recompile that doesn't touch a given node's (id,
        // type), via GraphCompiler's previousPlan-aware reuse
        // (ARCHITECTURE.md §3.2's per-(voiceIndex,nodeID) pool, finally
        // built — RECONCILIATION.md 3.2/ADR-0020). Plus one for the
        // global domain, used only once a graph actually contains an
        // instance.mix node (DOMAINS.md §2, supersedes the original
        // util.voiceSum boundary); hasGlobalDomain is read on the audio
        // thread, so it's atomic despite being set only from the message
        // thread.
        std::array<bazalt::engine::PlanSwapper, numVoices> voicePlanSwappers;
        bazalt::engine::PlanSwapper globalPlanSwapper;
        std::atomic<bool> hasGlobalDomain { false };
        std::atomic<bool> monoOnlyGraph { false };
        const juce::String noteInNodeId { "noteIn" }; // see findNoteIn

        bazalt::engine::VoiceManager voiceManager;
        MacroParameters macroParameters;
        std::vector<bazalt::engine::MacroMapping> macroMappings;
        bazalt::engine::NanGuard outputGuard;

        // M17: fixed, sensible defaults for the generic per-voice silence
        // detector (VoiceManager::updateSilenceAndCheckFinished) — reading
        // a real instance.mix node's own threshold/hold-time parameters
        // per graph is a later integration (InstanceMixNode.h's own
        // comment), not required for the mechanism itself to be correct
        // and generic. -80dB is a linear ~0.0001; 200ms matches
        // InstanceMixNode's own parameter default.
        static constexpr float silenceThresholdLinear = 0.0001f;
        int silenceHoldTimeSamples = 0; // computed from sample rate in prepareToPlay

        // Mono sum (or average, per an instance.mix node's own mode
        // parameter) of every active voice's output for the current block,
        // sized once in prepareToPlay (no audio-thread allocation). When
        // there's no global domain this feeds the main output directly,
        // matching pre-M7 behaviour exactly; when an instance.mix node
        // exists, this is what gets handed to it via setExternalBlock()
        // before the global plan runs (DOMAINS.md §2).
        juce::AudioBuffer<float> instanceMixScratchBuffer;

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
        int activeVoiceCountThisBlock = 0; // for instance.mix's "average" mode

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

        // M20 — voice-domain visualization taps needing re-pointing as the
        // most-recently-triggered voice changes. Mirrors TelemetryHub::Slot's
        // own message-thread-claims/audio-thread-reads pattern exactly
        // (fully configure a slot, THEN set active=true last with release
        // ordering, so a reader who observes active=true via acquire is
        // guaranteed to also see a fully-configured bufferIndex/tap) —
        // scoped here rather than in engine/ since it's specifically about
        // redirecting between this processor's own 8 per-voice
        // ExecutionPlans, not a generic engine mechanism. nodeId/portId are
        // message-thread-owned (unsubscribe lookup only); bufferIndex/tap
        // are write-once-before-activation, read-only after.
        struct VoiceDomainTapSlot
        {
            std::atomic<bool> active { false };
            juce::String nodeId, portId;
            int bufferIndex = -1;
            bazalt::engine::Tap* tap = nullptr;
        };
        static constexpr int maxVoiceDomainTaps = (int) bazalt::engine::TelemetryHub::maxTaps;
        std::array<VoiceDomainTapSlot, (size_t) maxVoiceDomainTaps> voiceDomainTapSlots;
        // Audio-thread-owned only (handleMidiEvent is the only reader/writer,
        // and it always runs on the audio thread via processBlock) — not
        // atomic, matches every other audio-thread-only piece of state here.
        int lastPointedVoiceForTaps = -1;

        double currentSampleRate = 44100.0;
        int currentBlockSize = 512;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BazaltAudioProcessor)
    };
}
