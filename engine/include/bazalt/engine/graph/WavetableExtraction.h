#pragma once

#include "bazalt/engine/AudioAnalysis.h"
#include "bazalt/engine/graph/SampleData.h"
#include "bazalt/engine/graph/WavetableData.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace bazalt::engine
{
    /** A sample made into a wavetable (wiki/plans/Sampling.md §3): the
        Wavetable factory's "From sample…", and data.wavetable's live Capture.
        Every result is an ordinary table of **harmonics** keyframes — editable,
        morphable, one cycle each — placed evenly along the sound, so the
        table's Frame *is* time in the original: sweeping it replays the
        sound's change at any pitch.

        - **Cycles** (pitched sounds): the fundamental is found (the
          PitchTracker), one cycle is cut at an upward zero crossing near each
          frame's moment, resampled and measured exactly (amplitude and phase
          per harmonic), and every frame is turned to the first one's phase so
          they morph without cancelling.
        - **Harmonics**: the same measurement, phases dropped — the smoothest
          morphs, a softer copy of the original's shape.
        - **Texture** (unpitched sounds): the spectrum around each moment with
          one fixed random phase per harmonic — the colour of the sound in one
          cycle; interesting rather than faithful.
        - **Cycle file**: a single-cycle file (AKWF and friends), or Serum-style
          2048-sample frames back to back — each cycle becomes a keyframe.

        Each frame is normalised (its loudness belongs to an envelope; the
        table holds timbre). Message / worker thread. */
    namespace wavetableExtraction
    {
        enum class Method { Cycles, Harmonics, Texture, CycleFile };

        struct Options
        {
            Method method = Method::Cycles;
            int frames = 16;
            double start = 0.0, end = 0.0; // seconds; end 0: the end of the sample
            int maxHarmonics = 256;
            uint32_t seed = 1;
        };

        struct Result
        {
            bool success = false;
            juce::String errorMessage;
            WavetableDocument document;
            float pitch = std::numeric_limits<float>::quiet_NaN(); // the sound's note (MIDI), when it has one
        };

        constexpr int analysisSize = 2048; // = CurveView::tableSize: harmonic h of a cycle is bin h

        inline std::vector<float> monoRegion (const SampleSource& source, double start, double end)
        {
            const auto first = juce::jlimit (0, juce::jmax (0, source.frames - 1), (int) std::lround (start * source.sampleRate));
            const auto last = end > 0.0 ? juce::jlimit (first + 1, source.frames, (int) std::lround (end * source.sampleRate)) : source.frames;
            std::vector<float> out ((size_t) juce::jmax (0, last - first));
            for (int f = first; f < last; ++f)
            {
                float sum = 0.0f;
                for (int c = 0; c < source.channels; ++c)
                    sum += source.at (f, c);
                out[(size_t) (f - first)] = sum / (float) source.channels;
            }
            return out;
        }

        /** The sound's fundamental (Hz): the median confident pitch; 0 for none. */
        inline double fundamental (const std::vector<float>& x, double sampleRate, float& noteOut)
        {
            noteOut = std::numeric_limits<float>::quiet_NaN();
            PitchTracker tracker;
            tracker.prepare (sampleRate);
            tracker.setRange (24.0f, 108.0f);
            std::vector<float> notes;
            for (const auto v : x)
                if (tracker.push (v) && tracker.getConfidence() > 0.85f)
                    notes.push_back (tracker.getNote());
            if (notes.size() < 4)
                return 0.0;
            std::nth_element (notes.begin(), notes.begin() + (long) notes.size() / 2, notes.end());
            noteOut = notes[notes.size() / 2];
            return 440.0 * std::exp2 ((noteOut - 69.0) / 12.0);
        }

        inline float sampleAt (const std::vector<float>& x, double position)
        {
            const auto i = (int) std::floor (position);
            const auto t = (float) (position - (double) i);
            const auto at = [&x] (int k) { return k >= 0 && k < (int) x.size() ? x[(size_t) k] : 0.0f; };
            const auto y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
            const auto c1 = 0.5f * (y2 - y0);
            const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * t + c2) * t + c1) * t + y1;
        }

        /** Amplitude and phase (cycles, for the sine basis harmonics keyframes
            use) of harmonics 1..count of `analysisSize` samples. */
        inline void measure (std::vector<float>& cycle, int count, std::vector<float>& amplitudes, std::vector<float>& phases)
        {
            static_assert (analysisSize == 2048, "the FFT order below");
            juce::dsp::FFT fft (11);
            cycle.resize ((size_t) analysisSize * 2, 0.0f);
            fft.performRealOnlyForwardTransform (cycle.data(), true);
            amplitudes.assign ((size_t) count, 0.0f);
            phases.assign ((size_t) count, 0.0f);
            for (int h = 1; h <= count; ++h)
            {
                const auto re = (double) cycle[(size_t) h * 2], im = (double) cycle[(size_t) h * 2 + 1];
                amplitudes[(size_t) h - 1] = (float) (2.0 * std::sqrt (re * re + im * im) / analysisSize);
                // x = A cos(2 pi h t + theta) = A sin(2 pi (h t + theta / 2 pi + 1/4))
                auto p = std::atan2 (im, re) / juce::MathConstants<double>::twoPi + 0.25;
                p -= std::floor (p);
                phases[(size_t) h - 1] = (float) p;
            }
        }

        /** Scales a frame so its cycle peaks at 1 (and no harmonic passes 1). */
        inline void normaliseFrame (WavetableKeyframe& frame)
        {
            auto copy = frame;
            float peak = 0.0f, largest = 0.0f;
            {
                // renderCycle scales down only, so measure the raw sum.
                constexpr auto n = CurveView::tableSize;
                std::vector<double> raw ((size_t) n, 0.0);
                for (size_t h = 0; h < copy.amplitudes.size(); ++h)
                {
                    const auto a = (double) copy.amplitudes[h];
                    largest = juce::jmax (largest, std::abs (copy.amplitudes[h]));
                    if (std::abs (a) < 1.0e-7)
                        continue;
                    const auto phase = h < copy.phases.size() ? (double) copy.phases[h] : 0.0;
                    for (int i = 0; i < n; ++i)
                        raw[(size_t) i] += a * std::sin (juce::MathConstants<double>::twoPi * ((double) (h + 1) * i / n + phase));
                }
                for (const auto v : raw)
                    peak = juce::jmax (peak, (float) std::abs (v));
            }
            if (peak < 1.0e-6f)
                return;
            auto scale = 1.0f / peak;
            if (largest * scale > 1.0f)
                scale = 1.0f / largest;
            for (auto& a : frame.amplitudes)
                a *= scale;
        }

        inline WavetableDocument documentOf (std::vector<WavetableKeyframe> frames)
        {
            WavetableDocument doc;
            const auto n = (int) frames.size();
            for (int k = 0; k < n; ++k)
                frames[(size_t) k].position = n > 1 ? (float) k / (float) (n - 1) : 0.0f;
            doc.keyframes = std::move (frames);
            doc.normalise();
            return doc;
        }

        /** The cycle length a file of single cycles uses: the whole file when it
            is short, 2048 for back-to-back frames; 0 when it isn't one. */
        inline int cycleFileLength (const SampleSource& source)
        {
            if (source.frames >= 16 && source.frames <= 4096 && source.frames != analysisSize * 2)
                return source.frames;
            if (source.frames >= analysisSize * 2 && source.frames % analysisSize == 0 && source.frames / analysisSize <= 256)
                return analysisSize;
            return 0;
        }

        /** What "Auto" picks for a sample: a cycle file when it is one, Cycles
            when it has a pitch, Texture otherwise. */
        inline Method suggestMethod (const SampleSource& source)
        {
            if (cycleFileLength (source) > 0)
                return Method::CycleFile;
            float note = 0.0f;
            return fundamental (monoRegion (source, 0.0, juce::jmin (source.seconds(), 4.0)), source.sampleRate, note) > 0.0 ? Method::Cycles
                                                                                                                          : Method::Texture;
        }

        inline Result fromSample (const SampleSource& source, const Options& options)
        {
            Result result;
            const auto frameCount = juce::jlimit (1, 256, options.frames);
            const auto x = monoRegion (source, options.start, options.end);
            if (x.size() < 64)
            {
                result.errorMessage = "That part of the sample is too short";
                return result;
            }

            std::vector<WavetableKeyframe> frames;
            std::vector<float> cycle, amplitudes, phases;

            if (options.method == Method::CycleFile)
            {
                const auto length = cycleFileLength (source);
                if (length == 0)
                {
                    result.errorMessage = "Not a single-cycle or 2048-per-frame wavetable file";
                    return result;
                }
                const auto cycles = juce::jmax (1, (int) x.size() / length);
                const auto take = juce::jmin (cycles, frameCount);
                const auto count = juce::jmin (options.maxHarmonics, length / 2 - 1);
                for (int k = 0; k < take; ++k)
                {
                    const auto index = take > 1 ? (int) std::lround ((double) k * (cycles - 1) / (take - 1)) : 0;
                    cycle.assign ((size_t) analysisSize, 0.0f);
                    for (int i = 0; i < analysisSize; ++i)
                        cycle[(size_t) i] = sampleAt (x, (double) index * length + (double) i * length / analysisSize);
                    measure (cycle, count, amplitudes, phases);
                    frames.push_back (WavetableKeyframe::ofHarmonics (0.0f, amplitudes, phases));
                }
            }
            else if (options.method == Method::Texture)
            {
                juce::Random random ((juce::int64) options.seed);
                std::vector<float> fixedPhases ((size_t) options.maxHarmonics);
                for (auto& p : fixedPhases)
                    p = random.nextFloat();
                const auto count = juce::jmin (options.maxHarmonics, analysisSize / 2 - 1);
                for (int k = 0; k < frameCount; ++k)
                {
                    const auto centre = (double) x.size() * ((double) k + 0.5) / frameCount;
                    cycle.assign ((size_t) analysisSize, 0.0f);
                    for (int i = 0; i < analysisSize; ++i)
                    {
                        const auto hann = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / analysisSize);
                        const auto at = (int) centre - analysisSize / 2 + i;
                        cycle[(size_t) i] = at >= 0 && at < (int) x.size() ? x[(size_t) at] * (float) hann : 0.0f;
                    }
                    measure (cycle, count, amplitudes, phases);
                    frames.push_back (WavetableKeyframe::ofHarmonics (0.0f, amplitudes,
                                                                      std::vector<float> (fixedPhases.begin(), fixedPhases.begin() + count)));
                }
            }
            else
            {
                const auto hz = fundamental (x, source.sampleRate, result.pitch);
                if (hz <= 0.0)
                {
                    result.errorMessage = "No clear pitch in that part - try Texture";
                    return result;
                }
                const auto period = source.sampleRate / hz;
                if ((double) x.size() < 4.0 * period)
                {
                    result.errorMessage = "That part is too short for its pitch";
                    return result;
                }
                const auto count = juce::jmax (1, juce::jmin (options.maxHarmonics, (int) (period / 2.0) - 1));
                double firstPhase = 0.0;
                for (int k = 0; k < frameCount; ++k)
                {
                    // An upward zero crossing near this frame's moment, to the sample's fraction.
                    const auto centre = juce::jlimit (period, (double) x.size() - 2.0 * period - 1.0,
                                                      (double) x.size() * ((double) k + 0.5) / frameCount - period / 2.0);
                    auto start = centre;
                    auto best = std::numeric_limits<double>::max();
                    for (auto i = (int) (centre - period); i < (int) (centre + period); ++i)
                        if (i >= 0 && i + 1 < (int) x.size() && x[(size_t) i] < 0.0f && x[(size_t) i + 1] >= 0.0f)
                        {
                            const auto at = i + (double) (-x[(size_t) i] / (x[(size_t) i + 1] - x[(size_t) i]));
                            if (std::abs (at - centre) < best)
                            {
                                best = std::abs (at - centre);
                                start = at;
                            }
                        }
                    cycle.assign ((size_t) analysisSize, 0.0f);
                    for (int i = 0; i < analysisSize; ++i)
                        cycle[(size_t) i] = sampleAt (x, start + (double) i * period / analysisSize);
                    measure (cycle, count, amplitudes, phases);

                    // Turn the frame to the first one's phase: a shift of d cycles moves harmonic h by h * d.
                    if (k == 0)
                        firstPhase = phases.empty() ? 0.0 : phases[0];
                    else if (! phases.empty())
                    {
                        const auto d = firstPhase - phases[0];
                        for (size_t h = 0; h < phases.size(); ++h)
                        {
                            auto p = phases[h] + (double) (h + 1) * d;
                            phases[h] = (float) (p - std::floor (p));
                        }
                    }
                    frames.push_back (options.method == Method::Harmonics ? WavetableKeyframe::ofHarmonics (0.0f, amplitudes)
                                                                          : WavetableKeyframe::ofHarmonics (0.0f, amplitudes, phases));
                }
            }

            for (auto& frame : frames)
                normaliseFrame (frame);
            result.document = documentOf (std::move (frames));
            result.success = true;
            return result;
        }
    }
}
