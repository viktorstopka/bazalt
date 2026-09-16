#pragma once

#include "bazalt/engine/graph/SignalType.h"
#include <juce_core/juce_core.h>

namespace bazalt::engine
{
    /** UI-facing metadata for one port, fully decoupled from the DSP
        implementation (ARCHITECTURE.md §3.6) — enough for a future UI to
        build a socket without knowing the node's C++ type. `id` is a
        stable, hand-assigned string, never an index, never renamed once
        shipped.
    */
    struct PortDescriptor
    {
        juce::String id;
        SignalType type;
    };

    /** UI-facing metadata for one parameter — enough to build a control
        without knowing the DSP. `id` follows the same stable-string rule
        as port/node type IDs.
    */
    struct ParameterDescriptor
    {
        juce::String id;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float defaultValue = 0.0f;
        float skew = 1.0f;
        juce::String unit;
        juce::String displayName;
    };
}
