#pragma once

#include "bazalt/engine/nodes/InheritingPortsNode.h"
#include "bazalt/engine/nodes/ViewHistoryWindow.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "view.scope.modulation" (design/Visualization/ScopeMod.png)
        — the scrolling-history viewer for Modulation signals (Control with a
        normalised Unipolar/Bipolar quantity, wiki/NODES.System.md §2). The
        second of the three history viewers: view.scope.control with a
        different vertical scale and a filled trace, both of which are pure
        display concerns living in ScopeModulationBody.tsx. The engine side is
        the same pass-through + ViewHistoryWindow as view.scope.control.

        The one real engine difference: its ports adopt the source's
        QUANTITY (PortPolymorphism::Quantity, the logic.compare/math.clamp
        mechanism — the type stays Control). A fixed Bipolar input would make
        canConnect() splice an adapter between a Unipolar source and this
        node, so the viewer would no longer show the signal it was placed to
        show; and a fixed quantity on the pass-through OUTPUT would change
        what a downstream connection resolves to. Adopting the quantity keeps
        it a transparent splice either way. Unconnected, it declares Bipolar —
        the signed case the panel is drawn for.

        The vertical range and the centre line are cosmetic, so they live in
        NodeInstance.properties ("viewer.rangeMin"/"viewer.rangeMax"/
        "viewer.center"), never parameters — the same "cosmetic goes in
        properties, DSP-affecting goes in parameters" split view.scope.control
        already follows.
    */
    class ViewScopeModulationNode : public InheritingPortsNode
    {
    public:
        ViewScopeModulationNode() noexcept : InheritingPortsNode (SignalType::Control) { resolvedQuantity = Quantity::Bipolar; }

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return "Scope (Modulation)"; }
        juce::String getCategory() const override { return "View"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Glance; }

        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            if (toPortId == "in")
                offer (0, source, false);
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Control, .label = "In",
                                      .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::Quantity } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .label = "Out", .isPrimaryOutput = true,
                                      .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::Quantity } };
        }

        std::vector<ParameterDescriptor> getParameters() const override { return { window.getParameter() }; }
        void setParameter (const juce::String& parameterId, float value) override { window.setParameter (parameterId, value); }
        std::vector<PreviewDescriptor> getPreviews() const override { return { window.getPreview ("out") }; }

        void processSample (const float* inputs, float* outputs) noexcept override { outputs[0] = inputs[0]; }

    private:
        ViewHistoryWindow window { "view.scope.modulation.timeWindow" };
    };
}
