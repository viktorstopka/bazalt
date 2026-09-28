#pragma once

#include "bazalt/engine/graph/NodeGraph.h"

namespace bazalt::engine
{
    struct DomainSplitResult
    {
        bool success = false;
        juce::String errorMessage;

        /** True whenever a second, always-running plan (`globalGraph`)
            exists and needs to be compiled/run alongside the voice plans.
            Two real cases, distinguished by `instanceMixNodeId`:
             - Bridged (`instanceMixNodeId` non-empty): a real "instance.mix"
               node sums live voices into this plan (the original, oldest
               meaning of this flag).
             - Independent (`instanceMixNodeId` empty,
               09-28-InstanceAllocator.1): no "instance.mix" exists, but the
               graph's designated output isn't reachable from the allocator
               either — everything NOT reachable from the allocator runs as
               its own always-on plan, completely unbridged, and ITS OWN
               output (not any voice sum) is what's audible
               (`PluginProcessor::finalizeInstanceMixIntoOutput` reads this
               distinction directly from whether `externalInputNodeId` is
               empty).
            False for the simplest case — every M2-M6 graph, and every graph
            with either no boundary-relevant node at all or one whose voice
            domain already reaches the designated output on its own. Then
            `globalGraph` is unused, so nothing downstream needs a special
            case for "no second plan needed at all".
        */
        bool hasGlobalDomain = false;

        /** M21 — DOMAINS.md §7: the compiler marks the allocator's outputs
            poly and propagates forward, so a graph with NO instance.allocate.voice
            has no poly region at all and is entirely mono. True exactly then
            (no instance.allocate.voice, and no instance.mix wired in either). The
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
            hasGlobalDomain is true AND a real "instance.mix" bridges voices
            into `globalGraph`. The compiled global ExecutionPlan's
            getNodeById(instanceMixNodeId) is how the driver (PluginProcessor)
            finds the node to call setExternalBlock() on each block; there
            is no other well-known name to look it up by, since the node's
            id is whatever the user (or a command) gave it. Empty when
            hasGlobalDomain is true for the OTHER reason
            (09-28-InstanceAllocator.1's independent-region case, see
            hasGlobalDomain's own comment) — `globalGraph` still gets
            compiled and run every block, just never handed a voice sum.
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
        each block) and InstanceVoiceNode.h for the (structurally
        inert for this pass — see its own comment) upstream node.

        M17 scope limit: exactly one "instance.mix" node is supported, the
        same ceiling "util.voiceSum" already had — true "placed anywhere,
        multiple allowed" needs ExecutionPlan/GraphCompiler to support more
        than one named output per compiled plan, which doesn't exist yet
        (documented finding, not silently assumed). If one or more
        "instance.allocate.voice" nodes are present, each must land in the
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
