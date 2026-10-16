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

        // Groups in the resolver's order, then (2b.5) a nested group's
        // members per parent member times the parent's count; parents are
        // marked before the groups inside them, so an inner group's nodes
        // end up in it.
        for (const auto& origin : split.origins)
        {
            InstanceLayout::Group group;
            group.originId = origin.originId;
            group.membersPerParent = juce::jmax (1, countFor (origin.originId));
            layout.groups.push_back (group);
        }
        for (size_t g = 0; g < split.origins.size(); ++g)
            for (size_t p = 0; p < split.origins.size(); ++p)
                if (split.origins[g].parentOriginId == split.origins[p].originId)
                    layout.groups[g].parent = (int) p;
        std::vector<int> order;
        {
            std::vector<int> depth (layout.groups.size(), 0);
            for (size_t g = 0; g < layout.groups.size(); ++g)
            {
                auto d = 0;
                for (auto p = layout.groups[g].parent; p >= 0 && d <= (int) layout.groups.size(); p = layout.groups[(size_t) p].parent)
                    ++d;
                depth[g] = d;
                order.push_back ((int) g);
            }
            std::stable_sort (order.begin(), order.end(), [&depth] (int a, int b) { return depth[(size_t) a] < depth[(size_t) b]; });
        }
        for (const auto g : order)
        {
            auto& group = layout.groups[(size_t) g];
            group.count = group.membersPerParent * (group.parent >= 0 ? layout.groups[(size_t) group.parent].count : 1);
            if (group.count > maxInstancesPerGroup && layout.errorMessage.isEmpty())
                layout.errorMessage = "Population '" + group.originId + "' would run " + juce::String (group.count)
                                      + " members (its Size times the Populations it lives inside) — at most "
                                      + juce::String (maxInstancesPerGroup);
        }

        for (const auto group : order)
        {
            const auto& origin = split.origins[(size_t) group];

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
            // A Merge of a nested Population runs per member of this one.
            for (const auto& node : origin.voiceGraph.getNodes())
                if (node.type == "life.merge")
                    perInstance.insert (node.id);
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
            for (const auto start : std::vector<juce::String> (perInstance.begin(), perInstance.end()))
                walk (start, successors);

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
            if (hearsMidi && layout.groups[(size_t) group].parent >= 0 && layout.errorMessage.isEmpty())
                layout.errorMessage = "Population '" + origin.originId + "' lives inside another one, so its members are born "
                                      "from inside that one's members — not from a Note In";
            if (hearsMidi)
                perInstance.insert (chain.begin(), chain.end());
            else
                layout.groups[(size_t) group].internallyTriggered = true;
            // A nested Population's birth copies run once per parent member.
            if (! hearsMidi && layout.groups[(size_t) group].parent >= 0)
                layout.groupOfNode[birthNodeId (origin.originId)] = layout.groups[(size_t) group].parent;

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

        if (layout.errorMessage.isNotEmpty())
        {
            result.errorMessage = layout.errorMessage;
            return result;
        }

        const auto& groups = layout.groups;
        const auto parentOf = [&groups] (int g) { return g >= 0 ? groups[(size_t) g].parent : -1; };
        const auto isAncestor = [&] (int ancestor, int g)
        {
            for (auto p = parentOf (g); p >= 0; p = parentOf (p))
                if (p == ancestor)
                    return true;
            return false;
        };
        // Member `i` of group g → the member of its ancestor group it lives in.
        const auto ancestorInstance = [&] (int g, int i, int ancestor)
        {
            for (; g != ancestor && g >= 0; g = parentOf (g))
                i /= groups[(size_t) g].membersPerParent;
            return i;
        };

        // A Merge fed per instance becomes a reduce of that group — once, or
        // (2b.5) once per member of the group it lives in, over that member's
        // own slice of the nested group.
        std::unordered_map<juce::String, int> reduceGroupOf;
        for (const auto& c : graph.getConnections())
            if (c.toPortId == "in")
                if (const auto* to = graph.findNode (c.toNodeId); to != nullptr && to->type == "life.merge")
                    if (const auto group = layout.groupOf (c.fromNodeId); group >= 0 && group != layout.groupOf (c.toNodeId))
                        reduceGroupOf[c.toNodeId] = group;

        auto& out = result.graph;
        // A reduce of members [first, first + count) of one group under `id`:
        // one node when they fit one reduce's inputs, else leaves of
        // LifeReduceNode::maxInputs members summed by a combine node under `id`.
        const auto reduceNode = [generation] (const juce::String& id, int group, NodePosition position)
        {
            NodeInstance reduce { id, "life.reduce", position, {}, {} };
            reduce.parameters = { { "life.reduce.group", (float) group },
                                  { "life.reduce.generation", (float) (generation % 1000000000ull) } };
            return reduce;
        };
        // Leaves of a top-level reduce are `id@r0`, … ; of a per-member one
        // (`merge@3`) `merge#r0@3`, so they run with that member.
        const auto leafId = [] (const juce::String& id, int leaf)
        {
            int instance = -1;
            const auto base = baseNodeId (id, &instance);
            return instance < 0 ? id + "@r" + juce::String (leaf) : instanceNodeId (base + "#r" + juce::String (leaf), instance);
        };
        constexpr int leafWidth = nodes::LifeReduceNode::maxInputs;
        const auto addReduction = [&] (const juce::String& id, int group, bool average, NodePosition position, int first, int count)
        {
            if (count <= leafWidth)
            {
                auto reduce = reduceNode (id, group, position);
                reduce.parameters["life.reduce.count"] = (float) count;
                reduce.parameters["life.reduce.first"] = (float) first;
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
                reduce.parameters["life.reduce.first"] = (float) (first + leaf * leafWidth);
                out.addNode (reduce);
                out.addConnection ({ leafId (id, leaf), "out", id, "in." + juce::String (leaf) });
            }
        };
        // Where member `i` (counted from the reduce's first) enters the reduction under `id`.
        const auto reductionInput = [&] (const juce::String& id, int count, int i) -> std::pair<juce::String, juce::String>
        {
            if (count <= leafWidth)
                return { id, "in." + juce::String (i) };
            return { leafId (id, i / leafWidth), "in." + juce::String (i % leafWidth) };
        };

        // A Population born from inside the patch gets a birth copy: it alone
        // hears the inputs, and starts members at the sample of each birth
        // (LifePopulationNode). A hidden cable (births → afterBirths) runs it
        // before them. A nested one has a birth copy per parent member
        // (`pop@birth@3`), each starting members only in that member's slice.
        std::vector<bool> hasBirthCopy (groups.size(), false);
        for (size_t g = 0; g < groups.size(); ++g)
            if (const auto* origin = graph.findNode (groups[g].originId);
                origin != nullptr && origin->type == "life.population" && groups[g].internallyTriggered)
                hasBirthCopy[g] = true;
        const auto birthCopies = [&] (int g)
        {
            std::vector<juce::String> ids;
            const auto& origin = groups[(size_t) g].originId;
            if (parentOf (g) < 0)
                ids.push_back (birthNodeId (origin));
            else
                for (int o = 0; o < groups[(size_t) parentOf (g)].count; ++o)
                    ids.push_back (instanceNodeId (birthNodeId (origin), o));
            return ids;
        };

        for (const auto& node : graph.getNodes())
        {
            const auto group = layout.groupOf (node.id);
            if (const auto it = reduceGroupOf.find (node.id); it != reduceGroupOf.end())
            {
                const auto mode = node.parameters.find ("life.merge.mode");
                const auto average = mode != node.parameters.end() && mode->second >= 0.5f;
                const auto& fed = groups[(size_t) it->second];
                if (group < 0)
                {
                    addReduction (node.id, it->second, average, node.position, 0, fed.count);
                }
                else
                {
                    if (fed.parent != group)
                    {
                        result.errorMessage = "Merge '" + node.id + "' reduces a Population that doesn't live directly inside its own";
                        return result;
                    }
                    for (int o = 0; o < groups[(size_t) group].count; ++o)
                        addReduction (instanceNodeId (node.id, o), it->second, average, node.position, o * fed.membersPerParent,
                                      fed.membersPerParent);
                }
            }
            else if (group >= 0)
            {
                for (int i = 0; i < groups[(size_t) group].count; ++i)
                {
                    auto copy = node;
                    copy.id = instanceNodeId (node.id, i);
                    out.addNode (copy);
                }
                if (hasBirthCopy[(size_t) group] && node.id == groups[(size_t) group].originId)
                {
                    const auto copies = birthCopies (group);
                    const auto perCopy = groups[(size_t) group].count / (int) copies.size();
                    for (int o = 0; o < (int) copies.size(); ++o)
                    {
                        auto birth = node;
                        birth.id = copies[(size_t) o];
                        birth.parameters["life.population.birthGroup"] = (float) (group + 1);
                        birth.parameters["life.population.parentInstance"] = (float) (parentOf (group) < 0 ? -1 : o);
                        out.addNode (birth);
                        for (int i = o * perCopy; i < (o + 1) * perCopy; ++i)
                            out.addConnection ({ birth.id, "births", instanceNodeId (node.id, i), "afterBirths" });
                    }
                }
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
            const auto fromNode = [&] (int i) { return fromGroup >= 0 ? instanceNodeId (c.fromNodeId, i) : c.fromNodeId; };

            // Into a Population with a birth copy: only the birth copy hears it.
            if (toGroup >= 0 && hasBirthCopy[(size_t) toGroup] && c.toNodeId == groups[(size_t) toGroup].originId)
            {
                const auto copies = birthCopies (toGroup);
                const auto parent = parentOf (toGroup);
                if (fromGroup == toGroup || (fromGroup >= 0 && fromGroup != parent && ! isAncestor (fromGroup, parent)))
                {
                    result.errorMessage = "'" + c.fromNodeId + "' can't give birth to the Population '" + c.toNodeId
                                          + "': it doesn't live outside it";
                    return result;
                }
                for (int o = 0; o < (int) copies.size(); ++o)
                    out.addConnection ({ fromNode (fromGroup >= 0 ? ancestorInstance (parent, o, fromGroup) : 0), c.fromPortId,
                                         copies[(size_t) o], c.toPortId });
                continue;
            }

            // Into a reduce (a Merge).
            if (const auto it = reduceGroupOf.find (c.toNodeId); it != reduceGroupOf.end() && c.toPortId == "in")
            {
                if (fromGroup != it->second)
                {
                    result.errorMessage = "Merge '" + c.toNodeId + "' is fed by two different Populations";
                    return result;
                }
                const auto& fed = groups[(size_t) fromGroup];
                if (toGroup < 0)
                {
                    for (int i = 0; i < fed.count; ++i)
                    {
                        const auto [toNode, toPort] = reductionInput (c.toNodeId, fed.count, i);
                        out.addConnection ({ instanceNodeId (c.fromNodeId, i), c.fromPortId, toNode, toPort });
                    }
                }
                else
                {
                    for (int i = 0; i < fed.count; ++i)
                    {
                        const auto o = i / fed.membersPerParent;
                        const auto [toNode, toPort] = reductionInput (instanceNodeId (c.toNodeId, o), fed.membersPerParent, i % fed.membersPerParent);
                        out.addConnection ({ instanceNodeId (c.fromNodeId, i), c.fromPortId, toNode, toPort });
                    }
                }
                continue;
            }

            if (fromGroup >= 0 && fromGroup == toGroup)
            {
                for (int i = 0; i < groups[(size_t) fromGroup].count; ++i)
                    out.addConnection ({ instanceNodeId (c.fromNodeId, i), c.fromPortId, instanceNodeId (c.toNodeId, i), c.toPortId });
            }
            else if (toGroup >= 0 && (fromGroup < 0 || isAncestor (fromGroup, toGroup)))
            {
                // Once-only, heard by every member; or an outer member, heard by
                // each member that lives inside it.
                for (int i = 0; i < groups[(size_t) toGroup].count; ++i)
                    out.addConnection ({ fromNode (fromGroup >= 0 ? ancestorInstance (toGroup, i, fromGroup) : 0), c.fromPortId,
                                         instanceNodeId (c.toNodeId, i), c.toPortId });
            }
            else if (fromGroup >= 0 && toGroup < 0)
            {
                result.errorMessage = "'" + c.toNodeId + "' runs once but is fed per instance from '" + c.fromNodeId
                                      + "' — put a Merge between them";
                return result;
            }
            else if (fromGroup >= 0)
            {
                result.errorMessage = "'" + c.toNodeId + "' is fed by two different Populations — put a Merge between them";
                return result;
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
            addReduction (outputReduceNodeId(), outputGroup, false, {}, 0, count);
            for (int i = 0; i < count; ++i)
            {
                const auto [toNode, toPort] = reductionInput (outputReduceNodeId(), count, i);
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
