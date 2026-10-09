#pragma once

#include "bazalt/engine/nodes/InheritingPortsNode.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "logic.compare" (NODE_CATALOG.md's `logic.*` row:
        `a`, `b` (same quantity required), `tolerance`; structural `op`
        >, >=, =, !=, <=, < -> bool; "`=` uses `tolerance`, not exact float
        equality").

        "Same quantity required" is what the type/quantity inheritance is
        for: `a`, `b` and `tolerance` all take on one Quantity, from `a` if
        it's wired, else `b`, else `tolerance` (InheritingPortsNode.h), so
        comparing a Frequency with a Pitch is caught by canConnect() at the
        second cable instead of silently comparing 440 with 69. The values
        themselves are always Control.

        `tolerance` (an absolute difference, default 0.001, in the compared
        quantity's own unit) applies only to `=` and `!=`: `=` is
        `|a - b| <= tolerance`, `!=` its exact negation. The four ordering ops
        are exact, since a tolerance on `>` would make "greater" and "equal"
        overlap. A NaN on either side makes every op false, `!=` included
        (`|a - b| > tolerance` is false for NaN), so a NaN never reads as a
        real answer in either direction.
    */
    class LogicCompareNode : public InheritingPortsNode
    {
    public:
        static constexpr int numInputs = 3; // a, b, tolerance
        static constexpr int numOutputs = 1;

        LogicCompareNode() noexcept : InheritingPortsNode (Quantity::Dimensionless) {}

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Compare"; }
        juce::String getCategory() const override { return "Logic"; }

        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            if (toPortId == "a")
                offer (0, source, false);
            else if (toPortId == "b")
                offer (1, source, false);
            else if (toPortId == "logic.compare.tolerance")
                offer (2, source, false);
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "a", .type = SignalType::Signal, .label = "A",
                                  .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::Quantity },
                PortDescriptor { .id = "b", .type = SignalType::Signal, .label = "B",
                                  .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::Quantity },
                PortDescriptor { .id = "logic.compare.tolerance", .type = SignalType::Signal, .label = "Tolerance",
                                  .minValue = 0.0f, .defaultValue = 0.001f, .hasFallbackWhenUnconnected = true,
                                  .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::Quantity },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Signal, .isPrimaryOutput = true, .kind = ValueKind::Bool, .quantity = Quantity::Boolean } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "logic.compare.op",
                                            .minValue = 0.0f,
                                            .maxValue = 5.0f,
                                            .defaultValue = 0.0f,
                                            .displayName = "Op",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "gt", ">" }, { "gte", ">=" }, { "eq", "=" },
                                                              { "neq", "!=" }, { "lte", "<=" }, { "lt", "<" } },
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "logic.compare.op")
                op = opForValue (value);
            else if (parameterId == "logic.compare.tolerance")
                storedTolerance = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto a = inputs[0];
            const auto b = inputs[1];
            const auto tolerance = juce::jmax (0.0f, std::isnan (inputs[2]) ? storedTolerance : inputs[2]);

            bool result;
            switch (op)
            {
                case Op::Gte: result = a >= b; break;
                case Op::Eq:  result = std::fabs (a - b) <= tolerance; break;
                case Op::Neq: result = std::fabs (a - b) > tolerance; break;
                case Op::Lte: result = a <= b; break;
                case Op::Lt:  result = a < b; break;
                case Op::Gt:
                default:      result = a > b; break;
            }
            outputs[0] = result ? 1.0f : 0.0f;
        }

    private:
        enum class Op
        {
            Gt,
            Gte,
            Eq,
            Neq,
            Lte,
            Lt
        };

        // Matches enumOptions' declaration order (gt=0 ... lt=5).
        static Op opForValue (float value) noexcept
        {
            switch ((int) std::lround (value))
            {
                case 1:  return Op::Gte;
                case 2:  return Op::Eq;
                case 3:  return Op::Neq;
                case 4:  return Op::Lte;
                case 5:  return Op::Lt;
                default: return Op::Gt;
            }
        }

        Op op = Op::Gt;
        float storedTolerance = 0.001f; // matches the port's own declared defaultValue
    };
}
