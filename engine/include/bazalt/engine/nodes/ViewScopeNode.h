#pragma once

#include "bazalt/engine/nodes/InheritingPortsNode.h"
#include "bazalt/engine/nodes/ViewHistoryWindow.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "view.scope" — Scope (wiki/plans/DataAndWavetable.md
        §2): the scrolling-history viewer for any value, replacing the three
        type-specific ones (view.scope.control, view.scope.modulation,
        view.gate). The engine side is a transparent pass-through plus the
        shared ViewHistoryWindow; how it draws follows what is wired
        (ui/src/nodes/ScopeBody.tsx): a Boolean gets the TRUE/FALSE scale, a
        modulation or an audio signal the centred filled trace, anything else
        a plain line over its range.

        Its ports take on the type and quantity of the source, so splicing it
        into a wire never changes what that wire resolves to — and it never
        makes canConnect() insert anything in front of itself.

        (The id was the old phase-locked oscilloscope's until schema v9 dropped
        that node; view.cycle is its successor. Migrations run in order, so a
        pre-v9 "view.scope" never reaches this one.)
    */
    class ViewScopeNode : public InheritingPortsNode
    {
    public:
        ViewScopeNode() noexcept : InheritingPortsNode (SignalType::Control) {}

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return "Scope"; }
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
            return { PortDescriptor { .id = "out", .type = resolvedType, .label = "Out", .isPrimaryOutput = true,
                                      .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::SignalAndQuantity } };
        }

        std::vector<ParameterDescriptor> getParameters() const override { return { window.getParameter() }; }
        void setParameter (const juce::String& parameterId, float value) override { window.setParameter (parameterId, value); }
        std::vector<PreviewDescriptor> getPreviews() const override { return { window.getPreview ("out") }; }

        void processSample (const float* inputs, float* outputs) noexcept override { outputs[0] = inputs[0]; }

    private:
        ViewHistoryWindow window { "view.scope.timeWindow" };
    };
}
