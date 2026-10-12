#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

namespace bazalt::engine
{
    /** The state-variable filter `filter.svf` runs (Zavalishin's topology-
        preserving transform in Andy Simper's formulation): stable under
        audio-rate cutoff/resonance modulation, all five responses from one
        step. Shared by `SvfFilterNode` and the EQ (`EqCurve.h`), so an EQ's
        cut and notch bands and the SVF nodes they unwrap into compute the
        same numbers. `resonance` is Q (0.707 = Butterworth). */
    struct SvfCore
    {
        struct Outputs
        {
            double low, band, high;
        };

        void reset() noexcept { ic1 = ic2 = 0.0; }

        /** Recomputes only when cutoff or resonance actually changed. */
        void setCoefficients (double sampleRate, float cutoff, float resonance) noexcept
        {
            if (cutoff == cachedCutoff && resonance == cachedResonance)
                return;
            cachedCutoff = cutoff;
            cachedResonance = resonance;
            const auto fc = juce::jlimit (1.0, sampleRate * 0.49, (double) cutoff);
            const auto g = std::tan (juce::MathConstants<double>::pi * fc / sampleRate);
            k = 1.0 / juce::jlimit (0.01, 100.0, (double) resonance);
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        /** Forgets the cached coefficients (a new sample rate). */
        void invalidate() noexcept { cachedCutoff = cachedResonance = -1.0f; }

        Outputs process (float input) noexcept
        {
            const auto x = (double) input;
            const auto v3 = x - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            return { v2, v1, x - k * v1 - v2 };
        }

        double ic1 = 0.0, ic2 = 0.0, a1 = 1.0, a2 = 0.0, a3 = 0.0, k = 1.414;
        float cachedCutoff = -1.0f, cachedResonance = -1.0f;
    };
}
