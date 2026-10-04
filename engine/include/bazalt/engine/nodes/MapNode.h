#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "adapt.map" — Map (design/Map.png). Formerly
        `adapt.remap` ("Remap"); renamed 2026-10-04 on direct instruction
        ("The new name of remap is Map and it completely replaces Map node
        which should cease to exist"). The old two-parameter Map (a fixed
        0..1 / -1..1 input onto a min/max output) was always the special case
        of this one with the input range seeded from the source's polarity,
        so it is gone and this node is now the single rescaling adapter:
        GraphEditController::connectWithAutoAdapt seeds its input range from
        whatever feeds it (declared bounds, else polarity) and its output
        range from the destination. Patches saved with either old node are
        rewritten by PatchSerializer's migrateV7ToV8.

        The MVP of NODE_CATALOG.md's `adapt.remap` entry — that doc's full version reads an arbitrary
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
    class MapNode : public Node
    {
    public:
        static constexpr int numInputs = 5; // in, inMin, inMax, outMin, outMax
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Map"; }
        juce::String getCategory() const override { return "Adapters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            // minValue/maxValue are the actual (very wide, effectively
            // unbounded in practice) hard clamp — matching
            // NormaliseNode.h's own "-100000..100000" convention for a
            // range that's genuinely arbitrary rather than physically
            // limited. softMin/softMax (0..1) are only the slider's
            // default *visual* range — direct feedback: dragging past a
            // port's visually-shown range must still reach the value an
            // auto-seeded remap actually needs (e.g. 20000 for a
            // frequency's own max), not get clamped to whatever the
            // fallback slider background happened to show.
            return {
                PortDescriptor { .id = "in", .type = SignalType::Control, .label = "In" }, // design/Map.png: the merged pass-through row reads "In"
                PortDescriptor { .id = "adapt.map.inMin", .type = SignalType::Control, .label = "In Min",
                                  .minValue = -100000.0f, .maxValue = 100000.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .softMin = 0.0f, .softMax = 1.0f },
                PortDescriptor { .id = "adapt.map.inMax", .type = SignalType::Control, .label = "In Max",
                                  .minValue = -100000.0f, .maxValue = 100000.0f, .defaultValue = 1.0f,
                                  .hasFallbackWhenUnconnected = true, .softMin = 0.0f, .softMax = 1.0f },
                PortDescriptor { .id = "adapt.map.outMin", .type = SignalType::Control, .label = "Out Min",
                                  .minValue = -100000.0f, .maxValue = 100000.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .softMin = 0.0f, .softMax = 1.0f },
                PortDescriptor { .id = "adapt.map.outMax", .type = SignalType::Control, .label = "Out Max",
                                  .minValue = -100000.0f, .maxValue = 100000.0f, .defaultValue = 1.0f,
                                  .hasFallbackWhenUnconnected = true, .softMin = 0.0f, .softMax = 1.0f },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "adapt.map.inMin")
                inMinValue = value;
            else if (parameterId == "adapt.map.inMax")
                inMaxValue = value;
            else if (parameterId == "adapt.map.outMin")
                outMinValue = value;
            else if (parameterId == "adapt.map.outMax")
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
