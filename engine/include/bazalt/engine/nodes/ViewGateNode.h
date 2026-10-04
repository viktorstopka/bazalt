#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/nodes/ViewHistoryWindow.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "view.gate" (design/Visualization/Gate.png) — the
        scrolling-history viewer for Boolean signals, the third of the three
        history viewers. Same pass-through + ViewHistoryWindow as
        view.scope.control; the binary TRUE/FALSE scale and the square-edged
        filled trace are pure display (GateBody.tsx).

        "A brief true state must never be dropped" is already guaranteed on
        this side: AnalysisThread::publishRollingHistory folds EVERY sample
        into its column's (lo, hi) pair, so a single-sample pulse lands as
        hi = 1 in whichever column it fell in — nothing is point-sampled. The
        UI then treats any column with hi = 1 as true and never draws it
        narrower than one screen pixel.

        Fixed Boolean ports, not polymorphic: a viewer for one type, the same
        reasoning view.count/view.ripple give for staying fixed to theirs.
    */
    class ViewGateNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return "Gate"; }
        juce::String getCategory() const override { return "View"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Glance; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Boolean, .label = "In", .kind = ValueKind::Bool } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Boolean, .label = "Out", .isPrimaryOutput = true, .kind = ValueKind::Bool } };
        }

        std::vector<ParameterDescriptor> getParameters() const override { return { window.getParameter() }; }
        void setParameter (const juce::String& parameterId, float value) override { window.setParameter (parameterId, value); }
        std::vector<PreviewDescriptor> getPreviews() const override { return { window.getPreview ("out") }; }

        void processSample (const float* inputs, float* outputs) noexcept override { outputs[0] = inputs[0]; }

    private:
        ViewHistoryWindow window { "view.gate.timeWindow" };
    };
}
