#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "bazalt/engine/graph/ExecutionPlan.h"
#include "bazalt/engine/graph/VoiceManager.h"
#include "bazalt/engine/graph/NodeFactory.h"
#include "bazalt/engine/patch/PatchDocument.h"
#include "bazalt/engine/NanGuard.h"
#include "bazalt/engine/telemetry/TelemetryHub.h"
#include "bazalt/engine/telemetry/AnalysisThread.h"
#include "MacroParameters.h"
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
    class BazaltAudioProcessor final : public juce::AudioProcessor
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

    private:
        static BusesProperties makeBusLayout();

        void handleMidiEvent (const juce::MidiMessage& message);
        void renderVoiceRange (juce::AudioBuffer<float>& output, int startSample, int numSamples) noexcept;
        void updateAuxLevelsAndPassthrough (juce::AudioBuffer<float>& mainOutput, int numSamples);
        void setDefaultMacroMappings();

        bazalt::engine::NodeFactory nodeFactory;
        std::vector<std::unique_ptr<bazalt::engine::ExecutionPlan>> voicePlans;
        bazalt::engine::VoiceManager voiceManager;
        MacroParameters macroParameters;
        std::vector<bazalt::engine::MacroMapping> macroMappings;
        bazalt::engine::NanGuard outputGuard;

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
