#pragma once

#include "bazalt/engine/nodes/GrowableGroupNode.h"
#include <array>
#include <cmath>
#include <limits>

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.multiply" (renamed M14, was "util.multiply").
        Multiplies 2..16 inputs — the other half of Alt-drag's number+number
        case (NODE_EDITOR.md §7), alongside AddNode, and growable the same
        way (see AddNode.h for the M21 mechanism, the `a`/`b` -> `in.0`/
        `in.1` schema-v3 migration and the stored-fallback-value semantics).

        wiki/plans/DomainRedesign.md Batch 3: genuinely polymorphic now
        (`PortPolymorphism::SignalAndQuantity`, same mechanism as AddNode's
        own — see its comment for the full reasoning), correcting a real
        documentation-level claim (wiki/NODES.md said this node "already
        doubles as ring-mod" for audio-rate signals; verified against the
        real source during this redesign's own plan-mode pass and found
        false — its ports were fixed Control until this fix). Two Poly
        Audio-rate signals into this node's growable ports, both from the
        SAME voice-allocator origin, is exactly detuned self-ring-mod per
        note (wiki/plans/DomainRedesign.md §5.3) — no special case needed,
        it's just an ordinary polymorphic node under the Multiplicity
        resolver's general rule.

        The one difference from AddNode that matters: an unwired input's
        stored value defaults to 1, the identity for a product. That is
        what stops the spare port revealed for the next cable (or a hole
        left by a removed one) from zeroing the whole result, which reading
        plain silence would.
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

        bool hasPolymorphicPorts() const noexcept override { return true; }

        // Same rule AddNode::resolveIncomingPort() already establishes —
        // see its own comment (including the lenient, unanimous-agreement-
        // only Quantity rule, and why: math.multiply's ring-mod use case
        // is exactly "combine two things with no reason to share a
        // quantity").
        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            const auto isPlainValue = source.type == SignalType::Audio || source.type == SignalType::Control
                                       || source.type == SignalType::Boolean || source.type == SignalType::Event;
            const auto priority = parsePortGroupIndex (toPortId, "in.");
            if (! isPlainValue || priority < 0 || priority >= maxInputs)
                return;

            if (priority <= bestTypePriority)
            {
                bestTypePriority = priority;
                resolvedType = source.type;
            }

            perPortQuantity[(size_t) priority] = source.quantity;
            perPortHasQuantity[(size_t) priority] = true;

            auto agreed = Quantity::Dimensionless;
            auto anyReal = false;
            auto conflict = false;
            for (int i = 0; i < maxInputs; ++i)
            {
                if (! perPortHasQuantity[(size_t) i] || perPortQuantity[(size_t) i] == Quantity::Dimensionless)
                    continue;

                if (! anyReal)
                {
                    agreed = perPortQuantity[(size_t) i];
                    anyReal = true;
                }
                else if (perPortQuantity[(size_t) i] != agreed)
                {
                    conflict = true;
                    break;
                }
            }
            resolvedQuantity = (anyReal && ! conflict) ? agreed : Quantity::Dimensionless;
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            std::vector<PortDescriptor> ports;
            ports.reserve ((size_t) groupCount);
            const auto group = groupFor ("in.");
            for (int i = 0; i < groupCount; ++i)
            {
                auto port = makeNumericGroupPort (group, i, 1.0f);
                port.type = resolvedType;
                port.quantity = resolvedQuantity;
                port.polymorphism = PortPolymorphism::SignalAndQuantity;
                ports.push_back (port);
            }
            return ports;
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = resolvedType, .isPrimaryOutput = true,
                                       .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::SignalAndQuantity } };
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
        SignalType resolvedType = SignalType::Control; // unresolved default — every existing math.multiply's own prior behaviour
        Quantity resolvedQuantity = Quantity::Dimensionless;
        int bestTypePriority = std::numeric_limits<int>::max();
        std::array<Quantity, maxInputs> perPortQuantity {};
        std::array<bool, maxInputs> perPortHasQuantity {};
    };
}
