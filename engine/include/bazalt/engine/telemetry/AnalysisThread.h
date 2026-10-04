#pragma once

#include "bazalt/engine/telemetry/TelemetryHub.h"
#include "bazalt/engine/telemetry/MeterBallistics.h"
#include "bazalt/engine/telemetry/TapSettings.h"
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>
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

        /** Test hook: does exactly what one visit to this slot inside run()
            does, synchronously. Only valid while the thread is NOT running -
            it exists so each TapSettings field can be tested against known
            input without racing a live 100 Hz thread.
        */
        void processSlotForTesting (size_t slotIndex, double elapsedSeconds) { processTap (slotIndex, elapsedSeconds); }

        void run() override;

    private:
        static constexpr int oscilloscopeBuckets = 128;
        static constexpr int numFftOrders = TapSettings::maxFftOrder - TapSettings::minFftOrder + 1;
        static constexpr int maxFftSize = 1 << TapSettings::maxFftOrder;
        static constexpr int maxSamplesPerDrain = 8192;
        static constexpr double defaultMaxProcessingMsPerCycle = 5.0; // budget, at ~100Hz drain: <=50% duty cycle

        // design/Visualization/Ripple.png: the most events one publish ever
        // reports — a sane ceiling against a degenerate input (nothing
        // type-enforces that an Event-typed signal stays sparse; a
        // user could still wire audio-rate content into one), not a
        // realistic count for an actual trigger/gate stream. Keeps the
        // payload well inside maxTelemetryFrameBytes (shared with the
        // 4096-bin spectrum ceiling) with enormous headroom to spare.
        static constexpr int maxEventsPerPublish = 256;

        void processTap (size_t slotIndex, double elapsedSeconds);
        void publishOscilloscope (size_t slotIndex, const float* samples, int numSamples, const TapSettings& settings);
        void publishSpectrum (size_t slotIndex, const float* samples, int numSamples, const TapSettings& settings);
        void publishMeter (size_t slotIndex, const float* samples, int numSamples, double elapsedSeconds, const TapSettings& settings);
        void publishEventImpulse (size_t slotIndex, const float* samples, int numSamples, uint64_t totalPushed);

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
        std::vector<float> eventImpulsePayload; // reused across publishEventImpulse() calls, sized to maxEventsPerPublish in prepare()
        std::vector<std::byte> frameScratch;
        // ADR-0029: one FFT and Hann window per supported size, built once in
        // prepare(), so a tap's fftSize setting is an index, never an allocation.
        std::array<std::unique_ptr<juce::dsp::FFT>, numFftOrders> ffts;
        std::array<std::unique_ptr<juce::dsp::WindowingFunction<float>>, numFftOrders> windows;

        // Per-slot spectrum smoothing state (TapSettings::spectrumAveraging):
        // the running magnitudes, and the FFT order they were computed at (0 =
        // none yet) so changing the size starts a fresh average rather than
        // blending unrelated bins.
        std::vector<float> spectrumAverage; // maxTaps * maxSpectrumBins
        std::array<int, TelemetryHub::maxTaps> spectrumAverageOrder {};

        std::array<MeterBallistics, TelemetryHub::maxTaps> meterBallisticsBySlot;
        std::array<double, TelemetryHub::maxTaps> syntheticPhase {}; // per-slot phase for "demo." taps

        // Per-slot rising-edge memory for publishEventImpulse() — the same
        // "was the level already high as of the very last sample we saw"
        // state MacroNode's own previousTriggerLevelHigh keeps, just one
        // per tap slot instead of one per node instance, so an edge
        // spanning two consecutive drains (the common case: a drain almost
        // never lands exactly on an event boundary) is still only ever
        // reported once, not re-detected as a second edge at the start of
        // the next read.
        std::array<bool, TelemetryHub::maxTaps> eventWasHighBySlot {};

        // How many of this slot's own Tap::getTotalPushed() have already
        // been scanned for edges. Tap::readLatest() returns "the most
        // recent window currently in the ring" based on the CUMULATIVE
        // push count, not a delta since the last read — re-reading the
        // same already-scanned history whenever less than a full window's
        // worth of new data has arrived since the previous drain (the
        // ordinary case, at this thread's ~100Hz cadence against a typical
        // audio block size). Naively rescanning the whole window every
        // time would re-detect every old edge still inside it on every
        // subsequent drain; this is what lets publishEventImpulse scan
        // only the genuinely NEW tail instead.
        std::array<uint64_t, TelemetryHub::maxTaps> eventLastScannedTotalBySlot {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisThread)
    };
}
