#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>

namespace bazalt::engine
{
    enum class OscillatorWaveform
    {
        Sine,
        Saw,
        Square,
        Triangle
    };

    /** Band-limited single-cycle shapes, evaluated statelessly from a read
        phase `t` in [0, 1) and the per-sample phase increment `dt`. Stateless
        on purpose: a Phase port offsets the READ point, so anything that
        integrated the waveform over time (the leaky-integrated triangle
        PolyBlepOscillator used until 2026-10-04) drifts whenever phase is
        modulated — and a stateless shape can be drawn exactly by a
        phase-locked preview from a parameter snapshot.

        Saw and square correct their jumps with PolyBLEP; triangle has no
        jumps, only corners, so it corrects them with PolyBLAMP (the
        integrated BLEP) — the 2-sample polynomial residuals that are the
        project's band-limiting floor (ARCHITECTURE.md §5).
    */
    namespace bandLimited
    {
        /** Residual for a unit step at t = 0 (the classic 2-sample PolyBLEP). */
        inline double blep (double t, double dt) noexcept
        {
            if (dt <= 0.0)
                return 0.0;
            if (t < dt)
            {
                t /= dt;
                return t + t - t * t - 1.0;
            }
            if (t > 1.0 - dt)
            {
                t = (t - 1.0) / dt;
                return t * t + t + t + 1.0;
            }
            return 0.0;
        }

        /** Residual for a slope change at t = 0 (the integral of blep);
            `0.5 * slopeChange * dt * blamp` corrects a corner whose slope
            changes by `slopeChange` per unit phase. */
        inline double blamp (double t, double dt) noexcept
        {
            if (dt <= 0.0)
                return 0.0;
            if (t < dt)
            {
                t = t / dt - 1.0;
                return -t * t * t / 3.0;
            }
            if (t > 1.0 - dt)
            {
                t = (t - 1.0) / dt + 1.0;
                return t * t * t / 3.0;
            }
            return 0.0;
        }

        inline double wrap (double t) noexcept { return t - std::floor (t); }

        inline double saw (double t, double dt) noexcept
        {
            return 2.0 * t - 1.0 - blep (t, dt); // rises -1 -> 1, drops by 2 at t = 0
        }

        /** High for t < width, low after; rising edge at 0, falling at width. */
        inline double square (double t, double dt, double width) noexcept
        {
            const auto naive = t < width ? 1.0 : -1.0;
            return naive + blep (t, dt) - blep (wrap (t - width), dt);
        }

        /** 1 at t = 0, -1 at t = 0.5: slope -4 then +4, so the corner at 0
            changes slope by -8 and the one at 0.5 by +8. This blamp's
            polynomial already carries a factor of 2, hence half of each
            slope change (verified numerically against the Nyquist-truncated
            Fourier series — SineOscillatorNodeTests.cpp). */
        inline double triangle (double t, double dt) noexcept
        {
            const auto naive = 4.0 * std::fabs (t - 0.5) - 1.0;
            return naive - 4.0 * dt * blamp (t, dt) + 4.0 * dt * blamp (wrap (t - 0.5), dt);
        }
    }


    namespace bandLimited
    {
        /** One cycle's value at read phase `t` — the single definition every
            oscillator renders AND its phase-locked preview draws, so the
            preview is what the node actually produces. */
        inline double evaluate (OscillatorWaveform shape, double t, double dt, double pulseWidth = 0.5) noexcept
        {
            switch (shape)
            {
                case OscillatorWaveform::Sine:     return std::sin (juce::MathConstants<double>::twoPi * t);
                case OscillatorWaveform::Saw:      return saw (t, dt);
                case OscillatorWaveform::Square:   return square (t, dt, std::clamp (pulseWidth, 0.01, 0.99));
                case OscillatorWaveform::Triangle: return triangle (t, dt);
            }
            return 0.0;
        }
    }
}
