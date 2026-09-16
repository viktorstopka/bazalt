#pragma once

#include <juce_dsp/juce_dsp.h>

namespace bazalt::engine
{
    enum class SvfFilterType
    {
        Lowpass,
        Bandpass,
        Highpass
    };

    /** Engine-facing wrapper around juce::dsp::StateVariableTPTFilter — a
        topology-preserving-transform (Zavalishin) state variable filter
        that solves its own zero-delay feedback internally, so it stays
        stable under audio-rate cutoff/resonance modulation
        (ARCHITECTURE.md §5). All future filter topologies follow the same
        TPT discretization approach; this wrapper exists so callers depend
        on bazalt::engine's own naming/lifecycle rather than JUCE's
        directly, and so a from-scratch implementation could later replace
        the backing type without touching callers.
    */
    class SvfFilter
    {
    public:
        void prepare (double sampleRate, uint32_t maximumBlockSize, uint32_t numChannels)
        {
            filter.prepare ({ sampleRate, maximumBlockSize, numChannels });
        }

        void reset() noexcept { filter.reset(); }

        void setType (SvfFilterType newType) noexcept
        {
            filter.setType (static_cast<juce::dsp::StateVariableTPTFilterType> (newType));
        }

        /** @param hz must stay below Nyquist — the caller owns clamping,
            since the sensible margin depends on how fast cutoff is being
            modulated. */
        void setCutoffFrequency (float hz) noexcept { filter.setCutoffFrequency (hz); }

        /** Resonance in JUCE's convention: 1/sqrt(2) (~0.7071) gives the
            standard 12 dB/octave (Butterworth Q) response; higher values
            increase the resonant peak. Must stay > 0. */
        void setResonance (float q) noexcept { filter.setResonance (q); }

        float processSample (int channel, float inputSample) noexcept
        {
            return filter.processSample (channel, inputSample);
        }

    private:
        juce::dsp::StateVariableTPTFilter<float> filter;
    };
}
