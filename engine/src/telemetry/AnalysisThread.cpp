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

        scratchSamples.assign (maxSamplesPerDrain, 0.0f);
        oscilloscopePayload.assign ((size_t) oscilloscopeBuckets * 2, 0.0f);
        fftData.assign ((size_t) fftSize * 2, 0.0f);

        const auto maxFrameBytes = sizeof (TelemetryFrameHeader) + (size_t) fftSize * sizeof (float);
        frameScratch.reserve (maxFrameBytes);

        for (const auto& tapName : hub.getTapNames())
        {
            auto& ballistics = meterBallisticsByTap[tapName];
            ballistics.setTimes (0.001, 0.3);
            ballistics.reset();
        }
    }

    void AnalysisThread::run()
    {
        while (! threadShouldExit())
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            const auto elapsedSeconds = (now - lastDrainTimeMs) / 1000.0;
            lastDrainTimeMs = now;

            const auto& tapNames = hub.getTapNames();
            for (uint32_t tapId = 0; tapId < tapNames.size(); ++tapId)
                processTap (tapNames[tapId], tapId, elapsedSeconds);

            wait (drainIntervalMs);
        }
    }

    void AnalysisThread::processTap (const juce::String& tapName, uint32_t tapId, double elapsedSeconds)
    {
        auto* tap = hub.getTap (tapName);
        if (tap == nullptr)
            return;

        const auto numRead = tap->readLatest (scratchSamples.data(), (int) scratchSamples.size());
        if (numRead == 0)
            return;

        ++sequenceNumber;

        publishOscilloscope (tapName, tapId, scratchSamples.data(), numRead);
        publishSpectrum (tapName, tapId, scratchSamples.data(), numRead);
        publishMeter (tapName, tapId, scratchSamples.data(), numRead, elapsedSeconds);
    }

    void AnalysisThread::publishOscilloscope (const juce::String& tapName, uint32_t tapId, const float* samples, int numSamples)
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
        header.tapId = tapId;
        header.frameType = TelemetryFrameType::Oscilloscope;
        header.sampleRate = (float) sampleRate;
        header.sequenceNumber = sequenceNumber;

        serializeTelemetryFrame (header, oscilloscopePayload.data(), (uint32_t) oscilloscopePayload.size(), frameScratch);

        if (auto* buffer = hub.getFrameBuffer (tapName, TelemetryFrameType::Oscilloscope))
            buffer->publish (frameScratch.data(), frameScratch.size());
    }

    void AnalysisThread::publishSpectrum (const juce::String& tapName, uint32_t tapId, const float* samples, int numSamples)
    {
        std::fill (fftData.begin(), fftData.end(), 0.0f);

        const auto numToCopy = std::min (numSamples, fftSize);
        std::memcpy (fftData.data(), samples + (numSamples - numToCopy), (size_t) numToCopy * sizeof (float));

        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

        const auto numBins = (uint32_t) (fftSize / 2);

        TelemetryFrameHeader header;
        header.tapId = tapId;
        header.frameType = TelemetryFrameType::Spectrum;
        header.sampleRate = (float) sampleRate;
        header.sequenceNumber = sequenceNumber;

        serializeTelemetryFrame (header, fftData.data(), numBins, frameScratch);

        if (auto* buffer = hub.getFrameBuffer (tapName, TelemetryFrameType::Spectrum))
            buffer->publish (frameScratch.data(), frameScratch.size());
    }

    void AnalysisThread::publishMeter (const juce::String& tapName, uint32_t tapId, const float* samples, int numSamples, double elapsedSeconds)
    {
        float peak = 0.0f;
        double sumSquares = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            peak = std::max (peak, std::abs (samples[i]));
            sumSquares += (double) samples[i] * (double) samples[i];
        }

        const auto rms = (float) std::sqrt (sumSquares / (double) numSamples);
        const auto smoothedPeak = meterBallisticsByTap[tapName].pushPeak (peak, elapsedSeconds);

        const std::array<float, 2> payload { smoothedPeak, rms };

        TelemetryFrameHeader header;
        header.tapId = tapId;
        header.frameType = TelemetryFrameType::Meter;
        header.sampleRate = (float) sampleRate;
        header.sequenceNumber = sequenceNumber;

        serializeTelemetryFrame (header, payload.data(), (uint32_t) payload.size(), frameScratch);

        if (auto* buffer = hub.getFrameBuffer (tapName, TelemetryFrameType::Meter))
            buffer->publish (frameScratch.data(), frameScratch.size());
    }
}
