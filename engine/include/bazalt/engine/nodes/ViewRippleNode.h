#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "view.ripple" (design/Visualization/Ripple.png) — the
        default viewer for the Event type, spawned by Ctrl/Cmd-clicking an
        Event output port (ui/src/graph/graphStore.ts's createRippleFromPort,
        same shortcut shape as the existing drag-a-port-out-to-a-Macro one).

        Fixed `SignalType::Event` ports, not polymorphic the way
        `view.glance` is (`InheritingPortsNode`) — this is a viewer for
        Event signals specifically, not a generic splice-and-watch node for
        any type. A real pass-through (in AND out, unlike view.scope/
        spectrum/meter's dead end) so it can be spliced into an existing
        Event wire without altering behaviour, same reasoning
        ViewGlanceNode.h's own header comment gives for why IT has a real
        output too.

        No parameters at all — "No title, no labels — the panel is purely
        the visual" (direct instruction). The animation itself (a ring born
        per incoming event, expanding and fading) is driven entirely by
        AnalysisThread::publishEventImpulse's own PreviewKind::EventImpulse
        frames — real event telemetry with real sample-accurate ages, not a
        UI-side timer guessing at when events happened.
    */
    class ViewRippleNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        // No title (direct instruction) — NodeCard.tsx's RippleBody never
        // reads this, but a real, sensible value still matters for the Add
        // menu/component gallery/any other place a plain title string is
        // all that's available.
        juce::String getTitle() const override { return "Ripple"; }
        juce::String getCategory() const override { return "View"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Glance; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Event, .label = "In" } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Event, .label = "Out", .isPrimaryOutput = true } };
        }

        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::EventImpulse, .portId = "out" } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override { outputs[0] = inputs[0]; }
    };
}
