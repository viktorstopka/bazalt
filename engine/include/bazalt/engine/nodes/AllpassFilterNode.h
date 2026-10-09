#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/Biquad.h"
#include <array>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "filter.allpass" (NODE_CATALOG.md's `filter.*` row:
        "phase rotation without amplitude change; cascaded, it is what gives
        strings stiffness and reverbs diffusion"). `stages` (structural,
        1-16) cascades that many `Biquad` allpass sections in series.

        `stages` is a **fixed-size array of 16**, not a growable port group —
        `PortGroups.h`'s growable groups exist specifically for
        connection-derived port COUNTS (`math.add`'s `in.0..in.N`); this is a
        structural COUNT with no per-stage port at all, so the right shape is
        the same one `ExecutionPlan::maxPortsPerNode`/`GrowableGroupNode`'s own
        `maxGroupCount` clamp already uses elsewhere: declare the bound,
        preallocate a fixed array to it, loop only the active prefix. Zero
        allocation, RT-safe by construction — no `std::vector` resize ever
        happens after `prepare()`.

        `amount` (the catalog's own port, Unipolar 0-1) maps onto each
        stage's RBJ `Q` as `0.1 + amount*9.9` — this node's own documented
        design call, matching `Biquad::setShelf`'s `slope01`-to-`S` mapping
        in spirit (the catalog names the port but not its exact effect).
        Every stage shares the same `frequency`/`amount`, matching the
        catalog's single pair of ports for however many `stages` are active.
    */
    class AllpassFilterNode : public Node
    {
    public:
        static constexpr int maxStages = 16;
        static constexpr int defaultStages = 4;
        static constexpr float defaultFrequencyHz = 1000.0f;
        static constexpr float defaultAmount = 0.5f;
        static constexpr int numInputs = 3; // in, frequency, amount
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }
        void reset() override
        {
            for (auto& stage : stages)
                stage.reset();
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Allpass Filter"; }
        juce::String getCategory() const override { return "Filters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio }),
                ValueTypes::frequencyPort ("filter.allpass.frequency", "Frequency", defaultFrequencyHz),
                PortDescriptor { .id = "filter.allpass.amount", .type = SignalType::Signal, .label = "Amount",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultAmount,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar,
                                  .polarity = Polarity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { perChannel ({ .id = "out", .type = SignalType::Signal, .quantity = Quantity::Audio }) };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "filter.allpass.stages",
                                            .minValue = 1.0f,
                                            .maxValue = (float) maxStages,
                                            .defaultValue = (float) defaultStages,
                                            .displayName = "Stages",
                                            .isInteger = true,
                                            .quantity = Quantity::Count,
                                            .step = 1.0f,
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "filter.allpass.frequency")
                storedFrequencyHz = value;
            else if (parameterId == "filter.allpass.amount")
                storedAmount = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "filter.allpass.stages")
                activeStages = juce::jlimit (1, maxStages, (int) std::lround (value));
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto frequency = std::isnan (inputs[1]) ? storedFrequencyHz : inputs[1];
            const auto amount = std::isnan (inputs[2]) ? storedAmount : juce::jlimit (0.0f, 1.0f, inputs[2]);

            if (frequency != cache.frequency || amount != cache.amount)
            {
                cache = { frequency, amount };
                const auto q = 0.1f + amount * 9.9f;
                for (int i = 0; i < activeStages; ++i)
                    stages[(size_t) i].setAllpass (sampleRate, frequency, q);
            }

            auto sample = inputs[0];
            for (int i = 0; i < activeStages; ++i)
                sample = stages[(size_t) i].processSample (sample);

            outputs[0] = sample;
        }

    private:
        struct Cache
        {
            float frequency = -1.0f, amount = -1.0f;
        };

        double sampleRate = 44100.0;
        float storedFrequencyHz = defaultFrequencyHz;
        float storedAmount = defaultAmount;
        int activeStages = defaultStages;
        std::array<Biquad, (size_t) maxStages> stages;
        Cache cache;
    };
}
