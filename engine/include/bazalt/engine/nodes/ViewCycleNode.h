#pragma once

#include "bazalt/engine/nodes/InheritingPortsNode.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "view.cycle" — Cycle. The placeable phase-locked
        viewer (PreviewKind::PhaseLocked): splice it into a cable and it shows
        that signal over a fixed phaseLockedCycles cycles of the nearest
        phase source upstream (ExecutionPlan::resolvePhaseSources — "phase
        follows the cable"). A saw through a filter reads as the filtered saw,
        standing still, at any pitch or LFO rate; no trigger, no searching for
        crossings, no time window to set.

        Unlike an oscillator's own preview (its waveform evaluated from a
        parameter snapshot), this one folds the cable's REAL samples into
        phase bins (`foldSamples`), because whatever sits between the
        oscillator and here — a filter, a shaper, a VCA — has no formula to
        evaluate. With no phase source upstream there is nothing to lock to
        and the panel stays empty.

        Replaces view.scope and view.glance (removed 2026-10-04, direct
        instruction): a time-domain scope is the wrong tool for looking at a
        waveform. Polymorphic like Glance was (Audio or Control — the same
        node serves an audio path and an LFO path), and a real pass-through.
    */
    class ViewCycleNode : public InheritingPortsNode
    {
    public:
        ViewCycleNode() noexcept : InheritingPortsNode (SignalType::Audio) {}

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return "Cycle"; }
        juce::String getCategory() const override { return "View"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Glance; }

        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            if (toPortId == "in" && (source.type == SignalType::Audio || source.type == SignalType::Control))
                offer (0, source, true);
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = resolvedType, .label = "In", .quantity = resolvedQuantity,
                                      .channels = Channels::Inherited, .polymorphism = PortPolymorphism::SignalAndQuantity } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = resolvedType, .label = "Out", .isPrimaryOutput = true,
                                      .quantity = resolvedQuantity, .channels = Channels::Inherited, .polymorphism = PortPolymorphism::SignalAndQuantity } };
        }

        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::PhaseLocked, .portId = "out", .foldSamples = true } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override { outputs[0] = inputs[0]; }
    };
}
