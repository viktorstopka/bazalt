#pragma once

#include "bazalt/engine/nodes/GrowableGroupNode.h"
#include <array>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.multiply" (renamed M14, was "util.multiply").
        Multiplies 2..16 numeric (Control) inputs — the other half of
        Alt-drag's number+number case (NODE_EDITOR.md §7), alongside
        AddNode, and growable the same way (see AddNode.h for the M21
        mechanism, the `a`/`b` -> `in.0`/`in.1` schema-v3 migration and the
        stored-fallback-value semantics).

        The one difference that matters: an unwired input's stored value
        defaults to 1, the identity for a product. That is what stops the
        spare port revealed for the next cable (or a hole left by a removed
        one) from zeroing the whole result, which reading plain silence
        would.
    */
    class MultiplyNode : public GrowableGroupNode
    {
    public:
        static constexpr int minInputs = 2;
        static constexpr int maxInputs = 16;
        static constexpr int numOutputs = 1;

        MultiplyNode() noexcept : GrowableGroupNode (minInputs, maxInputs) { storedValues.fill (1.0f); }

        int getNumInputPorts() const noexcept override { return groupCount; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Multiply"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            std::vector<PortDescriptor> ports;
            ports.reserve ((size_t) groupCount);
            const auto group = groupFor ("in.");
            for (int i = 0; i < groupCount; ++i)
                ports.push_back (makeNumericGroupPort (group, i, 1.0f));
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
            auto product = 1.0f;
            for (int i = 0; i < groupCount; ++i)
                product *= std::isnan (inputs[i]) ? storedValues[(size_t) i] : inputs[i];
            outputs[0] = product;
        }

    private:
        std::array<float, maxInputs> storedValues {};
    };
}
