#include "bazalt/engine/graph/DomainSplitter.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bazalt::engine
{
    namespace
    {
        constexpr const char* instanceMixTypeId = "instance.mix";
        constexpr const char* instanceMixInputPortId = "in";
        constexpr const char* instanceAllocatorTypeId = "instance.allocator";

        std::unordered_set<juce::String> reachableFollowing (const juce::String& start,
                                                               const std::unordered_map<juce::String, std::vector<juce::String>>& edges,
                                                               bool includeStart)
        {
            std::unordered_set<juce::String> visited;
            std::vector<juce::String> stack { start };

            if (includeStart)
                visited.insert (start);

            while (! stack.empty())
            {
                const auto current = stack.back();
                stack.pop_back();

                const auto it = edges.find (current);
                if (it == edges.end())
                    continue;

                for (const auto& next : it->second)
                    if (visited.insert (next).second)
                        stack.push_back (next);
            }

            return visited;
        }
    }

    DomainSplitResult DomainSplitter::split (const NodeGraph& graph)
    {
        DomainSplitResult result;
        const auto& nodes = graph.getNodes();
        const auto& connections = graph.getConnections();

        juce::String instanceMixId;
        int instanceMixCount = 0;
        int instanceAllocatorCount = 0;
        for (const auto& node : nodes)
        {
            if (node.type == instanceMixTypeId)
            {
                instanceMixId = node.id;
                ++instanceMixCount;
            }
            else if (node.type == instanceAllocatorTypeId)
            {
                ++instanceAllocatorCount;
            }
        }

        if (instanceMixCount == 0)
        {
            result.success = true;
            result.hasGlobalDomain = false;
            result.voiceGraph = graph;
            return result;
        }

        if (instanceMixCount > 1)
        {
            result.errorMessage = "Only one instance.mix node is supported per graph (found "
                                   + juce::String (instanceMixCount)
                                   + ") — placing several is a future capability, "
                                   + "not yet built (ExecutionPlan needs multi-output support first)";
            return result;
        }

        if (instanceAllocatorCount > 1)
        {
            result.errorMessage = "Only one instance.allocator node is supported per graph (found "
                                   + juce::String (instanceAllocatorCount)
                                   + ") — multiple simultaneous instanced regions are M28 (Swarm) territory, not built yet";
            return result;
        }

        const Connection* incoming = nullptr;
        int incomingCount = 0;
        for (const auto& connection : connections)
        {
            if (connection.toNodeId == instanceMixId && connection.toPortId == instanceMixInputPortId)
            {
                incoming = &connection;
                ++incomingCount;
            }
        }

        // A freshly-placed instance.mix has no connection yet — that must
        // be placeable on its own (the UI places a node, then wires it, as
        // two separate commands; requiring the wire to already exist would
        // make it impossible to ever place one at all). Treat this exactly
        // like "no instance.mix node present" (the domain boundary simply
        // isn't active yet) rather than rejecting the whole graph — a real
        // gap found via actual hands-on testing (M19), not caught by any
        // test, since every existing test builds its graph in one shot via
        // setGraph() rather than incrementally via addNode()+connect().
        // Only a genuine double-connection (which the UI itself never
        // produces, but a hand-edited or scripted patch could) stays an
        // error.
        if (incomingCount == 0)
        {
            result.success = true;
            result.hasGlobalDomain = false;
            result.voiceGraph = graph;
            return result;
        }

        if (incomingCount > 1)
        {
            result.errorMessage = "instance.mix must have at most one connection into its 'in' port (found "
                                   + juce::String (incomingCount) + ")";
            return result;
        }

        std::unordered_map<juce::String, std::vector<juce::String>> successorsOf, predecessorsOf;
        for (const auto& connection : connections)
        {
            successorsOf[connection.fromNodeId].push_back (connection.toNodeId);
            predecessorsOf[connection.toNodeId].push_back (connection.fromNodeId);
        }

        // Backward from instance.mix (excluding itself) = voice domain.
        const auto voiceDomain = reachableFollowing (instanceMixId, predecessorsOf, false);
        // Forward from instance.mix (including itself) = global domain.
        const auto globalDomain = reachableFollowing (instanceMixId, successorsOf, true);

        for (const auto& node : nodes)
        {
            const auto inVoice = voiceDomain.count (node.id) > 0;
            const auto inGlobal = globalDomain.count (node.id) > 0;

            if (! inVoice && ! inGlobal)
            {
                result.errorMessage = "Node '" + node.id + "' is not connected to either the voice or global domain";
                return result;
            }

            if (inVoice && inGlobal)
            {
                result.errorMessage = "Node '" + node.id
                                       + "' is reachable from both domains (a cycle through instance.mix?)";
                return result;
            }

            if (node.type == instanceAllocatorTypeId && ! inVoice)
            {
                result.errorMessage = "instance.allocator node '" + node.id
                                       + "' must be in the voice domain (upstream of instance.mix)";
                return result;
            }
        }

        if (globalDomain.count (graph.getOutputNodeId()) == 0)
        {
            result.errorMessage = "Graph output node '" + graph.getOutputNodeId()
                                   + "' must be in the global domain when an instance.mix node is present";
            return result;
        }

        NodeGraph voiceGraph;
        for (const auto& node : nodes)
            if (voiceDomain.count (node.id) > 0)
                voiceGraph.addNode (node);

        for (const auto& connection : connections)
            if (voiceDomain.count (connection.fromNodeId) > 0 && voiceDomain.count (connection.toNodeId) > 0)
                voiceGraph.addConnection (connection);

        voiceGraph.setOutput (incoming->fromNodeId, incoming->fromPortId);

        NodeGraph globalGraph;
        for (const auto& node : nodes)
            if (globalDomain.count (node.id) > 0)
                globalGraph.addNode (node);

        for (const auto& connection : connections)
            if (globalDomain.count (connection.fromNodeId) > 0 && globalDomain.count (connection.toNodeId) > 0)
                globalGraph.addConnection (connection);
        // The edge into instance.mix's "in" port is naturally excluded
        // above — its source lives in voiceDomain, not globalDomain — no
        // explicit filtering needed; InstanceMixNode reads Silence there
        // and gets its real input via setExternalBlock() instead.

        globalGraph.setOutput (graph.getOutputNodeId(), graph.getOutputPortId());

        result.success = true;
        result.hasGlobalDomain = true;
        result.instanceMixNodeId = instanceMixId;
        result.voiceGraph = std::move (voiceGraph);
        result.globalGraph = std::move (globalGraph);
        return result;
    }
}
