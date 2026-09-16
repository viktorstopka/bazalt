#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace bazalt::engine
{
    /** Every parameter and macro-mapped target must be smoothed before it
        reaches DSP (ARCHITECTURE.md §5) — this is the one helper all node
        parameters should route through, so "read the raw parameter inside
        process()" isn't a pattern that can quietly creep back in.
    */
    class SmoothedParameter
    {
    public:
        void prepare (double sampleRate, double rampLengthSeconds = 0.02) noexcept
        {
            smoothed.reset (sampleRate, rampLengthSeconds);
        }

        /** Jumps immediately to a value with no ramp, e.g. on voice start. */
        void reset (float value) noexcept
        {
            smoothed.setCurrentAndTargetValue (value);
        }

        void setTargetValue (float newValue) noexcept { smoothed.setTargetValue (newValue); }
        float getNextValue() noexcept { return smoothed.getNextValue(); }
        float getCurrentValue() const noexcept { return smoothed.getCurrentValue(); }
        float getTargetValue() const noexcept { return smoothed.getTargetValue(); }
        bool isSmoothing() const noexcept { return smoothed.isSmoothing(); }
        float skip (int numSamples) noexcept { return smoothed.skip (numSamples); }

    private:
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothed;
    };
}
