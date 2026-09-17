#pragma once

#include "bazalt/engine/telemetry/TelemetryHub.h"
#include "bazalt/engine/telemetry/MeterBallistics.h"
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

namespace bazalt::engine
{
    /** The single non-RT analysis worker (ARCHITECTURE.md §6.2): drains
        active taps in the hub on a fixed poll interval and produces
        oscilloscope (min/max decimation), spectrum (Hann-windowed FFT),
        and meter (peak with ballistics + RMS) frames for each, publishing
        each into the hub's per-(tap, frame type) TelemetryFrameBuffer.
        Never blocks the audio thread — it only ever reads from Taps via
        their lock-free readLatest(), never anything the audio thread
        could contend on.

        M8: iterates TelemetryHub's fixed slot range (never by name — see
        TelemetryHub.h's threading note) and enforces a per-cycle
        processing time budget (NODE_EDITOR.md §3's "degrade gracefully...
        rather than dropping frames"): if a cycle's active-slot count would
        blow the budget, remaining slots are deferred to the *next* cycle
        (round-robin, so no slot starves under sustained overload) rather
        than every active tap's update rate dropping together, or a tap's
        frame silently never being produced.
    */
    class AnalysisThread : public juce::Thread
    {
    public:
        explicit AnalysisThread (TelemetryHub& hubToUse);

        /** Must be called before startThread(). Sizes every scratch buffer
            up front — nothing in run() allocates in steady state.
        */
        void prepare (double sampleRateToUse);

        /** Test/tuning hook — real usage never needs this (the 5ms default
            is what ships). Exists so a test can force the budget-exceeded
            path deterministically instead of depending on real FFT timing
            variance across machines.
        */
        void setMaxProcessingMsPerCycleForTesting (double ms) noexcept { maxProcessingMsPerCycle = ms; }

        void run() override;

    private:
        static constexpr int oscilloscopeBuckets = 128;
        static constexpr int fftOrder = 11;
        static constexpr int fftSize = 1 << fftOrder; // 2048
        static constexpr int maxSamplesPerDrain = 8192;
        static constexpr double defaultMaxProcessingMsPerCycle = 5.0; // budget, at ~100Hz drain: <=50% duty cycle

        void processTap (size_t slotIndex, double elapsedSeconds);
        void publishOscilloscope (size_t slotIndex, const float* samples, int numSamples);
        void publishSpectrum (size_t slotIndex, const float* samples, int numSamples);
        void publishMeter (size_t slotIndex, const float* samples, int numSamples, double elapsedSeconds);

        TelemetryHub& hub;
        double sampleRate = 44100.0;
        double lastDrainTimeMs = 0.0;
        double drainIntervalMs = 10.0; // ~100Hz — comfortably keeps tap ring buffers from wrapping
        uint64_t sequenceNumber = 0;
        size_t nextSlotToVisit = 0; // round-robin start point, so budget overrun defers fairly
        double maxProcessingMsPerCycle = defaultMaxProcessingMsPerCycle;

        std::vector<float> scratchSamples;
        std::vector<float> oscilloscopePayload;
        std::vector<float> fftData;
        std::vector<std::byte> frameScratch;
        juce::dsp::FFT fft { fftOrder };
        juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };

        std::array<MeterBallistics, TelemetryHub::maxTaps> meterBallisticsBySlot;
        std::array<double, TelemetryHub::maxTaps> syntheticPhase {}; // per-slot phase for "demo." taps

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisThread)
    };
}
