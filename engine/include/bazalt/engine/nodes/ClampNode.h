#pragma once

#include "bazalt/engine/nodes/InheritingPortsNode.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.clamp" (NODE_CATALOG.md's `math.*` row:
        `in`, `low`, `high`). `low`/`high` are real ports, not parameters
        — clamping to a live-modulated range (a sidechain envelope
        limiting how far another signal can swing, say) is exactly the
        kind of thing this whole node exists for. Swapped rather than
        rejected if `low > high` (a live modulator isn't bound by any
        author-time ordering guarantee).

        `in`/`low`/`high`/`out` all share one Quantity (InheritingPortsNode.h,
        same mechanism logic.compare/adapt.sampleHold already use) — unlike
        math.round, Clamp doesn't itself PRODUCE anything (an integer,
        a particular quantity); it's purely type-preserving, so there's no
        single "default" nature of its own to hardcode. Direct feedback,
        2026-10-04: a static `isInteger = true` here (mirroring Round's own
        fix) would be WRONG most of the time — Clamp bounds ordinary floats
        (gain, LFO depth, ...) at least as often as it bounds an index/count
        — so this inherits isInteger/quantity/unit from whichever of
        in/low/high is wired instead, same "colour follows what's actually
        there" answer util.reroute/view.glance/logic.compare already give:
        clamping an integer (e.g. straight out of math.round) reads as
        integer end to end, clamping a float stays correctly value/
        modulation-coloured. The SignalType itself stays fixed Control
        (inheritType=false in every offer() call below) — Clamp has never
        supported anything else.
    */
    class ClampNode : public InheritingPortsNode
    {
    public:
        static constexpr int numInputs = 3; // in, low, high
        static constexpr int numOutputs = 1;

        ClampNode() noexcept : InheritingPortsNode (SignalType::Control) {}

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Clamp"; }
        juce::String getCategory() const override { return "Utility"; }

        // Priority order in/low/high: "in" is the value actually being
        // bounded, so it wins when more than one of the three is wired —
        // same "declaration order breaks ties" rule logic.compare's own
        // a/b/tolerance (0/1/2) already establishes.
        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            if (toPortId == "in")
                offer (0, source, false);
            else if (toPortId == "math.clamp.low")
                offer (1, source, false);
            else if (toPortId == "math.clamp.high")
                offer (2, source, false);
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "in", .type = SignalType::Control,
                                  .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::Quantity },
                PortDescriptor { .id = "math.clamp.low", .type = SignalType::Control, .label = "Low",
                                  .defaultValue = 0.0f, .hasFallbackWhenUnconnected = true,
                                  .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::Quantity },
                PortDescriptor { .id = "math.clamp.high", .type = SignalType::Control, .label = "High",
                                  .defaultValue = 1.0f, .hasFallbackWhenUnconnected = true,
                                  .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::Quantity },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true,
                                       .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::Quantity } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "math.clamp.low")
                lowValue = value;
            else if (parameterId == "math.clamp.high")
                highValue = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto low = std::isnan (inputs[1]) ? lowValue : inputs[1];
            const auto high = std::isnan (inputs[2]) ? highValue : inputs[2];
            outputs[0] = std::clamp (inputs[0], std::min (low, high), std::max (low, high));
        }

    private:
        float lowValue = 0.0f;
        float highValue = 1.0f;
    };
}
