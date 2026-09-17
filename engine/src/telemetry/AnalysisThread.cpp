#include "bazalt/engine/telemetry/AnalysisThread.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace bazalt::engine
{
    AnalysisThread::AnalysisThread (TelemetryHub& hubToUse)
        : juce::Thread ("Bazalt Analysis"), hub (hubToUse)
    {
    }

    void AnalysisThread::prepare (double sampleRateToUse)
    {
        sampleRate = sampleRateToUse;
        lastDrainTimeMs = juce::Time::getMillisecondCounterHiRes();
        sequenceNumber = 0;
        nextSlotToVisit = 0;

        scratchSamples.assign (maxSamplesPerDrain, 0.0f);
        oscilloscopePayload.assign ((size_t) oscilloscopeBuckets * 2, 0.0f);
        fftData.assign ((size_t) fftSize * 2, 0.0f);

        const auto maxFrameBytes = sizeof (TelemetryFrameHeader) + (size_t) fftSize * sizeof (float);
        frameScratch.reserve (maxFrameBytes);

        for (auto& ballistics : meterBallisticsBySlot)
        {
            ballistics.setTimes (0.001, 0.3);
            ballistics.reset();
        }

        syntheticPhase.fill (0.0);
    }

    void AnalysisThread::run()
    {
        while (! threadShouldExit())
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            const auto elapsedSeconds = (now - lastDrainTimeMs) / 1000.0;
            lastDrainTimeMs = now;

            const auto cycleStartMs = juce::Time::getMillisecondCounterHiRes();

            // Round-robin over the fixed slot range, starting where the
            // last cycle left off — under budget pressure this is what
            // makes "some taps update less often" fair instead of
            // starving whichever slots happen to sort last.
            bool completedFullSweep = true;

            for (size_t visited = 0; visited < TelemetryHub::maxTaps; ++visited)
            {
                const auto slotIndex = (nextSlotToVisit + visited) % TelemetryHub::maxTaps;

                if (hub.isSlotActive (slotIndex))
                    processTap (slotIndex, elapsedSeconds);

                if (juce::Time::getMillisecondCounterHiRes() - cycleStartMs >= maxProcessingMsPerCycle)
                {
                    nextSlotToVisit = (slotIndex + 1) % TelemetryHub::maxTaps;
                    completedFullSweep = false;
                    break;
                }
            }

            if (completedFullSweep)
                nextSlotToVisit = 0; // restart from the top next cycle

            wait (drainIntervalMs);
        }
    }

    void AnalysisThread::processTap (size_t slotIndex, double elapsedSeconds)
    {
        auto* tap = hub.getTapBySlot (slotIndex);

        if (hub.isSlotSynthetic (slotIndex))
        {
            // M8's UI-only rendering stress test subscribes many "demo."
            // taps with no real audio-thread pusher behind them
            // (TelemetryHub.h's subscribeTap() note) — generate a simple,
            // per-slot-distinct waveform here instead of waiting for data
            // that will never arrive. Single-threaded write-then-read on
            // this same (analysis) thread — no cross-thread concern at all
            // for this specific path.
            syntheticPhase[slotIndex] += elapsedSeconds * (0.5 + 0.1 * (double) (slotIndex % 7));
            const auto amplitude = 0.5f + 0.5f * std::sin ((float) syntheticPhase[slotIndex]);

            constexpr int numSyntheticSamples = 256;
            for (int i = 0; i < numSyntheticSamples; ++i)
                scratchSamples[(size_t) i] = amplitude * std::sin (0.2f * (float) i);

            tap->push (scratchSamples.data(), numSyntheticSamples);
        }

        const auto numRead = tap->readLatest (scratchSamples.data(), (int) scratchSamples.size());
        if (numRead == 0)
            return;

        ++sequenceNumber;

        publishOscilloscope (slotIndex, scratchSamples.data(), numRead);
        publishSpectrum (slotIndex, scratchSamples.data(), numRead);
        publishMeter (slotIndex, scratchSamples.data(), numRead, elapsedSeconds);
    }

    void AnalysisThread::publishOscilloscope (size_t slotIndex, const float* samples, int numSamples)
    {
        const auto samplesPerBucket = std::max (1, numSamples / oscilloscopeBuckets);

        for (int b = 0; b < oscilloscopeBuckets; ++b)
        {
            const auto start = b * samplesPerBucket;
            const auto end = std::min (numSamples, start + samplesPerBucket);

            float lo = 0.0f, hi = 0.0f;
            bool any = false;

            for (int i = start; i < end; ++i)
            {
                if (! any)
                {
                    lo = hi = samples[i];
                    any = true;
                }
                else
                {
                    lo = std::min (lo, samples[i]);
                    hi = std::max (hi, samples[i]);
                }
            }

            oscilloscopePayload[(size_t) b * 2] = lo;
            oscilloscopePayload[(size_t) b * 2 + 1] = hi;
        }

        TelemetryFrameHeader header;
        header.tapId = (uint32_t) slotIndex;
        header.frameType = TelemetryFrameType::Oscilloscope;
        header.sampleRate = (float) sampleRate;
        header.sequenceNumber = sequenceNumber;

        serializeTelemetryFrame (header, oscilloscopePayload.data(), (uint32_t) oscilloscopePayload.size(), frameScratch);

        if (auto* buffer = hub.getFrameBufferBySlot (slotIndex, TelemetryFrameType::Oscilloscope))
            buffer->publish (frameScratch.data(), frameScratch.size());
    }

    void AnalysisThread::publishSpectrum (size_t slotIndex, const float* samples, int numSamples)
    {
        std::fill (fftData.begin(), fftData.end(), 0.0f);

        const auto numToCopy = std::min (numSamples, fftSize);
        std::memcpy (fftData.data(), samples + (numSamples - numToCopy), (size_t) numToCopy * sizeof (float));

        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

        const auto numBins = (uint32_t) (fftSize / 2);

        TelemetryFrameHeader header;
        header.tapId = (uint32_t) slotIndex;
        header.frameType = TelemetryFrameType::Spectrum;
        header.sampleRate = (float) sampleRate;
        header.sequenceNumber = sequenceNumber;

        serializeTelemetryFrame (header, fftData.data(), numBins, frameScratch);

        if (auto* buffer = hub.getFrameBufferBySlot (slotIndex, TelemetryFrameType::Spectrum))
            buffer->publish (frameScratch.data(), frameScratch.size());
    }

    void AnalysisThread::publishMeter (size_t slotIndex, const float* samples, int numSamples, double elapsedSeconds)
    {
        float peak = 0.0f;
        double sumSquares = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            peak = std::max (peak, std::abs (samples[i]));
            sumSquares += (double) samples[i] * (double) samples[i];
        }

        const auto rms = (float) std::sqrt (sumSquares / (double) numSamples);
        const auto smoothedPeak = meterBallisticsBySlot[slotIndex].pushPeak (peak, elapsedSeconds);

        const std::array<float, 2> payload { smoothedPeak, rms };

        TelemetryFrameHeader header;
        header.tapId = (uint32_t) slotIndex;
        header.frameType = TelemetryFrameType::Meter;
        header.sampleRate = (float) sampleRate;
        header.sequenceNumber = sequenceNumber;

        serializeTelemetryFrame (header, payload.data(), (uint32_t) payload.size(), frameScratch);

        if (auto* buffer = hub.getFrameBufferBySlot (slotIndex, TelemetryFrameType::Meter))
            buffer->publish (frameScratch.data(), frameScratch.size());
    }
}
