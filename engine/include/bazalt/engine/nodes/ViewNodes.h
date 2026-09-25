#pragma once

#include "bazalt/engine/nodes/InheritingPortsNode.h"

namespace bazalt::engine::nodes
{
    /** The three placeable viewers (NODE_CATALOG.md's `view.*` row): each has
        ONE input, no outputs, and does nothing to the signal - a viewer is a
        legitimate dead end the compiler schedules like any node
        (`view.listen` is the same shape). What it shows is a preview declared
        on its own input port (`getPreviews()`), which the shared M20 machinery
        renders; the engine resolves a preview on an input to the buffer wired
        into it (ADR-0029), so a viewer costs nothing on the audio thread
        beyond the tap's own copy, and only while it is on screen.

        Like every preview it is only live while the node is near the
        viewport, and an unwired input simply shows nothing yet: the
        subscription is remembered and binds the moment a cable is connected.
    */

    /** "view.scope": a Scope. Takes any plain per-sample signal - Audio and
        Control as the catalog says, and also Boolean/Event, since watching a
        gate is one of the things a scope is for - through the same inherited-
        port mechanism `util.reroute` and `logic.select` use
        (InheritingPortsNode.h, ADR-0027). It does not take Note, Data or
        Spectral signals; those are rejected as an ordinary type mismatch.
    */
    class ViewScopeNode : public InheritingPortsNode
    {
    public:
        ViewScopeNode() noexcept : InheritingPortsNode (SignalType::Audio) {}

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 0; }

        juce::String getTitle() const override { return "Scope"; }
        juce::String getCategory() const override { return "View"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Horizontal; }

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

        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::Waveform, .portId = "in" } };
        }

        void processSample (const float*, float*) noexcept override {}
    };

    /** "view.spectrum": a Spectrum analyser. Audio only - a spectrum of a
        control signal isn't something the analysis is meant for.
    */
    class ViewSpectrumNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 0; }

        juce::String getTitle() const override { return "Spectrum"; }
        juce::String getCategory() const override { return "View"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Horizontal; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Audio, .label = "In" } };
        }

        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::Spectrum, .portId = "in" } };
        }

        void processSample (const float*, float*) noexcept override {}
    };

    /** "view.meter": a level Meter, for Audio or any other plain signal (same
        input rules as view.scope).
    */
    class ViewMeterNode : public InheritingPortsNode
    {
    public:
        ViewMeterNode() noexcept : InheritingPortsNode (SignalType::Audio) {}

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 0; }

        juce::String getTitle() const override { return "Meter"; }
        juce::String getCategory() const override { return "View"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Horizontal; }

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

        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::Meter, .portId = "in" } };
        }

        void processSample (const float*, float*) noexcept override {}
    };
}
