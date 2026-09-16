#pragma once

#include "bazalt/engine/graph/NodeGraph.h"
#include "bazalt/engine/graph/NodeFactory.h"
#include "bazalt/engine/graph/ExecutionPlan.h"

namespace bazalt::engine
{
    struct CompileResult
    {
        bool success = false;
        ExecutionPlan plan;
        juce::String errorMessage;
    };

    /** Turns a NodeGraph into an ExecutionPlan (ARCHITECTURE.md §3.1–§3.4):
        resolves connections, topologically schedules acyclic nodes as
        block-rate steps, detects feedback cycles via strongly-connected
        components and schedules each as a per-sample region, and rejects
        the compile (leaving the caller's previous plan live — the compiler
        itself has no notion of "previous", that's PlanSwapper's job) if a
        cycle contains a node that can't run per-sample.
    */
    class GraphCompiler
    {
    public:
        static CompileResult compile (const NodeGraph& graph,
                                       const NodeFactory& factory,
                                       const NodePrepareInfo& prepareInfo,
                                       uint64_t generation);
    };
}
