#include "bazalt/engine/telemetry/AnalysisThread.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

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
        fftData.assign ((size_t) maxFftSize * 2, 0.0f);
        eventImpulsePayload.assign ((size_t) maxEventsPerPublish, 0.0f);
        eventWasHighBySlot.fill (false);
        eventLastScannedTotalBySlot.fill (0);

        // NaN: "this column has never been written" (publishRollingHistory's
        // own no-data sentinel, same NaN-means-absent idiom
        // GraphCompiler::applyHostInputs already established for an
        // unconnected port) - reset unconditionally on every prepare(), same
        // as every other per-tap accumulator above: a sample-rate change
        // invalidates the accumulated column timing anyway, so there is
        // nothing worth preserving across a re-prepare here (unlike
        // TelemetryHub's own SLOT metadata, which is a different class with
        // a real, documented reason to survive one).
        historyColumnLo.assign (TelemetryHub::maxTaps * (size_t) maxHistoryColumns, std::numeric_limits<float>::quiet_NaN());
        historyColumnHi.assign (TelemetryHub::maxTaps * (size_t) maxHistoryColumns, std::numeric_limits<float>::quiet_NaN());
        historyHeadBySlot.fill (0);
        historyElapsedInColumnBySlot.fill (0.0);
        historyConfiguredWindowBySlot.fill (0.0f);
        historyPayload.assign ((size_t) maxHistoryColumns * 2, 0.0f);
        historyLastScannedTotalBySlot.fill (0);

        // Built once, here: a tap's fftSize setting selects among these and
        // never allocates. (Left alone on a second prepare() - they don't
        // depend on the sample rate.)
        for (int i = 0; i < numFftOrders; ++i)
        {
            if (ffts[(size_t) i] == nullptr)
            {
                const auto order = TapSettings::minFftOrder + i;
                ffts[(size_t) i] = std::make_unique<juce::dsp::FFT> (order);
                windows[(size_t) i] = std::make_unique<juce::dsp::WindowingFunction<float>> (
                    (size_t) 1 << order, juce::dsp::WindowingFunction<float>::hann);
            }
        }

        spectrumAverage.assign (TelemetryHub::maxTaps * maxSpectrumBins, 0.0f);
        spectrumAverageOrder.fill (0);

        frameScratch.reserve (maxTelemetryFrameBytes);

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

        // ADR-0029: how this tap wants to be analysed (window, FFT size, meter
        // mode, ...). Defaults reproduce exactly what every tap got before.
        const auto settings = hub.getTapSettingsBySlot (slotIndex);

        // M20: only do the work a subscriber actually asked for — a
        // Waveform-only preview tap stops paying for an FFT nobody reads.
        // Every M4 baseline tap subscribes with the all-true default, so
        // this is a pure scope reduction, never a behaviour change for
        // anything that doesn't ask for it.
        if (hub.isFrameTypeNeeded (slotIndex, TelemetryFrameType::Oscilloscope))
            publishOscilloscope (slotIndex, scratchSamples.data(), numRead, settings);
        if (hub.isFrameTypeNeeded (slotIndex, TelemetryFrameType::Spectrum))
            publishSpectrum (slotIndex, scratchSamples.data(), numRead, settings);
        if (hub.isFrameTypeNeeded (slotIndex, TelemetryFrameType::Meter))
            publishMeter (slotIndex, scratchSamples.data(), numRead, elapsedSeconds, settings);
        if (hub.isFrameTypeNeeded (slotIndex, TelemetryFrameType::EventImpulse))
            publishEventImpulse (slotIndex, scratchSamples.data(), numRead, tap->getTotalPushed());
        if (hub.isFrameTypeNeeded (slotIndex, TelemetryFrameType::RollingHistory))
            publishRollingHistory (slotIndex, scratchSamples.data(), numRead, tap->getTotalPushed(), settings);
    }

    void AnalysisThread::publishOscilloscope (size_t slotIndex, const float* samples, int numSamples, const TapSettings& settings)
    {
        // Which samples to show. A window of 0 keeps the pre-ADR-0029 behaviour
        // (everything the tap returned); otherwise the newest `window` of them,
        // or - re-triggered - the newest window that starts on a rising edge.
        const auto requested = settings.scopeWindowSeconds > 0.0f
                                   ? (int) std::lround ((double) settings.scopeWindowSeconds * sampleRate)
                                   : numSamples;
        const auto window = std::clamp (requested, std::min (numSamples, oscilloscopeBuckets), numSamples);

        auto start = numSamples - window;

        if (settings.scopeTrigger == ScopeTriggerMode::RisingEdge && numSamples > window)
        {
            // The crossing level is the middle of what was read, so it works for
            // a bipolar signal (crossing zero) and a unipolar control signal
            // alike. The latest crossing that still leaves a full window after
            // it wins; a flat signal, or one with no rising edge, just stays free.
            const auto [lowest, highest] = std::minmax_element (samples, samples + numSamples);
            const auto level = 0.5f * (*lowest + *highest);

            for (int i = numSamples - window; i >= 1; --i)
            {
                if (samples[i - 1] < level && samples[i] >= level)
                {
                    start = i;
                    break;
                }
            }
        }

        for (int b = 0; b < oscilloscopeBuckets; ++b)
        {
            // Proportional bucket edges, so every sample of the window lands in
            // exactly one bucket (a fixed samples-per-bucket left a dead tail).
            const auto bucketStart = start + (int) ((int64_t) b * window / oscilloscopeBuckets);
            auto bucketEnd = start + (int) ((int64_t) (b + 1) * window / oscilloscopeBuckets);
            bucketEnd = std::max (bucketEnd, bucketStart + 1);
            bucketEnd = std::min (bucketEnd, numSamples);

            float lo = 0.0f, hi = 0.0f;
            bool any = false;

            for (int i = bucketStart; i < bucketEnd; ++i)
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

    void AnalysisThread::publishSpectrum (size_t slotIndex, const float* samples, int numSamples, const TapSettings& settings)
    {
        const auto order = std::clamp (settings.fftOrder, TapSettings::minFftOrder, TapSettings::maxFftOrder);
        const auto fftSize = 1 << order;
        const auto numBins = (size_t) (fftSize / 2);
        auto& fft = *ffts[(size_t) (order - TapSettings::minFftOrder)];
        auto& window = *windows[(size_t) (order - TapSettings::minFftOrder)];

        std::fill (fftData.begin(), fftData.begin() + (std::ptrdiff_t) fftSize * 2, 0.0f);

        const auto numToCopy = std::min (numSamples, fftSize);
        std::memcpy (fftData.data(), samples + (numSamples - numToCopy), (size_t) numToCopy * sizeof (float));

        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

        // Smoothing runs on the raw magnitudes and tilt is applied after it, so
        // changing the tilt never contaminates the running average. A new FFT
        // size starts a fresh average: bin k means a different frequency now.
        auto* average = spectrumAverage.data() + slotIndex * maxSpectrumBins;
        const auto smoothing = std::clamp (settings.spectrumAveraging, 0.0f, 0.99f);
        const auto continuing = smoothing > 0.0f && spectrumAverageOrder[slotIndex] == order;

        for (size_t k = 0; k < numBins; ++k)
            average[k] = continuing ? smoothing * average[k] + (1.0f - smoothing) * fftData[k] : fftData[k];

        spectrumAverageOrder[slotIndex] = smoothing > 0.0f ? order : 0;

        // Tilt: `tilt` dB per octave about 1 kHz, as a linear gain per bin.
        // 10^(tilt * log2(x) / 20) == x^(tilt * log2(10) / 20).
        const auto tilt = settings.spectrumTiltDbPerOctave;
        const auto exponent = (double) tilt * 0.16609640474436813;
        const auto binHz = sampleRate / (double) fftSize;

        for (size_t k = 0; k < numBins; ++k)
        {
            auto value = average[k];

            if (tilt != 0.0f)
            {
                const auto hz = (double) std::max<size_t> (k, 1) * binHz; // bin 0 (DC) takes bin 1's gain
                value *= (float) std::pow (hz / 1000.0, exponent);
            }

            fftData[k] = value;
        }

        TelemetryFrameHeader header;
        header.tapId = (uint32_t) slotIndex;
        header.frameType = TelemetryFrameType::Spectrum;
        header.sampleRate = (float) sampleRate;
        header.sequenceNumber = sequenceNumber;

        serializeTelemetryFrame (header, fftData.data(), (uint32_t) numBins, frameScratch);

        if (auto* buffer = hub.getFrameBufferBySlot (slotIndex, TelemetryFrameType::Spectrum))
            buffer->publish (frameScratch.data(), frameScratch.size());
    }

    namespace
    {
        // The 4x interpolation filter behind the true-peak meter: for each of
        // the three points between two samples (a quarter, half and three
        // quarters of the way), a 16-tap Hann-windowed sinc, normalised to unity
        // gain at DC. Built once. An estimate of the true (inter-sample) peak
        // that lands within a fraction of a dB on ordinary material - but NOT
        // the ITU-R BS.1770 measurement, which specifies a longer polyphase
        // filter, so it is labelled as an estimate on the node.
        struct TruePeakFilter
        {
            static constexpr int tapsBefore = 7;  // samples before the interpolated point's left neighbour
            static constexpr int tapsAfter = 8;   // ... and after
            static constexpr int numTaps = tapsBefore + tapsAfter + 1;
            static constexpr int numPhases = 3;

            std::array<std::array<float, numTaps>, numPhases> coefficients {};

            TruePeakFilter()
            {
                constexpr double pi = 3.14159265358979323846;

                for (int phase = 0; phase < numPhases; ++phase)
                {
                    const auto fraction = 0.25 * (phase + 1);
                    double sum = 0.0;

                    for (int j = 0; j < numTaps; ++j)
                    {
                        const auto u = (double) (j - tapsBefore) - fraction; // distance from the interpolated point
                        const auto sinc = std::abs (u) < 1.0e-9 ? 1.0 : std::sin (pi * u) / (pi * u);
                        const auto window = 0.5 * (1.0 + std::cos (pi * u / 8.0));
                        coefficients[(size_t) phase][(size_t) j] = (float) (sinc * window);
                        sum += sinc * window;
                    }

                    for (auto& c : coefficients[(size_t) phase])
                        c = (float) ((double) c / sum);
                }
            }
        };

        float interSamplePeak (const float* samples, int numSamples) noexcept
        {
            static const TruePeakFilter filter;

            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                peak = std::max (peak, std::abs (samples[i]));

            // Only where the whole filter has real samples under it; the edges of
            // the window are covered by the plain sample peak above.
            for (int i = TruePeakFilter::tapsBefore; i + TruePeakFilter::tapsAfter < numSamples; ++i)
            {
                for (const auto& phase : filter.coefficients)
                {
                    float value = 0.0f;
                    for (int j = 0; j < TruePeakFilter::numTaps; ++j)
                        value += samples[i + j - TruePeakFilter::tapsBefore] * phase[(size_t) j];

                    peak = std::max (peak, std::abs (value));
                }
            }

            return peak;
        }
    }

    void AnalysisThread::publishMeter (size_t slotIndex, const float* samples, int numSamples, double elapsedSeconds,
                                       const TapSettings& settings)
    {
        float samplePeak = 0.0f;
        double sumSquares = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            samplePeak = std::max (samplePeak, std::abs (samples[i]));
            sumSquares += (double) samples[i] * (double) samples[i];
        }

        const auto rms = (float) std::sqrt (sumSquares / (double) numSamples);

        // The frame is always { line, bar }: the UI draws the second as a filled
        // bar and the first as a line across it, whatever the mode. Peak (the
        // long-standing behaviour): a ballistic peak line over the RMS bar.
        // TruePeak: the same, with the 4x inter-sample peak in place of the sample
        // peak. Rms: the line rides on the bar - one reading, no peak.
        std::array<float, 2> payload {};

        if (settings.meterMode == MeterMode::Rms)
        {
            payload = { rms, rms };
        }
        else
        {
            const auto peak = settings.meterMode == MeterMode::TruePeak ? interSamplePeak (samples, numSamples) : samplePeak;
            payload = { meterBallisticsBySlot[slotIndex].pushPeak (peak, elapsedSeconds), rms };
        }

        TelemetryFrameHeader header;
        header.tapId = (uint32_t) slotIndex;
        header.frameType = TelemetryFrameType::Meter;
        header.sampleRate = (float) sampleRate;
        header.sequenceNumber = sequenceNumber;

        serializeTelemetryFrame (header, payload.data(), (uint32_t) payload.size(), frameScratch);

        if (auto* buffer = hub.getFrameBufferBySlot (slotIndex, TelemetryFrameType::Meter))
            buffer->publish (frameScratch.data(), frameScratch.size());
    }

    void AnalysisThread::publishEventImpulse (size_t slotIndex, const float* samples, int numSamples, uint64_t totalPushed)
    {
        // Tap::readLatest() returns "the most recent window currently in
        // the ring", not a delta since the last call — re-including
        // already-scanned history whenever less than a full window's worth
        // of new data has arrived since the previous drain (the ordinary
        // case). Rescanning that old portion from `samples[0]` every time
        // would re-detect its edges again on every subsequent drain, using
        // a `wasHigh` carried from the END of the PREVIOUS scan but applied
        // at the START of a window that reaches further back than that —
        // a real bug this exact scenario caught live (a test driving two
        // separate pushes/drains through a synchronous Rig). Scanning only
        // the genuinely new tail — the last `min(totalPushed -
        // eventLastScannedTotalBySlot[slotIndex], numSamples)` samples —
        // is what makes `wasHigh` actually line up with "the sample
        // immediately before the first NEW one this call examines".
        const auto alreadyScanned = eventLastScannedTotalBySlot[slotIndex];
        const auto newSinceLastScan = totalPushed > alreadyScanned
                                           ? (int) std::min<uint64_t> (totalPushed - alreadyScanned, (uint64_t) numSamples)
                                           : 0;
        eventLastScannedTotalBySlot[slotIndex] = totalPushed;
        const auto scanStart = numSamples - newSinceLastScan;

        // "non-zero this sample = fired" (SineOscillatorNode.h's own "sync"
        // convention) refined to a RISING edge crossing 0.5 (MacroNode.h's
        // own Trigger detection: `storedValue >= 0.5f`) — a held-high Event
        // signal (a gate, not a one-sample pulse) reports exactly one ring
        // at the moment it opens, not one per sample for as long as it
        // stays high. eventWasHighBySlot carries the level across drains so
        // an edge exactly on a drain boundary is never double-counted or
        // missed.
        auto wasHigh = eventWasHighBySlot[slotIndex];
        int numEvents = 0;

        for (int i = scanStart; i < numSamples; ++i)
        {
            const auto isHigh = samples[i] >= 0.5f;
            if (isHigh && ! wasHigh && numEvents < maxEventsPerPublish)
            {
                // Age in seconds: how long ago (relative to the END of this
                // read, i.e. "now") this specific sample fired — the UI
                // spawns each ring already partway through its life rather
                // than always starting fresh, so a dense burst within one
                // drain still shows each event at its own true relative age,
                // not all bunched at age zero.
                eventImpulsePayload[(size_t) numEvents] = (float) (numSamples - 1 - i) / (float) sampleRate;
                ++numEvents;
            }
            wasHigh = isHigh;
        }

        eventWasHighBySlot[slotIndex] = wasHigh;

        // Nothing NEW this drain — leave whatever was last published alone
        // rather than publishing an empty frame every ~10ms. The UI side
        // only ever spawns rings off a sequenceNumber it hasn't already
        // seen, so a drain with nothing to add simply produces no visible
        // effect, the same as if this call never ran.
        if (numEvents == 0)
            return;

        TelemetryFrameHeader header;
        header.tapId = (uint32_t) slotIndex;
        header.frameType = TelemetryFrameType::EventImpulse;
        header.sampleRate = (float) sampleRate;
        header.sequenceNumber = sequenceNumber;

        serializeTelemetryFrame (header, eventImpulsePayload.data(), (uint32_t) numEvents, frameScratch);

        if (auto* buffer = hub.getFrameBufferBySlot (slotIndex, TelemetryFrameType::EventImpulse))
            buffer->publish (frameScratch.data(), frameScratch.size());
    }

    void AnalysisThread::publishRollingHistory (size_t slotIndex, const float* samples, int numSamples, uint64_t totalPushed, const TapSettings& settings)
    {
        // design/Visualization/Scope1.png: "from a few milliseconds to tens
        // of seconds" - clamped here, not trusted from the UI, since this is
        // what actually sizes columnDuration below; a 0 or negative request
        // (nothing has ever set it, or a bad value slipped through) would
        // divide by zero or seal every column instantly otherwise.
        const auto window = std::clamp (settings.historyWindowSeconds, minHistoryWindowSeconds, maxHistoryWindowSeconds);
        const auto columnDuration = (double) window / (double) maxHistoryColumns;

        const auto base = slotIndex * (size_t) maxHistoryColumns;
        auto* lo = historyColumnLo.data() + base;
        auto* hi = historyColumnHi.data() + base;
        auto& head = historyHeadBySlot[slotIndex];
        auto& elapsed = historyElapsedInColumnBySlot[slotIndex];

        // The window changed (including "never configured yet", 0 !=
        // anything real) - start over cleanly rather than publish a ring
        // whose older columns were sealed under a different columnDuration,
        // which would read as a nonsensical mixed time-scale. "Changed"
        // compares the CLAMPED value, so a sub-minHistoryWindowSeconds
        // request that gets clamped to the same floor twice in a row is not
        // treated as a change.
        const auto windowJustChanged = historyConfiguredWindowBySlot[slotIndex] != window;
        if (windowJustChanged)
        {
            historyConfiguredWindowBySlot[slotIndex] = window;
            std::fill (lo, lo + maxHistoryColumns, std::numeric_limits<float>::quiet_NaN());
            std::fill (hi, hi + maxHistoryColumns, std::numeric_limits<float>::quiet_NaN());
            head = 0;
            elapsed = 0.0;
        }

        // Tap::readLatest() returns "the most recent window currently in
        // the ring", not a delta since the last call — scanning only the
        // genuinely NEW tail (the same `totalPushed`-vs-last-seen technique
        // publishEventImpulse already established, for the identical
        // reason: re-folding already-processed samples would double-count
        // real time and seal columns too fast).
        const auto alreadyScanned = historyLastScannedTotalBySlot[slotIndex];
        const auto newSinceLastScan = totalPushed > alreadyScanned
                                           ? (int) std::min<uint64_t> (totalPushed - alreadyScanned, (uint64_t) numSamples)
                                           : 0;
        historyLastScannedTotalBySlot[slotIndex] = totalPushed;
        const auto scanStart = numSamples - newSinceLastScan;

        // Nothing NEW this drain, and the ring wasn't just reset — leave
        // whatever was last published alone rather than republish an
        // identical frame under a new sequenceNumber (publishEventImpulse's
        // own "nothing to add" economy). A just-reset ring DOES still
        // publish once even with zero new samples, so a window-length edit
        // clears the displayed trace immediately instead of leaving the
        // previous (now stale) one on screen until real data arrives.
        if (newSinceLastScan == 0 && ! windowJustChanged)
            return;

        const auto dt = 1.0 / sampleRate;

        for (int i = scanStart; i < numSamples; ++i)
        {
            const auto s = samples[i];

            if (std::isnan (lo[head])) // first sample ever folded into this column
                lo[head] = hi[head] = s;
            else
            {
                lo[head] = std::min (lo[head], s);
                hi[head] = std::max (hi[head], s);
            }

            elapsed += dt;

            // A `while`, not an `if`: a short window (columnDuration below
            // one sample period, the "a few milliseconds" end of the
            // range) can seal more than one column per sample. Each newly
            // opened column is seeded with THIS sample's value (not left at
            // NaN) so a column boundary never reads as a gap - the same
            // "carry the value across the seam" choice publishOscilloscope
            // doesn't need (it recomputes its whole window every drain) but
            // an incrementally-built ring does.
            while (elapsed >= columnDuration)
            {
                elapsed -= columnDuration;
                head = (head + 1) % maxHistoryColumns;
                lo[head] = hi[head] = s;
            }
        }

        // Linearize the ring into oldest-first order for the wire, same
        // (lo, hi) interleaving publishOscilloscope's own payload already
        // uses - the UI's existing Oscilloscope-shaped reader needs no
        // changes to draw this. `head` is the NEWEST column (still
        // in-progress); the oldest is the very next slot after it.
        const auto oldestIndex = (head + 1) % maxHistoryColumns;
        for (int column = 0; column < maxHistoryColumns; ++column)
        {
            const auto ringIndex = (oldestIndex + column) % maxHistoryColumns;
            historyPayload[(size_t) column * 2] = lo[ringIndex];
            historyPayload[(size_t) column * 2 + 1] = hi[ringIndex];
        }

        TelemetryFrameHeader header;
        header.tapId = (uint32_t) slotIndex;
        header.frameType = TelemetryFrameType::RollingHistory;
        header.sampleRate = (float) sampleRate;
        header.sequenceNumber = sequenceNumber;

        serializeTelemetryFrame (header, historyPayload.data(), (uint32_t) historyPayload.size(), frameScratch);

        if (auto* buffer = hub.getFrameBufferBySlot (slotIndex, TelemetryFrameType::RollingHistory))
            buffer->publish (frameScratch.data(), frameScratch.size());
    }
}
