#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "view.count" (design/Visualization/Count.png) — the
        default viewer for integer Control signals, spawned by Ctrl/Cmd-
        clicking an integer Control output port (ui/src/graph/graphStore.ts's
        createCountFromPort, same shortcut shape ViewRippleNode.h's own
        createRippleFromPort already established for Event).

        Fixed Control+isInteger ports, not polymorphic the way view.glance
        is (`InheritingPortsNode`) — this is a viewer for integer Control
        signals specifically, same reasoning ViewRippleNode.h gives for
        staying fixed to Event rather than adopting whatever's wired. A real
        pass-through (in AND out) so it can be spliced into an existing
        integer wire without altering behaviour, same as view.ripple/
        view.glance.

        No new engine-side telemetry plumbing, unlike view.ripple's own
        EventImpulse addition: the declared preview below (Waveform on
        "out") reuses the Oscilloscope tap machinery view.scope already
        drives for Control signals end to end. CountBody.tsx (NodeCard.tsx's
        own typeId dispatch) reads the latest Oscilloscope frame's final
        bucket as "the current value" instead of drawing it as a trace —
        a client-side-only interpretation of existing data, not a new
        producer.
    */
    // Ports are quantity-neutral (Dimensionless, the wildcard): a viewer
    // shows any integer as it is. Declaring Count here made an Int macro of
    // another quantity get a Map inserted just to be looked at.
    class ViewCountNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        // No title (design/Visualization/Count.png) — CountBody.tsx never
        // reads this, but a real, sensible value still matters for the Add
        // menu/component gallery/any other place a plain title string is
        // all that's available (ViewRippleNode.h's own reasoning).
        juce::String getTitle() const override { return "Count"; }
        juce::String getCategory() const override { return "View"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Glance; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Signal, .label = "In",
                                       .isInteger = true, .kind = ValueKind::Int,
                                       .step = 1.0f } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true,
                                       .isInteger = true, .kind = ValueKind::Int,
                                       .step = 1.0f } };
        }

        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::Waveform, .portId = "out" } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override { outputs[0] = inputs[0]; }
    };
}
