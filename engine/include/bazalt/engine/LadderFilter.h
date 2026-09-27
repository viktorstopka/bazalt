#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

namespace bazalt::engine
{
    /** A 4-pole zero-delay-feedback (ZDF/TPT) Moog-style ladder — the one
        primitive NODE_CATALOG.md itself flags "Native: numerically
        delicate." Closed-form, non-iterative (CLAUDE.md rule 2: the audio
        thread must never do unbounded work, and a Newton-style implicit
        solve has no guaranteed iteration count) — the standard TPT
        derivation for a chain of 4 identical one-pole sections with global
        feedback, worked out below because it's the one place in this
        codebase doing it, not copied from another source file:

        Each stage is a trapezoidal ("TPT") one-pole: `v = G*(x-z)`,
        `y = v+z`, `z_new = y+v`, `G = g/(1+g)`, `g = tan(pi*fc/fs)` — the
        same prewarped-tangent coefficient every TPT filter in this codebase
        uses (`SvfFilter.h`'s own JUCE-backed equivalent). `y` is linear in
        `x` (`y = G*x + (1-G)*z`), so chaining 4 of them makes the WHOLE
        ladder's 4th-stage output linear in the loop's own input `u`:
        `y4 = G^4*u + S4`, where `S4` (computable from last sample's 4
        states alone) is what `y4` would be if `u` were 0. Since
        `u = x - k*y4` (the resonance feedback), substituting gives
        `u = (x - k*S4) / (1 + k*G^4)` — one division, no iteration, exact
        for the linear core.

        **Resonance is intentionally kept purely linear and clamped just
        below the literal self-oscillation boundary** (`k = resonance*3.999`,
        never the textbook `4.0`) rather than putting a saturating
        nonlinearity inside the feedback loop. A true nonlinear-feedback
        ZDF solve is transcendental (no closed form) and needs either Newton
        iteration or a frozen-nonlinearity approximation from the previous
        sample — both real added complexity this first implementation
        deliberately doesn't take on. The clamp guarantees the loop can
        never literally diverge for ANY resonance in [0,1], while a
        4th-order system this close to its stability boundary rings for a
        very long time after any excitation — audibly indistinguishable
        from "self-oscillating" for practical patching, and the resonant
        gain at cutoff grows without bound as resonance approaches 1 (the
        block-size-invariance and analytical tests below measure exactly
        that growth). **`drive` is a separate, real nonlinearity** — `tanh`
        applied to the SELECTED output only, after the linear loop is fully
        solved, not per-stage. This is a documented, deliberate
        simplification relative to a hardware-accurate ladder (which
        saturates inside every stage); it's what `LadderFilterNode.h`
        exposes as `drive`, and it's real, audible, correctly-implemented
        character — just not claimed to be more than what it is.

        `mode` derives highpass/bandpass from the same 4 stage outputs
        (`LadderFilterNode.h`'s own documented design call, the catalog
        names the modes but not how to derive them from a ladder's specific
        internal structure): **lowpass** is the `poles`-selected stage
        output directly; **highpass** is `u` (the loop's own input) minus
        that stage output — the same "input minus lowpass = highpass"
        identity `filter.onepole`'s own free highpass output already uses,
        generalised to whichever pole count is selected; **bandpass** is
        the difference between the `poles`-selected stage and the one
        before it (a classic "difference of two lowpasses" bandpass
        approximation), falling back to the highpass identity when
        `poles == 1` (there is no earlier stage to difference against).
    */
    class LadderFilter
    {
    public:
        enum class Mode { Lowpass, Highpass, Bandpass };

        void prepare (double newSampleRate) noexcept
        {
            sampleRate = newSampleRate;
            cachedCutoffHz = -1.0f;
        }

        void reset() noexcept { z.fill (0.0f); }

        void setPoles (int polesIn) noexcept { poles = juce::jlimit (1, 4, polesIn); }
        void setMode (Mode modeIn) noexcept { mode = modeIn; }

        void setCutoffFrequency (float cutoffHz) noexcept
        {
            if (cutoffHz == cachedCutoffHz || sampleRate <= 0.0)
                return;

            cachedCutoffHz = cutoffHz;
            const auto nyquist = sampleRate * 0.5;
            const auto clamped = juce::jlimit (1.0, nyquist * 0.98, (double) cutoffHz);
            const auto g = std::tan (juce::MathConstants<double>::pi * clamped / sampleRate);
            gCoefficient = (float) (g / (1.0 + g));
        }

        /** `resonance01` is the catalog's own 0-1 range; self-oscillation
            sits at 1 (see the class comment on why it's clamped just under
            the literal critical value rather than exactly at it).
        */
        void setResonance (float resonance01) noexcept
        {
            feedbackGain = juce::jlimit (0.0f, 1.0f, resonance01) * 3.999f;
        }

        float processSample (float x, float driveLinear) noexcept
        {
            const auto g2 = gCoefficient * gCoefficient;
            const auto g3 = g2 * gCoefficient;
            const auto g4 = g3 * gCoefficient;
            const auto oneMinusG = 1.0f - gCoefficient;

            // S4: what y4 would be if this sample's loop input (u) were 0 -
            // purely a function of LAST sample's 4 states (y_i = G*x_i +
            // (1-G)*z_i per stage, chained: each earlier state's contribution
            // to y4 picks up one more power of G on its way through the
            // remaining stages, but only ONE factor of (1-G), from its own
            // stage - not a compounding one).
            const auto s4 = oneMinusG * (g3 * z[0] + g2 * z[1] + gCoefficient * z[2] + z[3]);

            const auto u = (x - feedbackGain * s4) / (1.0f + feedbackGain * g4);

            std::array<float, 4> y {};
            auto stageInput = u;
            for (int i = 0; i < 4; ++i)
            {
                const auto v = gCoefficient * (stageInput - z[(size_t) i]);
                y[(size_t) i] = v + z[(size_t) i];
                z[(size_t) i] = y[(size_t) i] + v;
                stageInput = y[(size_t) i];
            }

            const auto selected = y[(size_t) (poles - 1)];
            const auto previous = poles >= 2 ? y[(size_t) (poles - 2)] : u;

            float result = selected; // Lowpass
            if (mode == Mode::Highpass)
                result = u - selected;
            else if (mode == Mode::Bandpass)
                result = previous - selected;

            return std::tanh (driveLinear * result);
        }

    private:
        double sampleRate = 44100.0;
        float cachedCutoffHz = -1.0f;
        float gCoefficient = 0.0f;
        float feedbackGain = 0.0f;
        int poles = 4;
        Mode mode = Mode::Lowpass;
        std::array<float, 4> z {};
    };
}
