#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include "bazalt/engine/graph/ExecutionPlan.h"
#include "bazalt/engine/graph/PlanSwapper.h"
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

    private:
        static BusesProperties makeBusLayout();

        using VoicePlanPtrs = std::array<bazalt::engine::ExecutionPlan*, numVoices>;

        void handleMidiEvent (const juce::MidiMessage& message, const VoicePlanPtrs& voicePlans);
        void renderVoiceRange (int startSample, int numSamples, const VoicePlanPtrs& voicePlans) noexcept;
        void triggerVoiceNote (bazalt::engine::ExecutionPlan* plan, float frequency, float velocity) noexcept;
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

        double currentSampleRate = 44100.0;
        int currentBlockSize = 512;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BazaltAudioProcessor)
    };
}
