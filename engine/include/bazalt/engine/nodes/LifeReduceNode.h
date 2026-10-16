#pragma once

#include "bazalt/engine/graph/InstanceGroups.h"
#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "life.reduce" — internal, never placed by hand
        (wiki/plans/InstanceAxis.md: "Merge is an ordinary step that reduces the
        instance axis"). Instance expansion (InstanceExpansion.h) puts one where
        a Merge stands, and one at the output when the output itself runs per
        instance: one input per instance (`in.0` … `in.N-1`), summed for the
        instances its group's runtime marks audible — a fading one with its
        ramp — and divided by the audible count in Average mode. Per channel,
        so a stereo voice reduces into a stereo sum. Records each instance's
        peak for release detection.

        Holds a pointer to its plan's group runtime, handed over once after
        compile (setRuntime), before the plan is published. A fresh node every
        compile (`life.reduce.generation` differs each time), so a running old
        plan never sees its pointer change. */
    class LifeReduceNode : public Node
    {
    public:
        static constexpr int maxInstances = 31; // one input per instance, within ExecutionPlan::maxPortsPerNode

        void setRuntime (InstanceGroupRuntime* runtimeIn) noexcept { runtime = runtimeIn; }
        int getGroupIndex() const noexcept { return groupIndex; }

        bool isDeprecated() const noexcept override { return true; } // internal: never in the Add menu
        juce::String getTitle() const override { return "Merge"; }
        juce::String getCategory() const override { return "Life"; }

        int getNumInputPorts() const noexcept override { return count; }
        int getNumOutputPorts() const noexcept override { return 1; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            std::vector<PortDescriptor> ports;
            for (int i = 0; i < count; ++i)
                ports.push_back (perChannel ({ .id = "in." + juce::String (i), .type = SignalType::Signal, .quantity = Quantity::Audio }));
            return ports;
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { perChannel ({ .id = "out", .type = SignalType::Signal, .isPrimaryOutput = true, .quantity = Quantity::Audio }) };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "life.reduce.count", .minValue = 1.0f, .maxValue = (float) maxInstances, .defaultValue = 1.0f,
                                           .isInteger = true, .isStructural = true },
                     ParameterDescriptor { .id = "life.reduce.average", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                           .isInteger = true, .isStructural = true },
                     ParameterDescriptor { .id = "life.reduce.group", .minValue = 0.0f, .maxValue = 1000.0f, .defaultValue = 0.0f,
                                           .isInteger = true, .isStructural = true },
                     ParameterDescriptor { .id = "life.reduce.generation", .minValue = 0.0f, .maxValue = 1.0e9f, .defaultValue = 0.0f,
                                           .isStructural = true } };
        }

        void setParameter (const juce::String& id, float value) override
        {
            if (id == "life.reduce.count")
                count = juce::jlimit (1, maxInstances, (int) std::lround (value));
            else if (id == "life.reduce.average")
                average = value > 0.5f;
            else if (id == "life.reduce.group")
                groupIndex = (int) std::lround (value);
        }

        bool supportsPerSample() const noexcept override { return false; }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            auto* out = outputs[0];
            std::fill (out, out + numSamples, 0.0f);
            if (runtime == nullptr)
                return;

            const auto n = juce::jmin (count, (int) runtime->instances.size());
            for (int i = 0; i < n; ++i)
            {
                auto& state = runtime->instances[(size_t) i];
                if (! state.audible)
                    continue;
                const auto* in = inputs[i];

                float peak = 0.0f;
                for (int s = 0; s < numSamples; ++s)
                    peak = juce::jmax (peak, std::abs (in[s]));
                state.peak = juce::jmax (state.peak, peak);

                if (! state.fading)
                {
                    for (int s = 0; s < numSamples; ++s)
                        out[s] += in[s];
                    continue;
                }

                // VoiceManager's steal fade, exactly as the per-voice path applied
                // it: a linear ramp over the fading samples, then nothing.
                const auto ramp = juce::jmin (numSamples, state.rampSamples);
                for (int s = 0; s < ramp; ++s)
                {
                    const auto t = ramp > 1 ? (float) s / (float) (ramp - 1) : 1.0f;
                    out[s] += in[s] * (state.gainStart + (state.gainEnd - state.gainStart) * t);
                }
            }

            if (average)
                if (const auto audible = runtime->audibleCount(); audible > 1)
                {
                    const auto scale = 1.0f / (float) audible;
                    for (int s = 0; s < numSamples; ++s)
                        out[s] *= scale;
                }
        }

    private:
        InstanceGroupRuntime* runtime = nullptr;
        int count = 1, groupIndex = 0;
        bool average = false;
    };
}
