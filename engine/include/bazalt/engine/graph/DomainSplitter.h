#pragma once

#include "bazalt/engine/graph/NodeGraph.h"

namespace bazalt::engine
{
    struct DomainSplitResult
    {
        bool success = false;
        juce::String errorMessage;

        /** False for any graph with no "instance.mix" node — every M2-M6
            graph, and every graph a future editor builds before the user
            ever adds a voice/global boundary. `voiceGraph` is then just a
            copy of the original graph and `globalGraph` is unused, so
            nothing downstream needs a special case for "no boundary yet".
        */
        bool hasGlobalDomain = false;

        /** M21 — DOMAINS.md §7: the compiler marks the allocator's outputs
            poly and propagates forward, so a graph with NO instance.allocator
            has no poly region at all and is entirely mono. True exactly then
            (no instance.allocator, and no instance.mix wired in either). The
            driver compiles the whole `voiceGraph` once, as the one global
            plan, and runs it every block — with or without a held note —
            instead of once per voice while voices are active. That is what
            an audio effect (io.audioIn -> ... -> out) needs. `voiceGraph` is
            still the unchanged input graph, as for every graph with no
            boundary, so nothing keyed on it changes; only what the driver
            does with it does. Every graph the editor produces before the user
            adds an allocator is one of these, and none of them ever played a
            note: without an allocator no gate or pitch reaches an envelope or
            oscillator.
        */
        bool monoOnly = false;

        /** The "instance.mix" node's own (user-chosen) id — set only when
            hasGlobalDomain is true. The compiled global ExecutionPlan's
            getNodeById(instanceMixNodeId) is how the driver (PluginProcessor)
            finds the node to call setExternalBlock() on each block; there
            is no other well-known name to look it up by, since the node's
            id is whatever the user (or a command) gave it.
        */
        juce::String instanceMixNodeId;

        NodeGraph voiceGraph;
        NodeGraph globalGraph;
    };

    /** Splits one editable NodeGraph into a per-voice subgraph and (if
        exactly one "instance.mix" node is present) a global subgraph, at
        the domain boundary DOMAINS.md §2/§7 describes (M17 — supersedes
        the "util.voiceSum" boundary, RECONCILIATION.md 3.1) — see
        InstanceMixNode.h for the runtime half of this mechanism (how the
        global subgraph's compiled plan actually receives the per-voice sum
        each block) and InstanceAllocatorNode.h for the (structurally
        inert for this pass — see its own comment) upstream node.

        M17 scope limit: exactly one "instance.mix" node is supported, the
        same ceiling "util.voiceSum" already had — true "placed anywhere,
        multiple allowed" needs ExecutionPlan/GraphCompiler to support more
        than one named output per compiled plan, which doesn't exist yet
        (documented finding, not silently assumed). If one or more
        "instance.allocator" nodes are present, each must land in the
        voice domain — a light correctness check, not a functional
        requirement (M17's allocator has no real graph inputs yet, so it
        can't actually influence which domain it lands in via reachability
        alone; the check exists so a future graph that DOES wire an
        allocator downstream of the boundary by mistake gets a clear
        error instead of silent nonsense).

        Each resulting NodeGraph compiles through the existing,
        unmodified GraphCompiler::compile() — this pass only reshapes the
        editable graph, it doesn't change how either half gets scheduled.
    */
    class DomainSplitter
    {
    public:
        static DomainSplitResult split (const NodeGraph& graph);
    };
}
