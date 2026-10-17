#pragma once

#include <juce_core/juce_core.h>
#include <charconv>
#include <cmath>

namespace bazalt::engine
{
    /** A float as the shortest decimal that reads back to the same float
        (wiki/plans/Release.md R0.3). 0.3f widened to double is
        0.300000011920929; this gives 0.3, so a typed value saves and shows as
        typed. Use it wherever a float is written to JSON. */
    inline double shortestDecimal (float value)
    {
        if (! std::isfinite (value))
            return (double) value;
        char text[32];
        const auto written = std::to_chars (text, text + sizeof (text), value);
        double result = 0.0;
        std::from_chars (text, written.ptr, result);
        return result;
    }

    inline juce::var exactFloatVar (float value) { return juce::var (shortestDecimal (value)); }

    /** JSON text with each number as short as reads back (JUCE's default
        writes 15 decimals: 261.63 becomes 261.629999999999995). Use it for
        every patch, snapshot and reply to the UI. */
    inline juce::String toJson (const juce::var& value, bool compact = true)
    {
        return juce::JSON::toString (value, juce::JSON::FormatOptions {}
                                                .withSpacing (compact ? juce::JSON::Spacing::singleLine : juce::JSON::Spacing::multiLine)
                                                .withMaxDecimalPlaces (0));
    }
}
