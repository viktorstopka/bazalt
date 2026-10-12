#pragma once

#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/MultiplicityResolver.h"
#include "bazalt/engine/graph/NodeGraph.h"
#include <functional>
#include <unordered_map>
#include <vector>

namespace bazalt::engine
{
    /** The instance axis, built by expansion (wiki/plans/InstanceAxis.md §2):
        one editable graph becomes one graph to compile, in which every node
        that runs per instance appears once per instance (`osc@0` … `osc@7`)
        and everything else appears once.

        - A node **downstream of an allocator** (its poly part) and a node
          **upstream of it** (its trigger chain — io.noteIn, a clock that
          spawns it) run per instance, exactly as each voice had its own copy.
        - **Every other node runs once** and feeds every instance the same
          signal — a shared LFO is one LFO, in phase everywhere (the old
          engine copied it into each voice, where the copies drifted apart).
        - A **Merge** fed per instance becomes `life.reduce` (LifeReduceNode),
          one input per instance; when the graph's output itself runs per
          instance, a reduce is added in front of it.

        The compiler then sees an ordinary graph: lanes, feedback regions,
        polymorphic ports, Data and Note wiring, and state carried across
        recompiles (copies keep stable ids) all work unchanged. The plan learns
        which steps belong to which instance afterwards
        (ExecutionPlan::applyInstanceLayout). */
    struct InstanceLayout
    {
        struct Group
        {
            juce::String originId;
            int count = 8;
        };

        std::vector<Group> groups;
        std::unordered_map<juce::String, int> groupOfNode; // absent: runs once

        int groupOf (const juce::String& nodeId) const noexcept
        {
            const auto it = groupOfNode.find (nodeId);
            return it == groupOfNode.end() ? -1 : it->second;
        }
    };

    /** `osc` → `osc@3`. '@' is reserved in node ids for this. */
    inline juce::String instanceNodeId (const juce::String& baseId, int instance)
    {
        return baseId + "@" + juce::String (instance);
    }

    /** `osc@3` → `osc` (and 3); a plain id comes back as is, instance -1. */
    inline juce::String baseNodeId (const juce::String& id, int* instance = nullptr)
    {
        const auto at = id.lastIndexOfChar ('@');
        if (at > 0 && id.substring (at + 1).containsOnly ("0123456789") && id.length() > at + 1)
        {
            if (instance != nullptr)
                *instance = id.substring (at + 1).getIntValue();
            return id.substring (0, at);
        }
        if (instance != nullptr)
            *instance = -1;
        return id;
    }

    /** The id of the reduce the expansion puts in front of a per-instance output. */
    inline const char* outputReduceNodeId() noexcept { return "__output.reduce"; }

    /** Which nodes run per instance, from the resolver's split. `countFor`
        gives each allocator's instance count. */
    InstanceLayout computeInstanceLayout (const MultiplicityResult& split,
                                          const std::function<int (const juce::String& originId)>& countFor);

    struct ExpansionResult
    {
        bool success = false;
        juce::String errorMessage;
        NodeGraph graph;
    };

    /** Expands `graph` along `layout`. `generation` makes every reduce a
        fresh node in each compile (LifeReduceNode's own comment). */
    ExpansionResult expandInstances (const NodeGraph& graph, const InstanceLayout& layout, uint64_t generation);

    /** Expand, compile, tag: the one call that turns an editable graph into an
        instanced plan. A layout with no groups compiles the graph as is. */
    CompileResult compileWithInstances (const NodeGraph& graph, const InstanceLayout& layout, const NodeFactory& factory,
                                         const NodePrepareInfo& prepareInfo, uint64_t generation, const ExecutionPlan* previousPlan = nullptr);
}
