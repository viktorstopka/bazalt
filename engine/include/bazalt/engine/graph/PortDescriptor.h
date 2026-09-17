#pragma once

#include "bazalt/engine/graph/SignalType.h"
#include <juce_core/juce_core.h>
#include <optional>

namespace bazalt::engine
{
    /** UI-facing metadata for one port, fully decoupled from the DSP
        implementation (ARCHITECTURE.md §3.6) — enough for a future UI to
        build a socket without knowing the node's C++ type. `id` is a
        stable, hand-assigned string, never an index, never renamed once
        shipped.

        Fields below `type` were added in M7 (NODE_EDITOR.md §3) — all
        defaulted so every M1/M2 node's existing `{id, type}`
        aggregate-initialization still compiles unchanged.
    */
    struct PortDescriptor
    {
        juce::String id;
        SignalType type;

        juce::String label;           // display name; falls back to `id` in the UI if empty
        bool isPrimaryOutput = false; // drives Alt-drag Mix/Add/Multiply and horizontal-node
                                       // output indicators (NODE_EDITOR.md §7's "primary output")

        // Numeric-value metadata (meaningful for Control/Boolean ports that
        // render as the UI's Value/Integer/Modulation/Boolean palette
        // entries, NODE_EDITOR.md §5) — left at defaults for Audio/Event
        // ports, which don't have a "value".
        juce::String unit;
        std::optional<float> minValue;
        std::optional<float> maxValue;
        float defaultValue = 0.0f;
        bool isInteger = false;
        bool isLogScale = false;
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
