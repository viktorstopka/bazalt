#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace bazalt::engine
{
    /** Hearing a played instrument (wiki/plans/GuitarInput.md): the shared DSP
        behind analysis.pitch, analysis.onset, analysis.notes and
        analysis.phase. Plain classes, no Node — a node owns one per channel.
        Everything is allocated in prepare(); process() never allocates. */

    inline double noteToFrequency (double note) noexcept { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }
    inline double frequencyToNote (double hz) noexcept { return 69.0 + 12.0 * std::log2 (hz / 440.0); }

    /** Monophonic pitch tracking with YIN (de Cheveigné & Kawahara 2002): the
        cumulative-mean-normalised difference function, the first dip below a
        threshold, parabolic interpolation. Searches only the note range it is
        given — on one guitar string that range is narrow, which makes it both
        several times faster and nearly free of octave errors. The estimate is
        refreshed every `hop` samples from the most recent two periods of the
        lowest note in range, so the delay is about two periods of that note
        (≈ 6–12 ms on a high E string). */
    class PitchTracker
    {
    public:
        static constexpr double lowestTrackableHz = 30.0;

        void prepare (double sampleRateIn)
        {
            sampleRate = sampleRateIn;
            maxTauCap = (int) std::ceil (sampleRate / lowestTrackableHz) + 2;
            history.assign ((size_t) (2 * maxTauCap + 4096), 0.0f);
            window.assign ((size_t) (2 * maxTauCap + 2), 0.0f);
            difference.assign ((size_t) (maxTauCap + 2), 0.0f);
            setRange (lowNote, highNote);
            reset();
        }

        void reset() noexcept
        {
            std::fill (history.begin(), history.end(), 0.0f);
            writeIndex = 0;
            samplesUntilAnalysis = 0;
            filled = 0;
            confidence = 0.0f;
            fresh = false;
        }

        /** MIDI note numbers. Safe on the audio thread. */
        void setRange (float lowNoteIn, float highNoteIn) noexcept
        {
            lowNote = juce::jlimit (12.0f, 120.0f, std::min (lowNoteIn, highNoteIn));
            highNote = juce::jlimit (lowNote + 1.0f, 127.0f, std::max (lowNoteIn, highNoteIn));
            if (sampleRate <= 0.0)
                return;
            maxTau = juce::jlimit (4, maxTauCap - 2, (int) std::ceil (sampleRate / noteToFrequency (lowNote - 0.5)));
            minTau = juce::jlimit (2, maxTau - 1, (int) std::floor (sampleRate / noteToFrequency (highNote + 0.5)));
            hop = juce::jlimit (32, 256, maxTau / 4);
        }

        void setThreshold (float t) noexcept { threshold = juce::jlimit (0.02f, 0.6f, t); }
        void setSilenceGate (float linear) noexcept { silenceGate = linear; }

        /** Feeds one sample; returns true when a new estimate is ready. */
        bool push (float x) noexcept
        {
            const auto size = (int) history.size();
            history[(size_t) writeIndex] = x;
            writeIndex = (writeIndex + 1) % size;
            filled = std::min (filled + 1, size);
            if (--samplesUntilAnalysis > 0)
                return false;
            samplesUntilAnalysis = hop;
            analyse();
            fresh = true;
            return true;
        }

        float getFrequency() const noexcept { return frequency; }
        float getNote() const noexcept { return note; }
        float getConfidence() const noexcept { return confidence; }
        int getLatencySamples() const noexcept { return 2 * maxTau; }

    private:
        void analyse() noexcept
        {
            const auto length = 2 * maxTau; // window W = maxTau, plus the largest lag
            if (filled < length)
            {
                confidence = 0.0f;
                return;
            }

            const auto size = (int) history.size();
            auto start = writeIndex - length;
            if (start < 0)
                start += size;
            double energy = 0.0;
            for (int i = 0; i < length; ++i)
            {
                const auto v = history[(size_t) ((start + i) % size)];
                window[(size_t) i] = v;
                energy += (double) v * v;
            }
            if (std::sqrt (energy / length) < silenceGate)
            {
                confidence = 0.0f;
                return;
            }

            const auto W = maxTau;
            const auto* x = window.data();
            difference[0] = 1.0f;
            double runningSum = 0.0;
            for (int tau = 1; tau <= maxTau; ++tau)
            {
                double d = 0.0;
                for (int j = 0; j < W; ++j)
                {
                    const auto delta = x[j] - x[j + tau];
                    d += (double) delta * delta;
                }
                runningSum += d;
                difference[(size_t) tau] = runningSum > 0.0 ? (float) (d * tau / runningSum) : 1.0f;
            }

            // The first dip below the threshold, followed down to its minimum;
            // else the deepest point in range.
            int best = -1;
            for (int tau = minTau; tau <= maxTau; ++tau)
            {
                if (difference[(size_t) tau] < threshold)
                {
                    while (tau + 1 <= maxTau && difference[(size_t) (tau + 1)] < difference[(size_t) tau])
                        ++tau;
                    best = tau;
                    break;
                }
            }
            if (best < 0)
            {
                best = minTau;
                for (int tau = minTau + 1; tau <= maxTau; ++tau)
                    if (difference[(size_t) tau] < difference[(size_t) best])
                        best = tau;
            }

            auto refined = (double) best;
            if (best > 1 && best < maxTau)
            {
                const auto a = difference[(size_t) (best - 1)], b = difference[(size_t) best], c = difference[(size_t) (best + 1)];
                const auto denominator = a - 2.0f * b + c;
                if (std::abs (denominator) > 1.0e-9f)
                    refined += juce::jlimit (-0.5, 0.5, (double) (0.5f * (a - c) / denominator));
            }

            confidence = juce::jlimit (0.0f, 1.0f, 1.0f - difference[(size_t) best]);
            frequency = (float) (sampleRate / refined);
            note = (float) frequencyToNote (frequency);
        }

        double sampleRate = 0.0;
        float lowNote = 40.0f, highNote = 88.0f, threshold = 0.15f, silenceGate = 0.001f;
        int maxTauCap = 0, maxTau = 4, minTau = 2, hop = 64;
        std::vector<float> history, window, difference;
        int writeIndex = 0, samplesUntilAnalysis = 0, filled = 0;
        float frequency = 0.0f, note = 0.0f, confidence = 0.0f;
        bool fresh = false;
    };

    /** A plucked or struck attack: a fast envelope jumping above a slow one.
        Fires once (then holds off for `holdMs`) when the fast envelope rises
        `sensitivityDb` above the slow one and is above the gate, so a new
        pluck on a still-ringing string is heard too. */
    class OnsetDetector
    {
    public:
        void prepare (double sampleRateIn) noexcept
        {
            sampleRate = sampleRateIn;
            fastAttack = coefficient (0.5);
            fastRelease = coefficient (15.0);
            slowCoefficient = coefficient (60.0);
            reset();
        }

        void reset() noexcept
        {
            fast = slow = 0.0;
            holdRemaining = attackRemaining = 0;
            strength = 0.0f;
        }

        void setSensitivityDb (float db) noexcept { ratio = std::pow (10.0, juce::jlimit (1.0f, 30.0f, db) / 20.0); }
        void setGateDb (float db) noexcept { gate = std::pow (10.0, juce::jlimit (-90.0f, 0.0f, db) / 20.0); }
        void setHoldMs (float ms) noexcept { holdSamples = (int) (juce::jlimit (5.0f, 2000.0f, ms) * 0.001 * sampleRate); }

        /** True on the sample an onset is heard. */
        bool push (float x) noexcept
        {
            const auto level = (double) std::abs (x);
            fast = level + (level > fast ? fastAttack : fastRelease) * (fast - level);
            const auto before = slow;
            slow = fast + slowCoefficient * (slow - fast);

            // The attack's strength is its peak over the first few ms, not the
            // level at the instant it was noticed (still rising then).
            if (attackRemaining > 0)
            {
                --attackRemaining;
                strength = std::max (strength, (float) juce::jlimit (0.0, 1.0, fast * 2.0));
            }

            if (holdRemaining > 0)
            {
                --holdRemaining;
                return false;
            }
            if (fast > gate && fast > (before + 1.0e-9) * ratio)
            {
                holdRemaining = holdSamples;
                attackRemaining = (int) (0.005 * sampleRate);
                strength = (float) juce::jlimit (0.0, 1.0, fast * 2.0);
                return true;
            }
            return false;
        }

        float getStrength() const noexcept { return strength; }
        float getLevel() const noexcept { return (float) fast; }

    private:
        double coefficient (double ms) const noexcept { return std::exp (-1.0 / (ms * 0.001 * sampleRate)); }

        double sampleRate = 48000.0, fast = 0.0, slow = 0.0;
        double fastAttack = 0.0, fastRelease = 0.0, slowCoefficient = 0.0;
        double ratio = 2.0, gate = 0.003;
        int holdSamples = 2880, holdRemaining = 0, attackRemaining = 0;
        float strength = 0.0f;
    };

    /** The phase of a played note, as a signal (0..1 per cycle) — so any
        shape can be played locked to the instrument's own period, with no
        note detection in between. Upward zero crossings of the fundamental
        (a low-pass that follows the measured frequency isolates it; hysteresis
        rejects noise) mark each period; between them the phase ramps by the
        smoothed period. `multiply / divide` rescales it: divide 2 is an octave
        down, multiply 3 divide 2 a fifth up. */
    class PhaseTracker
    {
    public:
        void prepare (double sampleRateIn) noexcept
        {
            sampleRate = sampleRateIn;
            envelopeRelease = std::exp (-1.0 / (0.05 * sampleRate));
            reset();
        }

        void reset() noexcept
        {
            lp1 = lp2 = previous = 0.0;
            envelope = 0.0;
            armed = false;
            period = 0.0;
            sinceCrossing = 0.0;
            cycle = 0;
            phase = 0.0f;
            setCutoff (1000.0);
        }

        void setRatio (int multiplyIn, int divideIn) noexcept
        {
            multiply = juce::jlimit (1, 16, multiplyIn);
            divide = juce::jlimit (1, 16, divideIn);
        }

        void setGateDb (float db) noexcept { gate = std::pow (10.0, juce::jlimit (-90.0f, 0.0f, db) / 20.0); }

        void push (float x) noexcept
        {
            // Two one-pole low-passes tuned to ~1.5x the measured fundamental.
            lp1 += cutoffAlpha * ((double) x - lp1);
            lp2 += cutoffAlpha * (lp1 - lp2);
            const auto y = lp2;
            const auto level = std::abs (y);
            envelope = level > envelope ? level : level + envelopeRelease * (envelope - level);

            sinceCrossing += 1.0;
            const auto hysteresis = 0.2 * envelope;
            if (y < -hysteresis)
                armed = true;
            if (armed && previous < 0.0 && y >= 0.0 && envelope > gate)
            {
                armed = false;
                const auto fraction = previous / (previous - y); // where between the two samples it crossed
                const auto measured = sinceCrossing - 1.0 + fraction;
                if (measured > 2.0)
                {
                    if (period <= 0.0 || measured < period * 0.55 || measured > period * 1.8)
                        period = measured; // a new note: take it as it is
                    else
                        period += 0.35 * (measured - period);
                    setCutoff (1.5 * sampleRate / period);
                }
                sinceCrossing = 1.0 - fraction;
                cycle = (cycle + 1) % divide;
            }
            previous = y;

            if (period > 0.0 && envelope > gate)
            {
                const auto within = std::min (0.9999, sinceCrossing / period);
                const auto scaled = ((double) cycle + within) * (double) multiply / (double) divide;
                phase = (float) (scaled - std::floor (scaled));
            }
        }

        float getPhase() const noexcept { return phase; }
        float getFrequency() const noexcept
        {
            return period > 0.0 && envelope > gate ? (float) (sampleRate / period * multiply / divide) : 0.0f;
        }

    private:
        void setCutoff (double hz) noexcept
        {
            hz = juce::jlimit (40.0, 0.45 * sampleRate, hz);
            cutoffAlpha = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * hz / sampleRate);
        }

        double sampleRate = 48000.0, cutoffAlpha = 0.1, envelopeRelease = 0.999;
        double lp1 = 0.0, lp2 = 0.0, previous = 0.0, envelope = 0.0, gate = 0.003;
        bool armed = false;
        double period = 0.0, sinceCrossing = 0.0;
        int cycle = 0, multiply = 1, divide = 1;
        float phase = 0.0f;
    };
}
