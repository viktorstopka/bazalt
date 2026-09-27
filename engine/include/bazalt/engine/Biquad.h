#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

namespace bazalt::engine
{
    /** A Direct-Form-II-Transposed biquad (two state variables, numerically
        well-behaved under live coefficient changes — the standard choice for
        an audio-rate-modulatable EQ section) plus the Audio EQ Cookbook (Robert
        Bristow-Johnson) coefficient formulas for allpass, low/high shelf, and
        peaking (bell) responses. Shared by `filter.allpass`/`filter.shelf`/
        `filter.peak` (`AllpassFilterNode.h`/`ShelfFilterNode.h`/
        `PeakFilterNode.h`) — the same relationship `SvfFilter.h` has to
        `SvfFilterNode.h`: `tests/SvfFilterTests.cpp` already verifies that node
        against these exact cookbook formulas, so building this primitive
        against them is consistent with established project practice, not a
        new convention.

        Coefficients are computed once per call to a `set*` function below —
        callers (the Node subclasses) are the ones responsible for memoising
        (CLAUDE.md rule 6's "sample-rate-dependent constants derived in
        prepare(), never hardcoded" extends naturally to "recompute only when
        the live inputs actually changed" for anything trig-derived — the same
        `CoefficientCache` idiom `SlewNode.h` established), since only the
        caller knows whether `frequency`/`gain`/`q` are live-modulated this
        sample or unchanged from the last one.
    */
    class Biquad
    {
    public:
        void reset() noexcept { z1 = z2 = 0.0f; }

        float processSample (float x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        /** `a0` is assumed already divided out (every `set*` function below
            does this) — `processSample` never divides, so it stays cheap.
        */
        void setCoefficients (float b0In, float b1In, float b2In, float a1In, float a2In) noexcept
        {
            b0 = b0In;
            b1 = b1In;
            b2 = b2In;
            a1 = a1In;
            a2 = a2In;
        }

        /** RBJ cookbook allpass: unity gain at every frequency, phase
            rotation only — `q` shapes how quickly the phase rotates through
            180° around `frequencyHz` (higher `q` = a sharper rotation).
        */
        void setAllpass (double sampleRate, float frequencyHz, float q) noexcept
        {
            const auto w0 = angularFrequency (sampleRate, frequencyHz);
            const auto cosw0 = std::cos (w0);
            const auto alpha = std::sin (w0) / (2.0 * (double) juce::jmax (0.01f, q));

            const auto a0 = 1.0 + alpha;
            setCoefficients ((float) ((1.0 - alpha) / a0),
                              (float) ((-2.0 * cosw0) / a0),
                              (float) ((1.0 + alpha) / a0),
                              (float) ((-2.0 * cosw0) / a0),
                              (float) ((1.0 - alpha) / a0));
        }

        /** RBJ cookbook peaking (bell) EQ: `gainLinear` boosts/cuts a band
            around `frequencyHz` whose width is set by `q`; `gainLinear == 1`
            (0 dB) is a true bypass (`alpha`'s `A` factor becomes 1, and the
            filter's transfer function reduces to unity by construction).
        */
        void setPeak (double sampleRate, float frequencyHz, float q, float gainLinear) noexcept
        {
            const auto w0 = angularFrequency (sampleRate, frequencyHz);
            const auto cosw0 = std::cos (w0);
            const auto a = std::sqrt ((double) juce::jmax (1.0e-6f, gainLinear)); // RBJ's "A": the PEAK linear-amplitude gain is A^2, so A = sqrt(desired gain)
            const auto alpha = std::sin (w0) / (2.0 * (double) juce::jmax (0.01f, q));

            const auto a0 = 1.0 + alpha / a;
            setCoefficients ((float) ((1.0 + alpha * a) / a0),
                              (float) ((-2.0 * cosw0) / a0),
                              (float) ((1.0 - alpha * a) / a0),
                              (float) ((-2.0 * cosw0) / a0),
                              (float) ((1.0 - alpha / a) / a0));
        }

        /** RBJ cookbook low/high shelf. `slope01` (0-1, `filter.shelf`'s own
            `slope` port) maps onto RBJ's shelf slope parameter `S` as
            `0.1 + slope01*0.9` — `S = 1` is RBJ's own "steepest slope without
            overshoot" reference point, `S` below that is gentler. Not
            specified further by NODE_CATALOG.md's `slope : float·Unipolar`
            entry; this is this node's own documented design call for what
            the port actually does.
        */
        void setShelf (double sampleRate, float frequencyHz, float gainLinear, float slope01, bool isHighShelf) noexcept
        {
            const auto w0 = angularFrequency (sampleRate, frequencyHz);
            const auto cosw0 = std::cos (w0);
            const auto sinw0 = std::sin (w0);
            const auto a = std::sqrt ((double) juce::jmax (1.0e-6f, gainLinear)); // same A = sqrt(desired gain) as setPeak() — see its comment
            const auto s = 0.1 + (double) juce::jlimit (0.0f, 1.0f, slope01) * 0.9;
            const auto alpha = sinw0 / 2.0 * std::sqrt ((a + 1.0 / a) * (1.0 / s - 1.0) + 2.0);
            const auto twoSqrtAAlpha = 2.0 * std::sqrt (a) * alpha;

            if (isHighShelf)
            {
                const auto a0 = (a + 1.0) - (a - 1.0) * cosw0 + twoSqrtAAlpha;
                setCoefficients ((float) (a * ((a + 1.0) + (a - 1.0) * cosw0 + twoSqrtAAlpha) / a0),
                                  (float) (-2.0 * a * ((a - 1.0) + (a + 1.0) * cosw0) / a0),
                                  (float) (a * ((a + 1.0) + (a - 1.0) * cosw0 - twoSqrtAAlpha) / a0),
                                  (float) (2.0 * ((a - 1.0) - (a + 1.0) * cosw0) / a0),
                                  (float) (((a + 1.0) - (a - 1.0) * cosw0 - twoSqrtAAlpha) / a0));
            }
            else
            {
                const auto a0 = (a + 1.0) + (a - 1.0) * cosw0 + twoSqrtAAlpha;
                setCoefficients ((float) (a * ((a + 1.0) - (a - 1.0) * cosw0 + twoSqrtAAlpha) / a0),
                                  (float) (2.0 * a * ((a - 1.0) - (a + 1.0) * cosw0) / a0),
                                  (float) (a * ((a + 1.0) - (a - 1.0) * cosw0 - twoSqrtAAlpha) / a0),
                                  (float) (-2.0 * ((a - 1.0) + (a + 1.0) * cosw0) / a0),
                                  (float) (((a + 1.0) + (a - 1.0) * cosw0 - twoSqrtAAlpha) / a0));
            }
        }

    private:
        static double angularFrequency (double sampleRate, float frequencyHz) noexcept
        {
            // Clamp comfortably below Nyquist: RBJ's trig-based derivation is
            // undefined/unstable right at or past it, and a live-modulated
            // frequency port can genuinely reach there.
            const auto nyquist = sampleRate * 0.5;
            const auto clamped = juce::jlimit (1.0, nyquist * 0.999, (double) frequencyHz);
            return 2.0 * juce::MathConstants<double>::pi * clamped / sampleRate;
        }

        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
    };
}
