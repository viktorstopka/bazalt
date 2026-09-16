#pragma once

#include "bazalt/engine/telemetry/TelemetryHub.h"
#include "bazalt/engine/telemetry/MeterBallistics.h"
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <unordered_map>
#include <vector>

namespace bazalt::engine
{
    /** The single non-RT analysis worker (ARCHITECTURE.md §6.2): drains
        every tap in the hub on a fixed poll interval and produces
        oscilloscope (min/max decimation), spectrum (Hann-windowed FFT),
        and meter (peak with ballistics + RMS) frames for each, publishing
        each into the hub's per-(tap, frame type) TelemetryFrameBuffer.
        Never blocks the audio thread — it only ever reads from Taps via
        their lock-free readLatest(), never anything the audio thread
        could contend on.
    */
    class AnalysisThread : public juce::Thread
    {
    public:
        explicit AnalysisThread (TelemetryHub& hubToUse);

        /** Must be called before startThread(). Sizes every scratch buffer
            up front — nothing in run() allocates in steady state.
        */
        void prepare (double sampleRateToUse);

        void run() override;

    private:
        static constexpr int oscilloscopeBuckets = 128;
        static constexpr int fftOrder = 11;
        static constexpr int fftSize = 1 << fftOrder; // 2048
        static constexpr int maxSamplesPerDrain = 8192;

        void processTap (const juce::String& tapName, uint32_t tapId, double elapsedSeconds);
        void publishOscilloscope (const juce::String& tapName, uint32_t tapId, const float* samples, int numSamples);
        void publishSpectrum (const juce::String& tapName, uint32_t tapId, const float* samples, int numSamples);
        void publishMeter (const juce::String& tapName, uint32_t tapId, const float* samples, int numSamples, double elapsedSeconds);

        TelemetryHub& hub;
        double sampleRate = 44100.0;
        double lastDrainTimeMs = 0.0;
        double drainIntervalMs = 10.0; // ~100Hz — comfortably keeps tap ring buffers from wrapping
        uint64_t sequenceNumber = 0;

        std::vector<float> scratchSamples;
        std::vector<float> oscilloscopePayload;
        std::vector<float> fftData;
        std::vector<std::byte> frameScratch;
        juce::dsp::FFT fft { fftOrder };
        juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };

        std::unordered_map<juce::String, MeterBallistics> meterBallisticsByTap;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisThread)
    };
}
