#pragma once

#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/nodes/InheritingPortsNode.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "adapt.sampleHold" (NODE_CATALOG.md's `adapt.*` row:
        `in` (Control), `trigger : Event`, `glide : float·Time·0–5s·log·0` ->
        `out` Control, "quantity inherited"). On each `trigger` event it
        samples `in` and holds that value until the next one — the classic
        way to turn a continuous or random source into steps.

        `in` and `out` share one Quantity taken from what feeds `in`
        (InheritingPortsNode.h), so a sampled Frequency is still a Frequency
        downstream. The type is always Control. `trigger` and `glide` don't
        influence it.

        `glide` is an exponential lag toward the held value: 0 (the default)
        jumps at once, otherwise it is the one-pole time constant — the same
        definition, and the same reasons (scale-free across quantities,
        sample-rate-derived rather than hardcoded), as `math.slew`. The
        coefficient is derived from the prepared sample rate and recomputed
        only when `glide` changes. Before the first trigger the output is 0;
        the first trigger jumps to the sampled value rather than gliding up from
        that 0.

        Events are "non-zero = fired this sample", so every non-zero `trigger`
        sample resamples. A NaN or infinite `in` at a trigger is not held (the
        previous value stays), so one bad sample can't poison the output.
    */
    class SampleHoldNode : public InheritingPortsNode
    {
    public:
        static constexpr int numInputs = 3; // in, trigger, glide
        static constexpr int numOutputs = 1;

        SampleHoldNode() noexcept : InheritingPortsNode (Quantity::Dimensionless) {}

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }

        void reset() override
        {
            held = 0.0f;
            state = 0.0f;
            hasSampled = false;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Sample & Hold"; }
        juce::String getCategory() const override { return "Adapters"; }

        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            if (toPortId == "in")
                offer (0, source, false);
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "in", .type = SignalType::Signal, .label = "In",
                                  .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::Quantity },
                PortDescriptor { .id = "trigger", .type = SignalType::Event, .label = "Trigger" },
                ValueTypes::timeSecondsPort ("adapt.sampleHold.glide", "Glide", 0.0f, 5.0f),
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Signal, .isPrimaryOutput = true,
                                       .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::Quantity } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "adapt.sampleHold.glide")
                storedGlide = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            if (std::fabs (inputs[1]) > 0.0f && std::isfinite (inputs[0])) // fabs(NaN) > 0 is false
            {
                held = inputs[0];
                if (! hasSampled)
                {
                    state = held; // first trigger jumps, no glide up from 0
                    hasSampled = true;
                }
            }

            const auto glide = juce::jmax (0.0f, std::isnan (inputs[2]) ? storedGlide : inputs[2]);
            state += (held - state) * (1.0f - coefficientFor (glide));
            outputs[0] = state;
        }

    private:
        // The pole for a glide time, memoised. glide <= 0 -> coefficient 0 ->
        // `state += (held - state) * 1`, i.e. jump.
        float coefficientFor (float glide) noexcept
        {
            if (glide != cachedGlide)
            {
                cachedGlide = glide;
                cachedCoefficient = glide > 0.0f ? (float) std::exp (-1.0 / ((double) glide * sampleRate)) : 0.0f;
            }
            return cachedCoefficient;
        }

        double sampleRate = 44100.0;
        float storedGlide = 0.0f; // matches the port's own declared defaultValue
        float held = 0.0f;
        float state = 0.0f;
        bool hasSampled = false;
        float cachedGlide = -1.0f;
        float cachedCoefficient = 0.0f;
    };
}
