#include "bazalt/engine/graph/MultiplicityResolver.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bazalt::engine
{
    namespace
    {
        // Batch 1b (wiki/plans/DomainRedesign.md sec 10.3) renamed this from
        // "instance.mix" — kept as one isolated constant so that rename was a
        // one-line diff here, not a re-derivation of the algorithm below.
        constexpr const char* instanceSumTypeId = "instance.sum";
        constexpr const char* instanceSumInputPortId = "in";

        /** Domain Extensions batch: every node type that opens an instanced
            (Poly) region is an ORIGIN to this algorithm, regardless of which
            of the four spawn mechanisms it is (DOMAINS.md §3) — the
            fixed-point propagation below operates purely on origin id
            strings once this set has identified them, with zero further
            dependency on which concrete node type produced a given origin.
            A fixed set of constants, not a generic "ask the node itself"
            marker, matching this file's own existing style (named
            constants) and because NodeGraph's plain {id,type,...} structs
            have no node OBJECT to ask here at all — only the raw type
            string, same reason instanceSumTypeId above is a string too.
        */
        constexpr const char* instanceOriginTypeIds[] = {
            "instance.allocate.voice",
            "instance.allocate.swarmPopulation",
            "instance.allocate.swarmTransient",
            "instance.allocate.trigger",
        };

        bool isInstanceOriginType (const juce::String& type) noexcept
        {
            for (const auto* originTypeId : instanceOriginTypeIds)
                if (type == originTypeId)
                    return true;
            return false;
        }
    }

    MultiplicityResult MultiplicityResolver::split (const NodeGraph& graph)
    {
        MultiplicityResult result;
        const auto& nodes = graph.getNodes();
        const auto& connections = graph.getConnections();

        std::vector<juce::String> originIds;
        std::vector<juce::String> sumIds;
        for (const auto& node : nodes)
        {
            if (isInstanceOriginType (node.type))
                originIds.push_back (node.id);
            else if (node.type == instanceSumTypeId)
                sumIds.push_back (node.id);
        }

        if ((int) originIds.size() > maxOrigins)
        {
            result.errorMessage = "Only up to " + juce::String (maxOrigins)
                                   + " instance-allocating nodes (instance.allocate.voice/swarmPopulation/"
                                     "swarmTransient/trigger) are supported per graph (found "
                                   + juce::String ((int) originIds.size()) + ")";
            return result;
        }

        if (originIds.empty() && sumIds.empty())
        {
            // No allocator, no reducer: nothing in this graph is ever Poly
            // (DOMAINS.md §7's "the allocator's outputs are what make a
            // region poly, so without one nothing is per-instance") — the
            // whole graph is Scalar and runs once, every block.
            result.success = true;
            result.monoOnly = true;
            result.globalGraph = graph;
            return result;
        }

        std::unordered_map<juce::String, std::vector<juce::String>> successorsOf, predecessorsOf;
        for (const auto& connection : connections)
        {
            successorsOf[connection.fromNodeId].push_back (connection.toNodeId);
            predecessorsOf[connection.toNodeId].push_back (connection.fromNodeId);
        }

        const std::unordered_set<juce::String> originIdSet (originIds.begin(), originIds.end());
        const std::unordered_set<juce::String> sumIdSet (sumIds.begin(), sumIds.end());

        // nodeId -> the origin it resolved to. Absence means Scalar. An
        // origin allocator seeds itself; every other node is derived below.
        std::unordered_map<juce::String, juce::String> resolvedOrigin;
        for (const auto& originId : originIds)
            resolvedOrigin[originId] = originId;

        // Fixed-point forward pass (step 2 of the class doc comment):
        // resolvedOrigin only ever grows, so this converges in at most
        // nodes.size() passes.
        bool changed = true;
        while (changed)
        {
            changed = false;

            for (const auto& node : nodes)
            {
                if (originIdSet.count (node.id) > 0 || sumIdSet.count (node.id) > 0)
                    continue; // fixed by type id — never re-derived from its own inputs here

                const auto predIt = predecessorsOf.find (node.id);
                if (predIt == predecessorsOf.end())
                    continue; // no inputs at all — stays Scalar (unless backward-pulled, step 4)

                juce::String foundOrigin;
                for (const auto& fromId : predIt->second)
                {
                    const auto it = resolvedOrigin.find (fromId);
                    if (it == resolvedOrigin.end())
                        continue; // a Scalar predecessor contributes nothing here

                    if (foundOrigin.isEmpty())
                    {
                        foundOrigin = it->second;
                    }
                    else if (foundOrigin != it->second)
                    {
                        result.errorMessage = "Node '" + node.id + "' is fed by two different voice allocators ('"
                                               + foundOrigin + "', '" + it->second
                                               + "') and can't combine them directly — reduce one to Scalar (with its own instance.sum) first";
                        return result;
                    }
                }

                if (foundOrigin.isNotEmpty() && resolvedOrigin.find (node.id) == resolvedOrigin.end())
                {
                    resolvedOrigin[node.id] = foundOrigin;
                    changed = true;
                }
            }
        }

        // Step 3: every instance.sum's "in" port must resolve Poly. Also
        // collects, per origin, which single instance.sum reduces it (a
        // second one for the SAME origin is an error; two DIFFERENT
        // origins each with their own is not — DomainRedesign.md §4).
        std::unordered_map<juce::String, juce::String> sumForOrigin; // originId -> its one instance.sum node id
        std::unordered_map<juce::String, juce::String> originForSum; // instance.sum node id -> the origin it reduces

        for (const auto& sumId : sumIds)
        {
            juce::String feedingNodeId;
            int incomingCount = 0;
            for (const auto& connection : connections)
            {
                if (connection.toNodeId == sumId && connection.toPortId == instanceSumInputPortId)
                {
                    feedingNodeId = connection.fromNodeId;
                    ++incomingCount;
                }
            }

            if (incomingCount == 0)
                continue; // freshly placed, not yet wired — no boundary active yet, not an error (M19's own carve-out, still true here)

            if (incomingCount > 1)
            {
                result.errorMessage = "instance.sum must have at most one connection into its 'in' port (found "
                                       + juce::String (incomingCount) + ")";
                return result;
            }

            const auto originIt = resolvedOrigin.find (feedingNodeId);
            if (originIt == resolvedOrigin.end())
            {
                result.errorMessage = "instance.sum '" + sumId + "'s input must be a Poly signal — nothing to reduce";
                return result;
            }

            const auto originId = originIt->second;
            originForSum[sumId] = originId;

            const auto existing = sumForOrigin.find (originId);
            if (existing != sumForOrigin.end())
            {
                result.errorMessage = "Only one instance.sum is supported per origin — origin '" + originId
                                       + "' is already reduced by '" + existing->second + "', found a second: '" + sumId + "'";
                return result;
            }

            sumForOrigin[originId] = sumId;
        }

        // Step 4: backward inclusion. For each origin, walk predecessors
        // (transitively) from every node already resolved to it, pulling in
        // every still-Scalar node reached along the way — an origin's own
        // trigger source and any other mono content it reads directly, both
        // of which need to be compiled REDUNDANTLY inside that origin's own
        // plan (see the header's own comment on why). Never crosses into
        // another origin's already-claimed territory, and never duplicates
        // an instance.sum node (fixed global-domain, by declaration).
        std::unordered_map<juce::String, std::unordered_set<juce::String>> voiceMembers;
        for (const auto& originId : originIds)
        {
            std::unordered_set<juce::String> members;
            std::vector<juce::String> stack;

            for (const auto& node : nodes)
            {
                const auto it = resolvedOrigin.find (node.id);
                if (it != resolvedOrigin.end() && it->second == originId && members.insert (node.id).second)
                    stack.push_back (node.id);
            }

            while (! stack.empty())
            {
                const auto current = stack.back();
                stack.pop_back();

                const auto predIt = predecessorsOf.find (current);
                if (predIt == predecessorsOf.end())
                    continue;

                for (const auto& fromId : predIt->second)
                {
                    if (sumIdSet.count (fromId) > 0)
                        continue;
                    if (resolvedOrigin.find (fromId) != resolvedOrigin.end())
                        continue; // already resolved (to this origin, or a different one — never poached)

                    if (members.insert (fromId).second)
                        stack.push_back (fromId);
                }
            }

            voiceMembers[originId] = std::move (members);
        }

        // Partition: one NodeGraph per origin...
        for (const auto& originId : originIds)
        {
            MultiplicityOrigin origin;
            origin.originId = originId;

            const auto& members = voiceMembers[originId];
            for (const auto& node : nodes)
                if (members.count (node.id) > 0)
                    origin.voiceGraph.addNode (node);

            for (const auto& connection : connections)
                if (members.count (connection.fromNodeId) > 0 && members.count (connection.toNodeId) > 0)
                    origin.voiceGraph.addConnection (connection);

            const auto sumIt = sumForOrigin.find (originId);
            if (sumIt != sumForOrigin.end())
            {
                origin.instanceSumNodeId = sumIt->second;

                // Retarget voiceGraph's own designated output to whatever
                // feeds the instance.sum's "in" — GraphCompiler still needs
                // a real output port to compile against; the actual final
                // value reaches the outside world via setExternalBlock(),
                // never through this designation.
                for (const auto& connection : connections)
                {
                    if (connection.toNodeId == origin.instanceSumNodeId && connection.toPortId == instanceSumInputPortId)
                    {
                        origin.voiceGraph.setOutput (connection.fromNodeId, connection.fromPortId);
                        break;
                    }
                }
            }
            else if (members.count (graph.getOutputNodeId()) > 0)
            {
                // Real, found-live bug fixed here: the graph's OWN
                // designated output is already a member of THIS origin
                // (outputOriginId == originId, the "voice sum IS the final
                // output" case, e.g. buildVoiceProofGraph()) — it must stay
                // the real designation. The "point at the allocator's own
                // gate" fallback below is for when this origin's own
                // signal does NOT reach anywhere meaningful; using it
                // unconditionally here silently swapped the compiled
                // plan's real audible output for the allocator's own raw
                // gate signal instead, on every single-origin,
                // no-instance.sum graph whose output already resolved
                // Poly (i.e. every ordinary voice patch with no instance.sum
                // at all) — caught by real audio-behaviour tests
                // (VoiceRenderTests.cpp, GraphEditControllerTests.cpp), not
                // by any resolver-level structural check.
                origin.voiceGraph.setOutput (graph.getOutputNodeId(), graph.getOutputPortId());
            }
            else
            {
                // No instance.sum reduces this origin (yet), and its own
                // signal doesn't reach the designated output either (the
                // 09-28-InstanceAllocator.1 independent-region case) — same
                // "point at the allocator's own primary output" convention
                // DomainSplitter used, never read by the driver in this
                // case either.
                origin.voiceGraph.setOutput (originId, "gate");
            }

            result.origins.push_back (std::move (origin));
        }

        // ...plus exactly one globalGraph: every Scalar-resolved node that's
        // actually GLOBALLY relevant — NOT simply every Scalar node.
        // "Scalar" alone isn't enough: a node whose every edge exists
        // purely to feed one or more origins directly (an origin's own
        // trigger source is the sharpest example — io.noteIn here has
        // exactly one edge, into the allocator's "spawn", and step 4 above
        // already duplicated it into that origin's own voiceGraph) has no
        // reason to ALSO get a second, pointless copy compiled into
        // globalGraph, where nothing would ever read it. A Scalar node
        // belongs in globalGraph when it's the designated output itself,
        // has no connections at all (the old "orphan folded into global"
        // convention), or has at least one edge to/from ANOTHER Scalar
        // node — the same "mono content wired only to itself" shape that
        // edge implies either way.
        std::unordered_set<juce::String> globalMembers;
        for (const auto& node : nodes)
        {
            if (resolvedOrigin.find (node.id) != resolvedOrigin.end())
                continue; // Poly-resolved — never a globalGraph member on its own account

            if (node.id == graph.getOutputNodeId())
            {
                globalMembers.insert (node.id);
                continue;
            }

            bool hasAnyConnection = false;
            bool hasScalarEdge = false;
            for (const auto& connection : connections)
            {
                if (connection.fromNodeId == node.id)
                {
                    hasAnyConnection = true;
                    if (resolvedOrigin.find (connection.toNodeId) == resolvedOrigin.end())
                        hasScalarEdge = true;
                }
                if (connection.toNodeId == node.id)
                {
                    hasAnyConnection = true;
                    if (resolvedOrigin.find (connection.fromNodeId) == resolvedOrigin.end())
                        hasScalarEdge = true;
                }
            }

            if (hasScalarEdge || ! hasAnyConnection)
                globalMembers.insert (node.id);
        }

        for (const auto& node : nodes)
            if (globalMembers.count (node.id) > 0)
                result.globalGraph.addNode (node);

        // A connection is included here only when BOTH endpoints are actual
        // globalGraph members, so the boundary edge into an instance.sum's
        // "in" is naturally excluded (its source is Poly), exactly as
        // DomainSplitter's own comment on this already explained.
        for (const auto& connection : connections)
            if (globalMembers.count (connection.fromNodeId) > 0 && globalMembers.count (connection.toNodeId) > 0)
                result.globalGraph.addConnection (connection);

        result.globalGraph.setOutput (graph.getOutputNodeId(), graph.getOutputPortId());

        // Step 5: does the designated output itself resolve Poly?
        const auto outputIt = resolvedOrigin.find (graph.getOutputNodeId());
        if (outputIt != resolvedOrigin.end())
        {
            result.hasGlobalDomain = false;
            result.outputOriginId = outputIt->second;
        }
        else
        {
            result.hasGlobalDomain = true;
        }

        result.success = true;
        return result;
    }
}
