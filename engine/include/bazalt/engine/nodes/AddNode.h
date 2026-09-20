#pragma once

#include "bazalt/engine/nodes/GrowableGroupNode.h"
#include <array>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.add" (renamed M14, was "util.add"). Sums 2..16
        numeric (Control) inputs — spawned by Alt-dragging between two nodes
        whose primary outputs are both numbers (NODE_EDITOR.md §7's Alt-drag
        Mix/Add/Multiply; Mix itself reuses "mix.sum" for the audio case).

        M21: a real growable port group (`in.0..in.N`, PortGroups.h has the
        mechanism). Ports were `a`/`b` before M21 — patch schema v3
        migrates them to `in.0`/`in.1` (CLAUDE.md rule 3: an ID is never
        just renamed; the migration is what keeps old patches loading).

        An unwired input isn't ignored, it reads its own stored value
        (default 0, the identity for a sum), which is what NodeCard shows as
        that port's in-node slider — so "A + 5" needs no Constant node. The
        spare port revealed for the next cable, and a hole left by a removed
        one, therefore contribute nothing until something is wired or typed.
    */
    class AddNode : public GrowableGroupNode
    {
    public:
        static constexpr int minInputs = 2;
        static constexpr int maxInputs = 16;
        static constexpr int numOutputs = 1;

        AddNode() noexcept : GrowableGroupNode (minInputs, maxInputs) {}

        int getNumInputPorts() const noexcept override { return groupCount; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Add"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            std::vector<PortDescriptor> ports;
            ports.reserve ((size_t) groupCount);
            const auto group = groupFor ("in.");
            for (int i = 0; i < groupCount; ++i)
                ports.push_back (makeNumericGroupPort (group, i, 0.0f));
            return ports;
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (const auto index = parsePortGroupIndex (parameterId, "in."); index >= 0 && index < maxInputs)
                storedValues[(size_t) index] = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            auto sum = 0.0f;
            for (int i = 0; i < groupCount; ++i)
                sum += std::isnan (inputs[i]) ? storedValues[(size_t) i] : inputs[i];
            outputs[0] = sum;
        }

    private:
        std::array<float, maxInputs> storedValues {}; // identity 0 until an in-node value is set
    };
}
