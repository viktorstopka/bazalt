#include "bazalt/engine/graph/InstanceExpansion.h"
#include <unordered_set>

namespace bazalt::engine
{
    InstanceLayout computeInstanceLayout (const MultiplicityResult& split,
                                          const std::function<int (const juce::String& originId)>& countFor)
    {
        InstanceLayout layout;
        if (split.monoOnly)
            return layout;

        for (const auto& origin : split.origins)
        {
            const auto group = (int) layout.groups.size();
            layout.groups.push_back ({ origin.originId, juce::jmax (1, countFor (origin.originId)) });

            // Within the origin's region: what the allocator feeds (its poly
            // part) and what feeds the allocator (its trigger chain) run per
            // instance. What the resolver only pulled in to feed the poly part
            // (an LFO, a constant) runs once.
            std::unordered_map<juce::String, std::vector<juce::String>> successors, predecessors;
            for (const auto& c : origin.voiceGraph.getConnections())
            {
                successors[c.fromNodeId].push_back (c.toNodeId);
                predecessors[c.toNodeId].push_back (c.fromNodeId);
            }

            std::unordered_set<juce::String> perInstance { origin.originId };
            const auto walk = [&perInstance] (const juce::String& start, auto& edges)
            {
                std::vector<juce::String> stack { start };
                while (! stack.empty())
                {
                    const auto id = stack.back();
                    stack.pop_back();
                    if (const auto it = edges.find (id); it != edges.end())
                        for (const auto& next : it->second)
                            if (perInstance.insert (next).second)
                                stack.push_back (next);
                }
            };
            walk (origin.originId, successors);
            walk (origin.originId, predecessors);

            for (const auto& id : perInstance)
                layout.groupOfNode[id] = group;
        }
        return layout;
    }

    ExpansionResult expandInstances (const NodeGraph& graph, const InstanceLayout& layout, uint64_t generation)
    {
        ExpansionResult result;
        if (layout.groups.empty())
        {
            result.success = true;
            result.graph = graph;
            return result;
        }

        for (const auto& node : graph.getNodes())
            if (node.id.containsChar ('@'))
            {
                result.errorMessage = "Node id '" + node.id + "' contains '@', which is reserved for instances";
                return result;
            }

        // A Merge fed per instance becomes a reduce of that group.
        std::unordered_map<juce::String, int> reduceGroupOf;
        for (const auto& c : graph.getConnections())
            if (c.toPortId == "in")
                if (const auto* to = graph.findNode (c.toNodeId); to != nullptr && to->type == "life.merge")
                    if (const auto group = layout.groupOf (c.fromNodeId); group >= 0)
                        reduceGroupOf[c.toNodeId] = group;

        auto& out = result.graph;
        const auto reduceNode = [generation, &layout] (const juce::String& id, int group, bool average, NodePosition position)
        {
            NodeInstance reduce { id, "life.reduce", position, {}, {} };
            reduce.parameters = { { "life.reduce.count", (float) layout.groups[(size_t) group].count },
                                  { "life.reduce.group", (float) group },
                                  { "life.reduce.average", average ? 1.0f : 0.0f },
                                  { "life.reduce.generation", (float) (generation % 1000000000ull) } };
            return reduce;
        };

        for (const auto& node : graph.getNodes())
        {
            if (const auto group = layout.groupOf (node.id); group >= 0)
            {
                for (int i = 0; i < layout.groups[(size_t) group].count; ++i)
                {
                    auto copy = node;
                    copy.id = instanceNodeId (node.id, i);
                    out.addNode (copy);
                }
            }
            else if (const auto it = reduceGroupOf.find (node.id); it != reduceGroupOf.end())
            {
                const auto mode = node.parameters.find ("life.merge.mode");
                out.addNode (reduceNode (node.id, it->second,
                                         mode != node.parameters.end() && mode->second >= 0.5f, node.position));
            }
            else
            {
                out.addNode (node);
            }
        }

        for (const auto& c : graph.getConnections())
        {
            const auto fromGroup = layout.groupOf (c.fromNodeId);
            const auto toGroup = layout.groupOf (c.toNodeId);

            if (fromGroup >= 0 && toGroup >= 0)
            {
                if (fromGroup != toGroup)
                {
                    result.errorMessage = "'" + c.toNodeId + "' is fed by two different allocators";
                    return result;
                }
                for (int i = 0; i < layout.groups[(size_t) fromGroup].count; ++i)
                    out.addConnection ({ instanceNodeId (c.fromNodeId, i), c.fromPortId, instanceNodeId (c.toNodeId, i), c.toPortId });
            }
            else if (fromGroup < 0 && toGroup >= 0)
            {
                // Runs once, heard by every instance.
                for (int i = 0; i < layout.groups[(size_t) toGroup].count; ++i)
                    out.addConnection ({ c.fromNodeId, c.fromPortId, instanceNodeId (c.toNodeId, i), c.toPortId });
            }
            else if (fromGroup >= 0)
            {
                if (reduceGroupOf.count (c.toNodeId) == 0 || c.toPortId != "in")
                {
                    result.errorMessage = "'" + c.toNodeId + "' runs once but is fed per instance from '" + c.fromNodeId
                                          + "' — put a Merge between them";
                    return result;
                }
                for (int i = 0; i < layout.groups[(size_t) fromGroup].count; ++i)
                    out.addConnection ({ instanceNodeId (c.fromNodeId, i), c.fromPortId, c.toNodeId, "in." + juce::String (i) });
            }
            else
            {
                out.addConnection (c);
            }
        }

        // The output itself runs per instance: sum the instances in front of it.
        const auto outputGroup = layout.groupOf (graph.getOutputNodeId());
        if (outputGroup >= 0)
        {
            const auto count = layout.groups[(size_t) outputGroup].count;
            out.addNode (reduceNode (outputReduceNodeId(), outputGroup, false, {}));
            for (int i = 0; i < count; ++i)
                out.addConnection ({ instanceNodeId (graph.getOutputNodeId(), i), graph.getOutputPortId(), outputReduceNodeId(),
                                     "in." + juce::String (i) });
            out.setOutput (outputReduceNodeId(), "out");
        }
        else
        {
            out.setOutput (graph.getOutputNodeId(), graph.getOutputPortId());
        }

        for (const auto& [id, asset] : graph.getAssets())
            out.setAsset (id, asset);

        result.success = true;
        return result;
    }

    CompileResult compileWithInstances (const NodeGraph& graph, const InstanceLayout& layout, const NodeFactory& factory,
                                         const NodePrepareInfo& prepareInfo, uint64_t generation, const ExecutionPlan* previousPlan)
    {
        auto expanded = expandInstances (graph, layout, generation);
        if (! expanded.success)
        {
            CompileResult failed;
            failed.errorMessage = expanded.errorMessage;
            return failed;
        }
        auto result = GraphCompiler::compile (expanded.graph, factory, prepareInfo, generation, previousPlan);
        if (result.success)
            result.plan.applyInstanceLayout (layout);
        return result;
    }
}
