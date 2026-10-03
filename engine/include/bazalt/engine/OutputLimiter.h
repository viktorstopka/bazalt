#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace bazalt::engine
{
    /** A real safety ceiling at the plugin's own final output stage — unlike
        `NanGuard` (which only replaces non-finite samples with silence, and
        has no concept of "too loud"), this actually caps peak level.

        Direct feedback: "getting my ears blown off once every few seconds"
        while testing patches — genuinely easy to do by accident once
        resonant feedback nodes (`resonator.comb`/`modal`/`string`/`plate`)
        are in the mix, and there was no safety net at all below the host's
        own fader. Belongs at the plugin level, unconditional, not something
        a patch has to remember to wire in itself (a real, separate,
        patchable `shape.clip` node — the catalog's own "the node you put in
        a feedback loop so a slider can't destroy a speaker" — is still real,
        useful, future work for mid-chain sound design; this is the
        always-on backstop underneath it).

        A standard peak-detecting limiter: fast attack (~1ms, so a sudden
        transient is caught before it's audible as a hard clip) and a
        slower release (~100ms, so gain recovery after a loud moment doesn't
        audibly "pump"/duck in an obviously artificial way). The ceiling
        itself (`-1dBFS`, not `0dBFS`) leaves a sliver of real headroom —
        standard practice, avoids inter-sample peaks right at the float
        range's own edge. A final hard clamp to `[-1, 1]` underneath the
        smoothed gain reduction is a deliberate backstop for a single-sample
        transient sharp enough that even a 1ms attack can't fully catch it
        in time — belt and suspenders, not redundant: the smoothed envelope
        protects against anything sustained; the hard clamp protects against
        the one isolated spike the envelope hasn't reacted to yet.
    */
    class OutputLimiter
    {
    public:
        static constexpr float ceiling = 0.891f; // ~ -1 dBFS

        void prepare (double sampleRate) noexcept
        {
            constexpr float attackSeconds = 0.001f;
            constexpr float releaseSeconds = 0.100f;
            attackCoeff = std::exp (-1.0f / (attackSeconds * (float) sampleRate));
            releaseCoeff = std::exp (-1.0f / (releaseSeconds * (float) sampleRate));
            gainReduction = 1.0f;
        }

        void reset() noexcept { gainReduction = 1.0f; }

        /** Must not allocate, lock, or block — called every block, on the
            audio thread, same as every other final-output-stage process
            (`NanGuard::process`'s own contract). Expects `buffer` to
            already be free of non-finite samples (run `NanGuard` first) —
            a NaN peak would otherwise poison `gainReduction`'s own IIR
            smoothing indefinitely (NaN propagates through every arithmetic
            op it ever touches again), not just for the one bad sample.
        */
        void process (juce::AudioBuffer<float>& buffer) noexcept
        {
            const auto numSamples = buffer.getNumSamples();
            const auto numChannels = buffer.getNumChannels();

            for (int i = 0; i < numSamples; ++i)
            {
                float peak = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    peak = juce::jmax (peak, std::fabs (buffer.getReadPointer (ch)[i]));

                const auto targetGain = peak > ceiling ? ceiling / peak : 1.0f;
                const auto coeff = targetGain < gainReduction ? attackCoeff : releaseCoeff;
                gainReduction = targetGain + coeff * (gainReduction - targetGain);

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto* data = buffer.getWritePointer (ch);
                    data[i] = juce::jlimit (-1.0f, 1.0f, data[i] * gainReduction);
                }
            }
        }

    private:
        float attackCoeff = 0.0f;
        float releaseCoeff = 0.0f;
        float gainReduction = 1.0f;
    };
}
