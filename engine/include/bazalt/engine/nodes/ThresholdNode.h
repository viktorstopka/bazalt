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

        A Schmitt trigger (2026-10-12): it turns **on** when By crosses On and
        **off** only once By has come back past On − Hysteresis, so a signal
        hovering at the level (a ringing guitar string) never chatters. Rising
        is "on above the level"; Falling is "on below it" (a sound dying away).
        Outputs: **On** fires at the moment it turns on, **Off** at the moment it
        turns off, **Gate** is 1 while on — a held gate for an Envelope, a
        Switch, a Clock's Run.
    */
    class ThresholdNode : public Node
    {
    public:
        static constexpr int numInputs = 3;
        static constexpr int numOutputs = 3;

        void reset() override { on = false; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Threshold"; }
        juce::String getCategory() const override { return "Logic"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { .id = "by", .type = SignalType::Signal },
                PortDescriptor { .id = "threshold", .type = SignalType::Signal, .label = "On",
                                  .minValue = -100000.0f, .maxValue = 100000.0f, .defaultValue = 0.5f,
                                  .hasFallbackWhenUnconnected = true, .softMin = 0.0f, .softMax = 1.0f },
                PortDescriptor { .id = "logic.threshold.hysteresis", .type = SignalType::Signal, .label = "Hysteresis",
                                  .minValue = 0.0f, .maxValue = 100000.0f, .defaultValue = 0.02f,
                                  .hasFallbackWhenUnconnected = true, .softMin = 0.0f, .softMax = 0.5f },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "onThreshold", .type = SignalType::Event, .label = "On", .isPrimaryOutput = true },
                     PortDescriptor { .id = "off", .type = SignalType::Event, .label = "Off" },
                     PortDescriptor { .id = "gate", .type = SignalType::Signal, .label = "Gate", .kind = ValueKind::Bool,
                                      .quantity = Quantity::Boolean } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "logic.threshold.direction", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                           .displayName = "Direction", .isInteger = true, .kind = ValueKind::Enum,
                                           .enumOptions = { { "rising", "Rising (on above)" }, { "falling", "Falling (on below)" } } } };
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
            else if (parameterId == "logic.threshold.hysteresis")
                storedHysteresis = value;
            else if (parameterId == "logic.threshold.direction")
                falling = value >= 0.5f;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto by = inputs[0];
            const auto level = std::isnan (inputs[1]) ? storedThreshold : inputs[1];
            const auto hysteresis = juce::jmax (0.0f, std::isnan (inputs[2]) ? storedHysteresis : inputs[2]);

            const auto turnsOn = falling ? by <= level : by >= level;
            const auto turnsOff = falling ? by > level + hysteresis : by < level - hysteresis;

            auto fired = 0.0f, released = 0.0f;
            if (! on && turnsOn)
            {
                on = true;
                fired = 1.0f;
            }
            else if (on && turnsOff)
            {
                on = false;
                released = 1.0f;
            }
            outputs[0] = fired; // Event: non-zero = fired this sample
            outputs[1] = released;
            outputs[2] = on ? 1.0f : 0.0f;
        }

    private:
        bool on = false, falling = false;
        float storedThreshold = 0.5f; // matches the "threshold" port's own declared defaultValue
        float storedHysteresis = 0.02f;
    };
}
