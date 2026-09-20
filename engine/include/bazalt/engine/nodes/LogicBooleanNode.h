#pragma once

#include "bazalt/engine/nodes/GrowableGroupNode.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "logic.boolean" (NODE_CATALOG.md's `logic.*` row:
        `in.0…in.N` growable bool inputs; structural `op` AND, OR, XOR,
        NAND, NOR — "one node, not five"). A growable port group of
        `SignalType::Boolean` (PortGroups.h has the mechanism), 2..16
        inputs.

        Only WIRED inputs take part. Each port is declared
        `hasFallbackWhenUnconnected` purely so GraphCompiler gives an unwired
        one the NaN sentinel — the node skips it, which is the only way to
        tell "unwired" from "wired and false" (a plain 0 can't, and AND
        needs the difference: an empty spare port must not force the whole
        result false). It has no stored value and no in-node control; the
        UI shows the unwired port as a dot, exactly as `env.adsr`'s `gate`
        does. With NO wired input the output is false for every op, the
        least surprising answer for a node that has nothing to say yet.

        XOR over more than two inputs is parity (true when an odd number of
        the wired inputs are true), the standard n-ary generalisation.
        "True" is `> 0.5`, the same threshold `env.adsr`'s gate uses.
    */
    class LogicBooleanNode : public GrowableGroupNode
    {
    public:
        static constexpr int minInputs = 2;
        static constexpr int maxInputs = 16;
        static constexpr int numOutputs = 1;

        LogicBooleanNode() noexcept : GrowableGroupNode (minInputs, maxInputs) {}

        int getNumInputPorts() const noexcept override { return groupCount; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Boolean"; }
        juce::String getCategory() const override { return "Logic"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            std::vector<PortDescriptor> ports;
            ports.reserve ((size_t) groupCount);
            const auto group = groupFor ("in.");
            for (int i = 0; i < groupCount; ++i)
                ports.push_back (PortDescriptor { .id = "in." + juce::String (i),
                                                   .type = SignalType::Boolean,
                                                   .label = "In " + juce::String (i + 1),
                                                   .hasFallbackWhenUnconnected = true,
                                                   .kind = ValueKind::Bool,
                                                   .group = group });
            return ports;
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Boolean, .isPrimaryOutput = true, .kind = ValueKind::Bool } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "logic.boolean.op",
                                            .minValue = 0.0f,
                                            .maxValue = 4.0f,
                                            .defaultValue = 0.0f,
                                            .displayName = "Op",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "and", "AND" }, { "or", "OR" }, { "xor", "XOR" },
                                                              { "nand", "NAND" }, { "nor", "NOR" } },
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "logic.boolean.op")
                op = opForValue (value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            auto wired = 0;
            auto trues = 0;
            for (int i = 0; i < groupCount; ++i)
            {
                if (std::isnan (inputs[i]))
                    continue; // unwired

                ++wired;
                if (inputs[i] > 0.5f)
                    ++trues;
            }

            if (wired == 0)
            {
                outputs[0] = 0.0f;
                return;
            }

            bool result;
            switch (op)
            {
                case Op::Or:   result = trues > 0; break;
                case Op::Xor:  result = (trues % 2) == 1; break;
                case Op::Nand: result = trues != wired; break;
                case Op::Nor:  result = trues == 0; break;
                case Op::And:
                default:       result = trues == wired; break;
            }
            outputs[0] = result ? 1.0f : 0.0f;
        }

    private:
        enum class Op
        {
            And,
            Or,
            Xor,
            Nand,
            Nor
        };

        // Matches enumOptions' declaration order (and=0 ... nor=4).
        static Op opForValue (float value) noexcept
        {
            switch ((int) std::lround (value))
            {
                case 1:  return Op::Or;
                case 2:  return Op::Xor;
                case 3:  return Op::Nand;
                case 4:  return Op::Nor;
                default: return Op::And;
            }
        }

        Op op = Op::And;
    };
}
