#include "bazalt/engine/graph/InstanceExpansion.h"
#include "bazalt/engine/nodes/LifeReduceNode.h"
#include <algorithm>
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

            // The trigger chain: per instance only when MIDI reaches it through
            // an io.noteIn (each instance's note-in is poked directly).
            std::unordered_set<juce::String> chain { origin.originId };
            {
                std::vector<juce::String> stack { origin.originId };
                while (! stack.empty())
                {
                    const auto id = stack.back();
                    stack.pop_back();
                    if (const auto it = predecessors.find (id); it != predecessors.end())
                        for (const auto& next : it->second)
                            if (chain.insert (next).second)
                                stack.push_back (next);
                }
            }
            const auto hearsMidi = std::any_of (chain.begin(), chain.end(), [&origin] (const juce::String& id)
                                                { const auto* n = origin.voiceGraph.findNode (id); return n != nullptr && n->type == "io.noteIn"; });
            if (hearsMidi)
                perInstance.insert (chain.begin(), chain.end());
            else
                layout.groups.back().internallyTriggered = true;

            for (const auto& id : perInstance)
                layout.groupOfNode[id] = group;

            // Split (LifeSplitNode): what only branch k reaches runs only for
            // branch k's members. A node reached from two branches of one Split
            // runs for all of them.
            for (const auto& node : origin.voiceGraph.getNodes())
            {
                if (node.type != "life.split" || perInstance.count (node.id) == 0)
                    continue;
                std::unordered_map<juce::String, int> branchOf; // -1: reached from more than one branch
                for (const auto& c : origin.voiceGraph.getConnections())
                {
                    if (c.fromNodeId != node.id || ! c.fromPortId.startsWith ("branch."))
                        continue;
                    const auto branch = c.fromPortId.fromFirstOccurrenceOf (".", false, false).getIntValue();
                    std::vector<juce::String> stack { c.toNodeId };
                    std::unordered_set<juce::String> seen;
                    while (! stack.empty())
                    {
                        const auto id = stack.back();
                        stack.pop_back();
                        if (perInstance.count (id) == 0 || ! seen.insert (id).second)
                            continue;
                        const auto [it, inserted] = branchOf.try_emplace (id, branch);
                        if (! inserted && it->second != branch)
                            it->second = -1;
                        if (const auto next = successors.find (id); next != successors.end())
                            for (const auto& to : next->second)
                                stack.push_back (to);
                    }
                }
                for (const auto& [id, branch] : branchOf)
                    if (branch >= 0)
                        layout.branchesOfNode[id].push_back ({ node.id, branch });
            }
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
        // A reduce of one group under `id`: one node when the group fits one
        // reduce's inputs, else leaves of LifeReduceNode::maxInputs instances
        // (`id@r0`, `id@r1`, …) summed by a combine node under `id`.
        const auto reduceNode = [generation] (const juce::String& id, int group, NodePosition position)
        {
            NodeInstance reduce { id, "life.reduce", position, {}, {} };
            reduce.parameters = { { "life.reduce.group", (float) group },
                                  { "life.reduce.generation", (float) (generation % 1000000000ull) } };
            return reduce;
        };
        const auto leafId = [] (const juce::String& id, int leaf) { return id + "@r" + juce::String (leaf); };
        constexpr int leafWidth = nodes::LifeReduceNode::maxInputs;
        const auto addReduction = [&] (const juce::String& id, int group, bool average, NodePosition position)
        {
            const auto count = layout.groups[(size_t) group].count;
            if (count <= leafWidth)
            {
                auto reduce = reduceNode (id, group, position);
                reduce.parameters["life.reduce.count"] = (float) count;
                reduce.parameters["life.reduce.average"] = average ? 1.0f : 0.0f;
                out.addNode (reduce);
                return;
            }
            const auto leaves = (count + leafWidth - 1) / leafWidth;
            auto combine = reduceNode (id, group, position);
            combine.parameters["life.reduce.count"] = (float) leaves;
            combine.parameters["life.reduce.combine"] = 1.0f;
            combine.parameters["life.reduce.average"] = average ? 1.0f : 0.0f;
            out.addNode (combine);
            for (int leaf = 0; leaf < leaves; ++leaf)
            {
                auto reduce = reduceNode (leafId (id, leaf), group, position);
                reduce.parameters["life.reduce.count"] = (float) juce::jmin (leafWidth, count - leaf * leafWidth);
                reduce.parameters["life.reduce.first"] = (float) (leaf * leafWidth);
                out.addNode (reduce);
                out.addConnection ({ leafId (id, leaf), "out", id, "in." + juce::String (leaf) });
            }
        };
        // Where instance `i` enters the reduction under `id`.
        const auto reductionInput = [&] (const juce::String& id, int group, int i) -> std::pair<juce::String, juce::String>
        {
            if (layout.groups[(size_t) group].count <= leafWidth)
                return { id, "in." + juce::String (i) };
            return { leafId (id, i / leafWidth), "in." + juce::String (i % leafWidth) };
        };

        // A Population born from inside the patch gets a birth copy: it alone
        // hears the inputs, and starts members at the sample of each birth
        // (LifePopulationNode). A hidden cable (births → afterBirths) runs it before them.
        std::vector<bool> hasBirthCopy (layout.groups.size(), false);
        for (size_t g = 0; g < layout.groups.size(); ++g)
            if (const auto* origin = graph.findNode (layout.groups[g].originId);
                origin != nullptr && origin->type == "life.population" && layout.groups[g].internallyTriggered)
                hasBirthCopy[g] = true;

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
                if (hasBirthCopy[(size_t) group] && node.id == layout.groups[(size_t) group].originId)
                {
                    auto birth = node;
                    birth.id = birthNodeId (node.id);
                    birth.parameters["life.population.birthGroup"] = (float) (group + 1);
                    out.addNode (birth);
                    for (int i = 0; i < layout.groups[(size_t) group].count; ++i)
                        out.addConnection ({ birth.id, "births", instanceNodeId (node.id, i), "afterBirths" });
                }
            }
            else if (const auto it = reduceGroupOf.find (node.id); it != reduceGroupOf.end())
            {
                const auto mode = node.parameters.find ("life.merge.mode");
                addReduction (node.id, it->second, mode != node.parameters.end() && mode->second >= 0.5f, node.position);
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
                // Births from inside the patch reach only the birth copy, which
                // starts members at their sample (LifePopulationNode).
                const auto& group = layout.groups[(size_t) toGroup];
                if (hasBirthCopy[(size_t) toGroup] && c.toNodeId == group.originId)
                {
                    out.addConnection ({ c.fromNodeId, c.fromPortId, birthNodeId (c.toNodeId), c.toPortId });
                    continue;
                }
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
                {
                    const auto [toNode, toPort] = reductionInput (c.toNodeId, fromGroup, i);
                    out.addConnection ({ instanceNodeId (c.fromNodeId, i), c.fromPortId, toNode, toPort });
                }
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
            addReduction (outputReduceNodeId(), outputGroup, false, {});
            for (int i = 0; i < count; ++i)
            {
                const auto [toNode, toPort] = reductionInput (outputReduceNodeId(), outputGroup, i);
                out.addConnection ({ instanceNodeId (graph.getOutputNodeId(), i), graph.getOutputPortId(), toNode, toPort });
            }
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
