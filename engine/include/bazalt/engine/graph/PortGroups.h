#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>
#include <vector>

namespace bazalt::engine
{
    /** Growable port groups (SIGNAL_TYPES.md §6, `PortGroup` in
        PortDescriptor.h) — the shared, node-agnostic half of the mechanism.

        How it fits together: a node that takes "N of the same thing"
        (math.add, mix.sum, logic.and/or/xor) tags each of its group ports with
        a `PortGroup` and reports how many it currently declares via
        `Node::getGroupPortCount()`. The graph never stores that count — it
        is DERIVED, at compile time, from the connections: the highest
        referenced index + 1, held within [minCount, maxCount]
        (`requiredPortGroupCount`). GraphCompiler hands the result to
        `Node::setGroupPortCount()` before it reads the node's ports, so an
        `in.5` connection makes `in.0..in.5` exist. Nothing extra is
        persisted, so a patch can never disagree with itself about how many
        ports a node has, and removing a middle connection leaves a "hole"
        (an unconnected port between wired ones) exactly as the design says,
        never a renumbering.

        Port IDs are `idPrefix + index` with the index in canonical decimal
        ("in.1", never "in.01"), so an ID names exactly one port.
    */

    /** `portId`'s index within the group whose `idPrefix` it starts with, or
        -1 if `portId` isn't `idPrefix` followed by a canonical non-negative
        decimal integer.
    */
    inline int parsePortGroupIndex (const juce::String& portId, const juce::String& idPrefix)
    {
        if (idPrefix.isEmpty() || ! portId.startsWith (idPrefix))
            return -1;

        const auto suffix = portId.substring (idPrefix.length());
        if (suffix.isEmpty() || suffix.length() > 4 || ! suffix.containsOnly ("0123456789"))
            return -1;

        const auto index = suffix.getIntValue();
        return juce::String (index) == suffix ? index : -1; // rejects "01", "007"
    }

    /** The distinct groups declared by `ports` (one entry per idPrefix). */
    inline std::vector<PortGroup> collectPortGroups (const std::vector<PortDescriptor>& ports)
    {
        std::vector<PortGroup> groups;
        for (const auto& port : ports)
        {
            if (! port.group.has_value())
                continue;

            const auto known = std::any_of (groups.begin(), groups.end(),
                                             [&] (const PortGroup& g) { return g.idPrefix == port.group->idPrefix; });
            if (! known)
                groups.push_back (*port.group);
        }
        return groups;
    }

    /** The group index `portId` refers to on a node that declares `ports`, or
        -1 if it isn't a member of any of that node's groups.
    */
    inline int portGroupIndexOf (const std::vector<PortDescriptor>& ports, const juce::String& portId)
    {
        for (const auto& group : collectPortGroups (ports))
            if (const auto index = parsePortGroupIndex (portId, group.idPrefix); index >= 0)
                return index;
        return -1;
    }

    /** How many ports of `node`'s growable group must exist for every
        connection into it to resolve, given the port IDs of those
        connections: the highest referenced group index + 1, held within
        [minCount, maxCount]. -1 if `node` has no group at all. An index
        >= maxCount is clamped here on purpose — the compiler then reports
        the over-range port as an ordinary unknown port, which is the right
        rejection.

        A node's groups share one index range (mix.sum's `in.N` and its
        `level.N` companion both grow together), so a connection to any of
        them counts.

        Takes just the incoming port IDs, not the whole graph: GraphCompiler
        indexes those by node ONCE per compile, since scanning every
        connection per group node made a 500-node graph quadratic.
    */
    inline int requiredPortGroupCount (const Node& node, const std::vector<juce::String>& incomingPortIds)
    {
        const auto groups = collectPortGroups (node.getInputPorts());
        if (groups.empty())
            return -1;

        auto highest = -1;
        for (const auto& portId : incomingPortIds)
            for (const auto& group : groups)
                highest = std::max (highest, parsePortGroupIndex (portId, group.idPrefix));

        return juce::jlimit (groups.front().minCount, groups.front().maxCount, highest + 1);
    }
}
