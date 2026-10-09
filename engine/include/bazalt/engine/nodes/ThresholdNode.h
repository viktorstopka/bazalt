#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "logic.threshold" (M16, promotes the UI-only
        `mock.triggerByThreshold` to a real engine node). `SIGNAL_TYPES.md`
        §5's Control -> Event adapter — `canConnect` auto-inserts this when
        a Control output feeds an Event-typed input, wired into this
        node's "by" port (not "in" — `AdapterStep::inputPortId` exists
        specifically so the caller doesn't have to special-case this).

        Rising-edge detector with a small fixed hysteresis band so noise
        sitting exactly at the threshold doesn't chatter — not
        user-exposed, matching NODE_CATALOG (1).md's own note ("a small
        fixed dead-band, not user-exposed").
    */
    class ThresholdNode : public Node
    {
    public:
        static constexpr int numInputs = 2;
        static constexpr int numOutputs = 1;

        void reset() override { wasAboveHysteresisBand = false; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Threshold"; }
        juce::String getCategory() const override { return "Logic"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { .id = "by", .type = SignalType::Signal },
                PortDescriptor { .id = "threshold",
                                  .type = SignalType::Signal,
                                  .minValue = 0.0f,
                                  .maxValue = 1.0f,
                                  .defaultValue = 0.5f,
                                  .hasFallbackWhenUnconnected = true },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "onThreshold", .type = SignalType::Event, .isPrimaryOutput = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            // "threshold" is a real port, not a ParameterDescriptor (per
            // SIGNAL_TYPES.md §5's own default-seeding column) — but its
            // hasFallbackWhenUnconnected slider still commits through this
            // same setParameter path (docs/CLEANUP.md Priority 1 #3: this
            // used to be a no-op, so dragging the slider changed the
            // displayed number with zero effect on the actual sound).
            if (parameterId == "threshold")
                storedThreshold = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            static constexpr float hysteresis = 0.02f;

            const auto by = inputs[0];
            const auto threshold = std::isnan (inputs[1]) ? storedThreshold : inputs[1];

            const auto risingEdge = ! wasAboveHysteresisBand && by >= threshold;
            outputs[0] = risingEdge ? 1.0f : 0.0f; // Event: non-zero = fired this sample

            if (by >= threshold)
                wasAboveHysteresisBand = true;
            else if (by < threshold - hysteresis)
                wasAboveHysteresisBand = false;
        }

    private:
        bool wasAboveHysteresisBand = false;
        float storedThreshold = 0.5f; // matches the "threshold" port's own declared defaultValue
    };
}
