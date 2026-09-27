#pragma once

#include "bazalt/engine/nodes/InheritingPortsNode.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "view.glance". wiki/NODES_Gaps.md's
        `single-type-preview-coverage` finding, Milestone 0.6: your own
        described shape — "a special node, that doesn't really have a
        title, a single output and input and are overall VERY
        minimalistic" — splice it into any cable the same way
        `util.reroute` splices in, and it shows a live trace of whatever
        passes through, with none of an ordinary node's title/parameter
        chrome. Unlike `view.scope`/`meter`/`spectrum` (a dead end that
        only taps a wire from the side), this one has a real output and
        passes the signal through unchanged — the point is to sit INSIDE
        an existing connection and be looked at, not to branch off it.

        Same polymorphic-port mechanism `util.reroute`/`view.scope`/
        `view.meter` already use (`InheritingPortsNode`, ADR-0027) —
        adopts whatever signal type and quantity is wired into it: Audio,
        Control, Boolean or Event. Does not support `Note` (the same scope
        `view.scope`/`view.meter` already have — forwarding a Note-typed
        passthrough needs `consumeNoteBlock`/`produceNoteBlock` the way
        `util.reroute` does; add that if a real patch ever needs to glance
        at a note stream, not speculatively here) or `Data` (never converts
        implicitly, `canConnect` rejects it the same as everywhere else).
    */
    class ViewGlanceNode : public InheritingPortsNode
    {
    public:
        ViewGlanceNode() noexcept : InheritingPortsNode (SignalType::Audio) {}

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return "Glance"; }
        juce::String getCategory() const override { return "View"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Glance; }

        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            if (toPortId == "in")
                offer (0, source, true);
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = resolvedType, .label = "In", .quantity = resolvedQuantity,
                                       .polymorphism = PortPolymorphism::SignalAndQuantity } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = resolvedType, .isPrimaryOutput = true, .quantity = resolvedQuantity,
                                       .polymorphism = PortPolymorphism::SignalAndQuantity } };
        }

        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::Waveform, .portId = "out" } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override { outputs[0] = inputs[0]; }
    };
}
