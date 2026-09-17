#include "bazalt/engine/graph/DomainSplitter.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bazalt::engine
{
    namespace
    {
        constexpr const char* voiceSumTypeId = "util.voiceSum";
        constexpr const char* voiceSumInputPortId = "in";

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

        juce::String voiceSumId;
        int voiceSumCount = 0;
        for (const auto& node : nodes)
        {
            if (node.type == voiceSumTypeId)
            {
                voiceSumId = node.id;
                ++voiceSumCount;
            }
        }

        if (voiceSumCount == 0)
        {
            result.success = true;
            result.hasGlobalDomain = false;
            result.voiceGraph = graph;
            return result;
        }

        if (voiceSumCount > 1)
        {
            result.errorMessage = "Only one util.voiceSum node is supported per graph (found "
                                   + juce::String (voiceSumCount) + ")";
            return result;
        }

        const Connection* incoming = nullptr;
        int incomingCount = 0;
        for (const auto& connection : connections)
        {
            if (connection.toNodeId == voiceSumId && connection.toPortId == voiceSumInputPortId)
            {
                incoming = &connection;
                ++incomingCount;
            }
        }

        if (incomingCount != 1)
        {
            result.errorMessage = "util.voiceSum must have exactly one connection into its 'in' port (found "
                                   + juce::String (incomingCount) + ")";
            return result;
        }

        std::unordered_map<juce::String, std::vector<juce::String>> successorsOf, predecessorsOf;
        for (const auto& connection : connections)
        {
            successorsOf[connection.fromNodeId].push_back (connection.toNodeId);
            predecessorsOf[connection.toNodeId].push_back (connection.fromNodeId);
        }

        // Backward from voiceSum (excluding itself) = voice domain.
        const auto voiceDomain = reachableFollowing (voiceSumId, predecessorsOf, false);
        // Forward from voiceSum (including itself) = global domain.
        const auto globalDomain = reachableFollowing (voiceSumId, successorsOf, true);

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
                                       + "' is reachable from both domains (a cycle through util.voiceSum?)";
                return result;
            }
        }

        if (globalDomain.count (graph.getOutputNodeId()) == 0)
        {
            result.errorMessage = "Graph output node '" + graph.getOutputNodeId()
                                   + "' must be in the global domain when a util.voiceSum node is present";
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
        // The edge into voiceSum's "in" port is naturally excluded above —
        // its source lives in voiceDomain, not globalDomain — no explicit
        // filtering needed; VoiceSumNode reads Silence there and gets its
        // real input via setExternalBlock() instead (VoiceSumNode.h).

        globalGraph.setOutput (graph.getOutputNodeId(), graph.getOutputPortId());

        result.success = true;
        result.hasGlobalDomain = true;
        result.voiceSumNodeId = voiceSumId;
        result.voiceGraph = std::move (voiceGraph);
        result.globalGraph = std::move (globalGraph);
        return result;
    }
}
