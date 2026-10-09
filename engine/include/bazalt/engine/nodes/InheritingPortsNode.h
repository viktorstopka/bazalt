#pragma once

#include "bazalt/engine/graph/Node.h"
#include <limits>

namespace bazalt::engine::nodes
{
    /** Shared base for the nodes whose data ports take on the type and/or
        quantity of what is wired to them (logic.select, logic.compare,
        adapt.sampleHold) — the multi-input generalisation of what
        `deco.reroute` does for one port. See Node.h's
        `resolveIncomingPort()` for how the compiler drives this, and
        PortDescriptor::polymorphic for what the UI mirrors.

        A subclass calls `offer()` from `resolveIncomingPort()` with the
        priority of the port the source arrived on (0 = highest; by
        convention the port's declaration order, which is exactly the rule the
        UI applies) and reads `resolvedType`/`resolvedQuantity` when it
        declares its ports.

        Two rules keep it deterministic:
        - Priority, never arrival order. The compiler re-offers to a fixed
          point, so a source that is itself an unresolved Reroute reports its
          default on an early pass and its real type later; the lowest-numbered
          priority seen so far always wins, and the latest offer FROM that
          priority is the one applied (`<=`, not `<`), so a chain resolves.
        - Only sources with a plain per-sample value count. A Note has a
          one-input-per-node limit in the compiler and Data/Spectral are
          different runtime representations; ignoring them leaves the ports at
          their defaults, so the compiler's canConnect pass reports the ordinary
          type mismatch rather than this node pretending to carry them.
    */
    class InheritingPortsNode : public Node
    {
    public:
        bool hasPolymorphicPorts() const noexcept override { return true; }

    protected:
        /** \p defaultQuantity is what the ports declare until something is
            wired: Quantity::Audio for a node that is mostly fed sound (Clip,
            Blend, Cycle, Meter), Dimensionless for a plain value. */
        explicit InheritingPortsNode (Quantity defaultQuantity) noexcept : resolvedQuantity (defaultQuantity) {}

        /** \p inheritType false keeps the type a Signal (logic.compare's values
            never become Events) and adopts only the quantity.
        */
        void offer (int priority, const PortDescriptor& source, bool inheritType) noexcept
        {
            const auto isPlainValue = source.type == SignalType::Signal || source.type == SignalType::Event;
            if (! isPlainValue || priority > bestPriority)
                return;

            bestPriority = priority;
            if (inheritType)
                resolvedType = source.type;
            resolvedQuantity = source.quantity;
        }

        SignalType resolvedType = SignalType::Signal;
        Quantity resolvedQuantity;

    private:
        int bestPriority = std::numeric_limits<int>::max();
    };
}
