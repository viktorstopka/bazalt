#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "filter.dcBlock" (NODE_CATALOG.md's `filter.*` row:
        "trivial but essential wherever nonlinearities and feedback meet").
        A tunable one-pole DC blocker: `y[n] = x[n] - x[n-1] + R*y[n-1]`,
        `R` derived from `cutoff` — NOT the fixed `R` (~0.995-0.9997) most
        DC blockers hardcode, since the catalog asks for a real 1-100Hz
        port, not a single fixed constant.

        `R = exp(-2*pi*cutoff/sampleRate)`: the same exponential-decay
        approximation `math.slew`/`adapt.sampleHold` already use for their
        own one-pole time constants (CLAUDE.md rule 6: derived from the
        live sample rate in `prepare()`, memoised — `SlewNode.h`'s
        `CoefficientCache` pattern, copied verbatim — so a static or
        slowly-modulated `cutoff` costs nothing beyond the NaN check most
        samples, and a genuinely audio-rate-modulated one still works,
        just costs the `exp()` every such sample).

        Pattern B (inline DSP): two floats of state (`x[n-1]`, `y[n-1]`),
        nothing worth a standalone primitive class over.
    */
    class DcBlockNode : public Node
    {
    public:
        static constexpr float defaultCutoffHz = 20.0f;
        static constexpr int numInputs = 2; // in, cutoff
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }

        void reset() override
        {
            previousInput = 0.0f;
            previousOutput = 0.0f;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "DC Blocker"; }
        juce::String getCategory() const override { return "Filters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ "in", SignalType::Audio }),
                PortDescriptor { .id = "filter.dcBlock.cutoff", .type = SignalType::Control, .label = "Cutoff",
                                  .unit = "Hz", .minValue = 1.0f, .maxValue = 100.0f, .defaultValue = defaultCutoffHz,
                                  .isLogScale = true, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency,
                                  .curve = Curve::Logarithmic },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { perChannel ({ "out", SignalType::Audio }) };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "filter.dcBlock.cutoff")
                storedCutoffHz = juce::jlimit (1.0f, 100.0f, value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto cutoff = std::isnan (inputs[1]) ? storedCutoffHz : juce::jlimit (1.0f, 100.0f, inputs[1]);
            const auto r = coefficientFor (cutoff);

            const auto x = inputs[0];
            const auto y = x - previousInput + r * previousOutput;

            previousInput = x;
            previousOutput = y;
            outputs[0] = y;
        }

    private:
        float coefficientFor (float cutoffHz) noexcept
        {
            if (cutoffHz != cache.cutoffHz)
            {
                cache.cutoffHz = cutoffHz;
                cache.coefficient = sampleRate > 0.0
                                       ? (float) std::exp (-2.0 * juce::MathConstants<double>::pi * (double) cutoffHz / sampleRate)
                                       : 0.0f;
            }
            return cache.coefficient;
        }

        struct CoefficientCache
        {
            float cutoffHz = -1.0f;
            float coefficient = 0.0f;
        };

        double sampleRate = 44100.0;
        float storedCutoffHz = defaultCutoffHz;
        float previousInput = 0.0f;
        float previousOutput = 0.0f;
        CoefficientCache cache;
    };
}
