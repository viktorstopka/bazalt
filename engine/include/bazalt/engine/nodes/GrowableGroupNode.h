#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/PortGroups.h"

namespace bazalt::engine::nodes
{
    /** Shared base for the nodes that take "N of the same thing" through a
        growable port group (math.add, math.multiply, mix.sum, logic.boolean)
        — see PortGroups.h for the whole mechanism. Holds the live group
        size and clamps it to the node's [min, max]; a subclass declares its
        ports from `getGroupPortCount()` and reads that many inputs in
        `processSample()`.

        `setGroupPortCount()` is only ever called by GraphCompiler on a
        node it just created (message thread, before publish), never on one
        the audio thread may be running — the compiler refuses to reuse a
        group node whose count would change.
    */
    class GrowableGroupNode : public Node
    {
    public:
        int getGroupPortCount() const noexcept override { return groupCount; }

        void setGroupPortCount (int count) noexcept override
        {
            groupCount = juce::jlimit (minGroupCount, maxGroupCount, count);
        }

    protected:
        GrowableGroupNode (int minCount, int maxCount) noexcept
            : minGroupCount (minCount), maxGroupCount (maxCount), groupCount (minCount)
        {
        }

        PortGroup groupFor (const juce::String& idPrefix) const
        {
            return PortGroup { .idPrefix = idPrefix, .minCount = minGroupCount, .maxCount = maxGroupCount };
        }

        const int minGroupCount;
        const int maxGroupCount;
        int groupCount;
    };

    /** One member of a numeric (Control) growable group whose unwired
        ports fall back to a stored value — math.add's `in.N` (identity 0)
        and math.multiply's (identity 1). Each unwired member is both a
        real in-node editable value (NodeCard shows its slider, "I have a
        value, but you can connect me") and the node's identity element, so
        a fresh spare port or a hole left by a removed cable never changes
        the result.
    */
    inline PortDescriptor makeNumericGroupPort (const PortGroup& group, int index, float identity)
    {
        return PortDescriptor { .id = group.idPrefix + juce::String (index),
                                 .type = SignalType::Control,
                                 .label = "In " + juce::String (index + 1),
                                 .defaultValue = identity,
                                 .hasFallbackWhenUnconnected = true,
                                 .group = group };
    }
}
