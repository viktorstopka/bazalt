#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "filter.onepole". One Audio input, one Audio
        output: y[n] = c*x[n] + (1-c)*y[n-1]. The Karplus-Strong proof
        graph's damping filter — lower `coefficient` values decay/mellow
        the plucked string faster (ARCHITECTURE.md §3.4).

        M20 (direct feedback: "there is no reason why ... wouldn't be
        modulatable"): `coefficient` is a real Control-type input port now
        (same dotted id, same `hasFallbackWhenUnconnected` NaN-sentinel
        pattern as elsewhere) rather than a plain parameter — it's used
        directly inline per sample already, so modulating it costs nothing
        beyond the NaN check itself.

        M22: a second output, `highpass` = `in - lowpass` — the complement
        of the one-pole's existing output, which is exactly what a
        one-pole low/high split already provides algebraically (their gains
        sum to unity by construction). Free: no new state, no new work
        beyond the subtraction, and it closes the gap between this node and
        NODE_CATALOG.md's own `filter.onepole` spec (`lowpass`, `highpass`
        outputs) without touching the existing `"out"` id — renaming it
        would break every saved patch already wired to it (CLAUDE.md rule
        3). `"out"`'s label moves from empty (falls back to the id in the
        UI) to "Lowpass" for clarity; a label isn't persisted, only the id
        is, so this is free to change.
    */
    class OnePoleFilterNode : public Node
    {
    public:
        static constexpr int numInputs = 2; // in, coefficient
        static constexpr int numOutputs = 2; // out (lowpass), highpass

        void reset() override { state = 0.0f; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "One-Pole Filter"; }
        juce::String getCategory() const override { return "Filters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio }),
                PortDescriptor { .id = "filter.onepole.coefficient", .type = SignalType::Signal, .label = "Damping",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar, .polarity = Polarity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                perChannel (PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Lowpass", .isPrimaryOutput = true, .quantity = Quantity::Audio }),
                perChannel (PortDescriptor { .id = "highpass", .type = SignalType::Signal, .label = "Highpass", .quantity = Quantity::Audio }),
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override { return {}; }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "filter.onepole.coefficient")
                coefficient = juce::jlimit (0.0f, 1.0f, value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto c = std::isnan (inputs[1]) ? coefficient : juce::jlimit (0.0f, 1.0f, inputs[1]);
            state = c * inputs[0] + (1.0f - c) * state;
            outputs[0] = state;
            outputs[1] = inputs[0] - state;
        }

    private:
        float coefficient = 0.5f;
        float state = 0.0f;
    };
}
