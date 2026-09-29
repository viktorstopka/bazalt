#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "clock.divide" (wiki/NODES.md's `clock.*` row, the
        Clock+Seq batch). Passes through every Nth incoming tick — the
        primitive that turns one clock into a whole family of related
        sub-clocks (half-time, quarter-time, ...) by chaining several of
        these off one `clock.pulse`.

        **Port id note**: the catalog names both the input and the output
        `tick` — a real, enforced engine invariant
        (`ExecutionPlanTapTests.cpp`'s "no node type reuses a port id across
        its inputs and outputs") rejects that, so the output's id is
        `tickOut` while its display label stays "Tick" (id != label is
        already normal throughout this codebase — the UI never shows ids).

        `divide = 1` is a plain passthrough (every tick fires). `reset`
        zeroes the internal counter without itself producing an output tick
        — the next N incoming ticks are needed before the first output tick,
        exactly as if the node had just been created.
    */
    class ClockDivideNode : public Node
    {
    public:
        static constexpr int numInputs = 3; // tick, divide, reset
        static constexpr int numOutputs = 1;

        void reset() override { counter = 0; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Divide"; }
        juce::String getCategory() const override { return "Clock"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "tick", .type = SignalType::Event, .label = "Tick" },
                PortDescriptor { .id = "clock.divide.divide", .type = SignalType::Control, .label = "Divide",
                                  .minValue = 1.0f, .maxValue = 64.0f, .defaultValue = 2.0f, .isInteger = true,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Count, .step = 1.0f },
                PortDescriptor { .id = "reset", .type = SignalType::Event, .label = "Reset" },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "tickOut", .type = SignalType::Event, .label = "Tick", .isPrimaryOutput = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "clock.divide.divide")
                storedDivide = juce::jmax (1, (int) std::lround (value));
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto tickIn = std::fabs (inputs[0]) > 0.0f;
            const auto divide = std::isnan (inputs[1]) ? storedDivide : juce::jmax (1, (int) std::lround (inputs[1]));
            const auto resetIn = std::fabs (inputs[2]) > 0.0f;

            if (resetIn)
                counter = 0;

            auto fired = false;

            if (tickIn)
            {
                ++counter;
                if (counter >= divide)
                {
                    counter = 0;
                    fired = true;
                }
            }

            outputs[0] = fired ? 1.0f : 0.0f;
        }

    private:
        int counter = 0;
        int storedDivide = 2;
    };
}
