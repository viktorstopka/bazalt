#pragma once

#include "bazalt/engine/nodes/GrowableGroupNode.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    enum class LogicGateOp
    {
        And,
        Or,
        Xor
    };

    /** Stable type ids: "logic.and", "logic.or", "logic.xor" — And / Or / Xor.
        Replaced the single "logic.boolean" node with an Op menu (2026-10-04,
        direct instruction: "Create logic nodes: Or, And, Xor"); a patch that
        used it is migrated on load (PatchSerializer v9 -> v10).

        Growable Boolean inputs `in.0…in.N` (2..16), exactly like math.add's
        (PortGroups.h). Only WIRED inputs take part: each port is declared
        `hasFallbackWhenUnconnected` purely so an unwired one reads the NaN
        sentinel and is skipped — the only way to tell "unwired" from "wired
        and false" (an empty spare port must not force And false). No wired
        input at all gives false.

        Xor over more than two inputs is parity (true when an odd number of
        the wired inputs are true). `Invert` negates the result — Nand, Nor
        and Xnor without three more nodes. "True" is `> 0.5`.
    */
    class LogicGateNode : public GrowableGroupNode
    {
    public:
        static constexpr int minInputs = 2;
        static constexpr int maxInputs = 16;

        explicit LogicGateNode (LogicGateOp opToUse)
            : GrowableGroupNode (minInputs, maxInputs), op (opToUse), invertId (typeId() + ".invert")
        {
        }

        int getNumInputPorts() const noexcept override { return groupCount; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override
        {
            switch (op)
            {
                case LogicGateOp::And: return "And";
                case LogicGateOp::Or:  return "Or";
                case LogicGateOp::Xor: return "Xor";
            }
            return {};
        }

        juce::String getCategory() const override { return "Logic"; }
        juce::String typeId() const { return "logic." + getTitle().toLowerCase(); }

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
            return { PortDescriptor { .id = "out", .type = SignalType::Boolean, .label = "Out", .isPrimaryOutput = true, .kind = ValueKind::Bool } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = invertId, .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                           .displayName = "Invert", .isInteger = true, .kind = ValueKind::Bool } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == invertId)
                invert = value >= 0.5f;
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

            bool result = false;
            switch (op)
            {
                case LogicGateOp::And: result = trues == wired; break;
                case LogicGateOp::Or:  result = trues > 0; break;
                case LogicGateOp::Xor: result = (trues % 2) == 1; break;
            }
            outputs[0] = (result != invert) ? 1.0f : 0.0f;
        }

    private:
        LogicGateOp op;
        juce::String invertId; // built once: setParameter may run on the audio thread
        bool invert = false;
    };

    struct LogicAndNode : LogicGateNode { LogicAndNode() : LogicGateNode (LogicGateOp::And) {} };
    struct LogicOrNode : LogicGateNode { LogicOrNode() : LogicGateNode (LogicGateOp::Or) {} };
    struct LogicXorNode : LogicGateNode { LogicXorNode() : LogicGateNode (LogicGateOp::Xor) {} };
}
