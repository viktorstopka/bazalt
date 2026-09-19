#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "adapt.remap". The MVP of NODE_CATALOG.md's
        `adapt.remap` entry — that doc's full version reads an arbitrary
        hand-drawn `Data(curve)` (with a second curve to morph towards),
        which needs `data.table`/the curve-editing UI that doesn't exist
        yet (M24 territory). This is the same node, same stable id,
        started at the linear special case: a plain (in - inMin) /
        (inMax - inMin) rescale into [outMin, outMax], clamped. Curve
        support is meant to grow this node later, not replace it with a
        different one.

        Direct feedback: connecting two Control ports with different real
        quantities (e.g. an absolute-pitch output into a Hz-quantity
        cutoff — pitch-tracking a filter, a standard technique) used to be
        a bare Reject; `CanConnect.cpp`'s `connectControl()` auto-inserts
        one of these instead, seeded from both endpoints' own ranges
        (inMin/inMax from the source, outMin/outMax from the destination)
        — a real, visible, editable node, never a hidden conversion.
        `inMin`/`inMax`/`outMin`/`outMax` are real ports (not parameters),
        matching this whole session's "almost every value should be
        modulatable" rule — a live-modulated remap range is a real,
        useful thing (e.g. an envelope driving how wide the output swings).
    */
    class RemapNode : public Node
    {
    public:
        static constexpr int numInputs = 5; // in, inMin, inMax, outMin, outMax
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Remap"; }
        juce::String getCategory() const override { return "Adapters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { "in", SignalType::Control },
                PortDescriptor { .id = "adapt.remap.inMin", .type = SignalType::Control, .label = "In Min",
                                  .defaultValue = 0.0f, .hasFallbackWhenUnconnected = true },
                PortDescriptor { .id = "adapt.remap.inMax", .type = SignalType::Control, .label = "In Max",
                                  .defaultValue = 1.0f, .hasFallbackWhenUnconnected = true },
                PortDescriptor { .id = "adapt.remap.outMin", .type = SignalType::Control, .label = "Out Min",
                                  .defaultValue = 0.0f, .hasFallbackWhenUnconnected = true },
                PortDescriptor { .id = "adapt.remap.outMax", .type = SignalType::Control, .label = "Out Max",
                                  .defaultValue = 1.0f, .hasFallbackWhenUnconnected = true },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "adapt.remap.inMin")
                inMinValue = value;
            else if (parameterId == "adapt.remap.inMax")
                inMaxValue = value;
            else if (parameterId == "adapt.remap.outMin")
                outMinValue = value;
            else if (parameterId == "adapt.remap.outMax")
                outMaxValue = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto in = inputs[0];
            const auto inMin = std::isnan (inputs[1]) ? inMinValue : inputs[1];
            const auto inMax = std::isnan (inputs[2]) ? inMaxValue : inputs[2];
            const auto outMin = std::isnan (inputs[3]) ? outMinValue : inputs[3];
            const auto outMax = std::isnan (inputs[4]) ? outMaxValue : inputs[4];

            const auto range = inMax - inMin;
            const auto normalized = range != 0.0f ? (in - inMin) / range : 0.0f;
            const auto clamped = std::clamp (normalized, 0.0f, 1.0f);
            outputs[0] = outMin + clamped * (outMax - outMin);
        }

    private:
        float inMinValue = 0.0f;
        float inMaxValue = 1.0f;
        float outMinValue = 0.0f;
        float outMaxValue = 1.0f;
    };
}
