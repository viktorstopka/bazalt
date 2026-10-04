#pragma once

#include "bazalt/engine/nodes/GrowableGroupNode.h"
#include <array>
#include <cmath>
#include <limits>

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.add" (renamed M14, was "util.add"). Sums 2..16
        inputs — spawned by Alt-dragging between two nodes whose primary
        outputs are both numbers (NODE_EDITOR.md §7's Alt-drag Mix/Add/
        Multiply).

        M21: a real growable port group (`in.0..in.N`, PortGroups.h has the
        mechanism). Ports were `a`/`b` before M21 — patch schema v3
        migrates them to `in.0`/`in.1` (CLAUDE.md rule 3: an ID is never
        just renamed; the migration is what keeps old patches loading).

        wiki/plans/DomainRedesign.md Batch 3: `mix.sum` (Audio-only, no
        stored-value fallback) folded straight into this node outright —
        read both `MixNode.h` (deleted) and this file directly and they were
        almost line-for-line the same node; the only real differences were
        Audio-vs-Control ports and this node's own stored-fallback
        convenience, which an unwired Audio input never needed (silence is
        already the sum identity). Now genuinely polymorphic
        (`PortPolymorphism::SignalAndQuantity`, the same mechanism
        `deco.reroute`/`logic.select` already use — declared first among
        the group's wired members wins if they disagree, InheritingPortsNode.h's
        own `offer()` rule, reimplemented here directly since this node's
        growable-group shape doesn't fit that class's fixed-arity one):
        Audio in, Audio out, still summed the exact same way (Audio and
        Control share one underlying per-sample-float representation,
        wiki/plans/AudioControlBridge.md §2). Channels (mono/stereo)
        needs no new handling — an unwired growable port stays declared
        Mono, and canConnect's existing Mono<->Stereo rules apply exactly
        as they would to any other Mono Audio port.

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
        juce::String getCategory() const override { return "Math"; }

        bool hasPolymorphicPorts() const noexcept override { return true; }

        // SignalType: same priority rule InheritingPortsNode::offer()
        // already establishes (Node.h's own doc comment on
        // resolveIncomingPort) — the lowest-numbered still-wired member
        // (declaration order, "in.0" first) wins if members disagree; only
        // a plain per-sample value (Audio/Control/Boolean/Event) counts as
        // a source at all. Genuinely mixing those (an Audio cable and a
        // Control cable both feeding one sum) is rare and, if it happens,
        // one of them IS the odd one out — priority is the right call.
        //
        // Quantity: deliberately LENIENT, not the same priority rule — a
        // real, found-live design bug caught by HostInputTests.cpp's own
        // "combine io.control + io.transport through math.add" case:
        // summing N inputs of genuinely DIFFERENT real quantities (a pitch
        // offset + a dimensionless LFO wobble + a raw modulation amount)
        // is an ordinary, pre-existing, legitimate math.add pattern
        // (buildInitPatchGraph()'s own detuneSum/cutoffSum both do it) —
        // unlike logic.select's whenTrue/whenFalse (genuinely "the same
        // kind of thing, chosen conditionally"), math.add's inputs have no
        // reason to agree at all. Resolving to whichever quantity the
        // lowest-index port happens to carry made canConnect's own strict
        // real-quantity matching (Frequency vs Time, say) spuriously
        // reject a SECOND input math.add always accepted before it became
        // polymorphic. Only resolves to a specific quantity when every
        // quantity-declaring input unanimously agrees; any disagreement
        // (or nothing declared at all) falls back to Dimensionless,
        // canConnect's own universal wildcard — exactly this node's
        // pre-polymorphism behaviour for that case. Recomputed fresh from
        // every port's own last-known offer on every call (not accumulated
        // sequentially), so a later re-offer during the compiler's
        // fixed-point iteration — a chain of Reroutes resolving after this
        // node's first pass — can un-stick an earlier disagreement.
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
                auto port = makeNumericGroupPort (group, i, 0.0f);
                port.type = resolvedType;
                port.quantity = resolvedQuantity;
                port.channels = Channels::Inherited;
                port.polymorphism = PortPolymorphism::SignalAndQuantity;
                ports.push_back (port);
            }
            return ports;
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = resolvedType, .isPrimaryOutput = true,
                                       .quantity = resolvedQuantity, .channels = Channels::Inherited, .polymorphism = PortPolymorphism::SignalAndQuantity } };
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
        SignalType resolvedType = SignalType::Control; // unresolved default — every existing math.add's own prior behaviour
        Quantity resolvedQuantity = Quantity::Dimensionless;
        int bestTypePriority = std::numeric_limits<int>::max();
        std::array<Quantity, maxInputs> perPortQuantity {};
        std::array<bool, maxInputs> perPortHasQuantity {};
    };
}
