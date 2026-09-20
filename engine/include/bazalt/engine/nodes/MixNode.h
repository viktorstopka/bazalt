#pragma once

#include "bazalt/engine/nodes/GrowableGroupNode.h"
#include <array>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "mix.sum" (renamed M14, was "mix.add2"). Sums 2..16
        Audio inputs, each with its own `level` gain companion, into one
        Audio output: `out = sum(in.i * level.i)`. Generic primitive — the
        Karplus-Strong proof graph uses it to sum the excitation with the
        feedback loop's return path (ARCHITECTURE.md §3.4).

        M21: a real growable port group with a companion (NODE_CATALOG.md:
        "each with a `level : float·Gain·0–2·log·1` companion in the same
        group"). `in.N` and `level.N` share ONE index range and grow
        together, and the ports are declared interleaved (`in.0`,
        `level.0`, `in.1`, ...) so each source sits beside its own gain.
        Ports were `a`/`b` before M21 — patch schema v3 migrates them to
        `in.0`/`in.1`; an unwired `level` reads its stored value (default 1)
        so `a + b` is bit-for-bit unchanged (x * 1.0f == x exactly), which is
        what keeps the proof graph's output identical.

        An unwired audio input reads silence — the identity for a sum, so
        the spare port and any hole cost nothing.
    */
    class MixNode : public GrowableGroupNode
    {
    public:
        static constexpr int minInputs = 2;
        static constexpr int maxInputs = 16;
        static constexpr int numOutputs = 1;

        MixNode() noexcept : GrowableGroupNode (minInputs, maxInputs) { storedLevels.fill (1.0f); }

        // Two ports per group member (the audio input and its level), which is
        // why ExecutionPlan::maxPortsPerNode is 32, not 16.
        int getNumInputPorts() const noexcept override { return groupCount * 2; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Mix"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            std::vector<PortDescriptor> ports;
            ports.reserve ((size_t) groupCount * 2);

            const auto inGroup = groupFor ("in.");
            const auto levelGroup = groupFor ("level.");

            for (int i = 0; i < groupCount; ++i)
            {
                ports.push_back (PortDescriptor { .id = "in." + juce::String (i),
                                                   .type = SignalType::Audio,
                                                   .label = "In " + juce::String (i + 1),
                                                   .group = inGroup });
                ports.push_back (PortDescriptor { .id = "level." + juce::String (i),
                                                   .type = SignalType::Control,
                                                   .label = "Level " + juce::String (i + 1),
                                                   .minValue = 0.0f,
                                                   .maxValue = 2.0f,
                                                   .defaultValue = 1.0f,
                                                   .isLogScale = true,
                                                   .hasFallbackWhenUnconnected = true,
                                                   .quantity = Quantity::Gain,
                                                   .curve = Curve::Logarithmic,
                                                   .group = levelGroup });
            }

            return ports;
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Audio, .isPrimaryOutput = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (const auto index = parsePortGroupIndex (parameterId, "level."); index >= 0 && index < maxInputs)
                storedLevels[(size_t) index] = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            auto sum = 0.0f;
            for (int i = 0; i < groupCount; ++i)
            {
                const auto levelInput = inputs[i * 2 + 1];
                sum += inputs[i * 2] * (std::isnan (levelInput) ? storedLevels[(size_t) i] : levelInput);
            }
            outputs[0] = sum;
        }

    private:
        std::array<float, maxInputs> storedLevels {};
    };
}
