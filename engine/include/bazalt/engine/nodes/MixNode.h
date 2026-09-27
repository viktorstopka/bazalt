#pragma once

#include "bazalt/engine/nodes/GrowableGroupNode.h"
#include <array>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "mix.sum" (renamed M14, was "mix.add2"). Sums 2..16
        Audio inputs into one Audio output: `out = sum(in.i)`. Generic
        primitive — the Karplus-Strong proof graph uses it to sum the
        excitation with the feedback loop's return path (ARCHITECTURE.md
        §3.4).

        wiki/NODES_Gaps.md's `redundant-composable-param` finding
        (confirmed, fixed): each input used to carry its own baked-in
        `level.N` gain — a second, node-internal way to do exactly what a
        `mix.gain` node placed in front of that input already does, with no
        way to see or modulate it as a real graph element (only host
        automation or a direct in-node slider drag could ever touch it).
        Schema v4 (`PatchDocument.h`, `PatchSerializer.cpp`'s v3→v4
        migration) removes it: an old patch's non-default/connected
        `level.N` values are migrated into a real, visible, spliced-in
        `mix.gain` node instead of being silently dropped — see that
        migration's own comment for the exact preservation rule. A patch
        that never touched `level.N` (left every one at its default 1.0,
        unconnected) needs no migration at all; this node's summing
        behaviour for that case is bit-for-bit unchanged.

        An unwired audio input reads silence — the identity for a sum, so
        the spare port and any hole cost nothing.
    */
    class MixNode : public GrowableGroupNode
    {
    public:
        static constexpr int minInputs = 2;
        static constexpr int maxInputs = 16;
        static constexpr int numOutputs = 1;

        MixNode() noexcept : GrowableGroupNode (minInputs, maxInputs) {}

        int getNumInputPorts() const noexcept override { return groupCount; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Mix"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            std::vector<PortDescriptor> ports;
            ports.reserve ((size_t) groupCount);

            const auto inGroup = groupFor ("in.");

            for (int i = 0; i < groupCount; ++i)
            {
                ports.push_back (PortDescriptor { .id = "in." + juce::String (i),
                                                   .type = SignalType::Audio,
                                                   .label = "In " + juce::String (i + 1),
                                                   .group = inGroup });
            }

            return ports;
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Audio, .isPrimaryOutput = true } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            auto sum = 0.0f;
            for (int i = 0; i < groupCount; ++i)
                sum += inputs[i];
            outputs[0] = sum;
        }
    };
}
