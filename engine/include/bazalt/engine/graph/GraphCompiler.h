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
        the compile (leaving the caller's *published* previous plan live —
        the compiler itself has no notion of "what's currently live",
        that's PlanSwapper's job) if a cycle contains a node that can't run
        per-sample.

        `previousPlan` (M17, may be null) is a *different* thing from the
        rejection-rollback plan above — it's ARCHITECTURE.md §3.2's
        per-(instanceIndex, nodeID) state pool: when non-null, any node
        instance whose `(id, type, parameters)` are *all* identical to a
        node already present in `previousPlan` is *reused* (the same
        `Node` object, carrying forward its DSP state — filter memory,
        envelope stage, delay-line contents) instead of freshly
        constructed via `factory.create()`. Both `prepare()` and every
        `setParameter()` call are skipped for a reused node — not just
        `prepare()` — because the node may still be reachable from the
        audio thread through `previousPlan` (it isn't provably dead until
        `PlanSwapper`'s epoch-gated `reclaim()` says so); mutating it from
        the compiler thread would race the audio thread's own
        `processSample()` calls on that same object. This means state
        continuity is real only for a node whose *own* parameters are
        unchanged since the last compile — editing a node's own parameter
        always takes effect immediately (a fresh node is built for it,
        exactly like pre-M17 behaviour), it just doesn't also get the
        state-preservation bonus on that same edit. Pass `nullptr` for a
        first-ever compile or wherever state continuity genuinely doesn't
        apply (the global-domain plan today; a graph being compiled
        somewhere with no prior generation).
    */
    class GraphCompiler
    {
    public:
        static CompileResult compile (const NodeGraph& graph,
                                       const NodeFactory& factory,
                                       const NodePrepareInfo& prepareInfo,
                                       uint64_t generation,
                                       const ExecutionPlan* previousPlan = nullptr);
    };
}
