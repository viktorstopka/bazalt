#pragma once

#include "bazalt/engine/graph/NodeGraph.h"
#include <vector>

namespace bazalt::engine
{
    /** One instance-allocating node's own Poly-resolved region (any of
        "life.voice"/"swarmPopulation"/"swarmTransient"/
        "trigger" — Domain Extensions batch; see MultiplicityResolver.cpp's
        own `isInstanceOriginType`) — every node MultiplicityResolver::
        split() classified as Poly(originId == this node's own id),
        duplicated in on the origin's own upstream trigger chain (see
        MultiplicityResolver.cpp's own comment on why), plus the allocator
        itself. Compiled numVoices times, exactly like
        DomainSplitResult::voiceGraph was — this struct is engine-level
        (NodeGraph-shaped); the plugin-level per-origin runtime state
        (VoiceManager + 8 PlanSwappers) is a DIFFERENT, deliberately
        similarly-named `OriginBundle` in PluginProcessor.h — don't confuse
        the two.
    */
    struct MultiplicityOrigin
    {
        juce::String originId;
        NodeGraph voiceGraph;

        /** One "life.merge" reducing this origin: the sum node and the
            voice-side port that feeds it. */
        struct Sum
        {
            juce::String sumNodeId;
            juce::String fromNodeId;
            juce::String fromPortId;
        };

        /** Every instance.sum reducing this origin, in graph order — empty
            when none does (yet), the 09-28-InstanceAllocator.1 "independent,
            unbridged region" case. Several are allowed (up to
            MultiplicityResolver::maxSumsPerOrigin): one voice can feed a main
            chain AND a separate layer, each summed on its own. The first
            one's feeder is also voiceGraph's designated output.
        */
        std::vector<Sum> sums;
    };

    /** Replaces DomainSplitResult — same job (split one editable NodeGraph
        into the pieces the driver compiles separately), reshaped around
        wiki/plans/DomainRedesign.md's per-node Multiplicity resolution
        instead of DomainSplitter's whole-graph reachability. See
        MultiplicityResolver::split()'s own doc comment for the algorithm.
    */
    struct MultiplicityResult
    {
        bool success = false;
        juce::String errorMessage;

        /** True iff the graph has no instance-allocating node of any kind
            AND no "life.merge" node at all — wholly Scalar, compiled once, run
            every block whether or not any note is held (DomainSplitResult::
            monoOnly, unchanged meaning). `origins` is empty and
            `globalGraph` is the whole, unfiltered input graph.
        */
        bool monoOnly = false;

        /** True whenever `globalGraph` is real, separately-compiled content
            that needs to run (one or more instance.sum nodes reducing some
            origin into it, or any other Scalar-resolved node reachable from
            the designated output). False exactly when the designated output
            itself resolved Poly — see `outputOriginId` — in which case that
            origin's own per-voice signal IS the final output and
            `globalGraph` (which may still be non-empty: unrelated Scalar
            content elsewhere in the graph compiles into it regardless) is
            simply never run.
        */
        bool hasGlobalDomain = false;

        /** Set only when hasGlobalDomain is false because the designated
            output resolved Poly (see hasGlobalDomain's own comment) — which
            origin's own voice-domain signal is the audible one. This is the
            oldest, pre-M17 "voice sum IS the final output" case, generalized
            to "whichever origin the output actually traces back to." Empty
            otherwise (including monoOnly).
        */
        juce::String outputOriginId;

        std::vector<MultiplicityOrigin> origins; // one per instance-allocating node, declaration order
        NodeGraph globalGraph;                    // every Scalar-resolved node, incl. every instance.sum and the designated output
    };

    /** Replaces DomainSplitter outright (wiki/plans/DomainRedesign.md §10.1:
        "a compile-time reclassification rewrite, not a runtime ExecutionPlan-
        internals rewrite" — the runtime shape stays N independent physical
        ExecutionPlans per origin, driven by PlanSwapper+VoiceManager exactly
        as before; only how nodes get sorted into buckets before compile
        changes). Multiplicity is a per-compile *resolved fact* about a node
        instance, not a static PortDescriptor field — no engine type changed
        to build this.

        Algorithm (`split`):
        1. Every instance-allocating node (any of the four Domain Extensions
           types — see MultiplicityResolver.cpp's own `isInstanceOriginType`)
           is an ORIGIN, keyed by its own id (a fixed producer, never
           resolved dynamically — every one of its output ports is
           Poly(originId = its own id) by construction). More than
           `maxOrigins` is a compile error, same tone as the old "only
           one... found N" message.
        2. Fixed-point pass over every other (non-origin, non-"life.merge")
           node: resolves Scalar if every wired input is Scalar (or
           unconnected); resolves Poly(X) if any wired input is Poly(X) and
           no OTHER wired input is Poly(Y != X); two inputs resolving to
           different origins is a compile error (the exact rule
           DomainRedesign.md §2.1 describes, "these two poly signals come
           from different voice allocators and can't be combined directly").
           Multiplicity is resolved once per NODE, applied uniformly across
           all its ports — ordinary nodes never have mixed per-port
           multiplicity; only the two boundary types do, by fixed
           declaration.
        3. Every "life.merge" node's "in" port is REQUIRED to resolve
           Poly — resolving Scalar (including "nothing wired yet") past the
           freshly-placed carve-out is a compile error ("nothing to
           reduce"). Up to `maxSumsPerOrigin` instance.sum nodes per origin
           — each reduces its own voice-side signal, so one voice can feed a
           main chain and a separate layer that are summed independently —
           and instance.sum nodes reducing DIFFERENT origins are legitimate
           too (DomainRedesign.md §4's multiple independent voice regions).
        4. Backward inclusion: for each origin, every Scalar node
           transitively upstream of anything already resolved to that origin
           gets DUPLICATED into that origin's own voiceGraph too (an origin's
           trigger source — io.noteIn, or an internally-sequenced
           clock/seq/note.assemble chain — and any other mono content a Poly
           region reads directly, DOMAINS.md §2's "mono is free everywhere").
           This is necessary, not optional: two independently-scheduled
           ExecutionPlans share no buffers, so the only way a Scalar node's
           value can reach a Poly-resolved consumer at all is for that
           Scalar node to be compiled redundantly INSIDE the consuming
           origin's own plan. Mirrors DomainSplitter's own
           09-28-InstanceAllocator.1 fix (which unioned backward
           reachability from the allocator into "voice domain"),
           generalized from "the allocator's own predecessors" to "any
           Poly(X)-resolved node's predecessors" and from one origin to N.
           A node already resolved to SOME origin (this one or another) is
           never re-classified by this pass, and "life.merge" is never
           duplicated (it's a fixed global-domain node by declaration).
        5. The graph's designated output (NodeGraph::getOutputNodeId())
           either resolves Poly(X) — see `outputOriginId` — or Scalar, in
           which case `hasGlobalDomain` is true and globalGraph carries it.
        6. Partition: one NodeGraph per origin (that origin's voiceMembers,
           step 4) plus exactly one `globalGraph` (every Scalar-resolved
           node, including every "life.merge" node and the designated
           output) — note this is NOT mutually exclusive with an origin's
           voiceGraph the way DomainSplitter's old voice/global split was: a
           Scalar node duplicated into an origin's voiceGraph (step 4) still
           also appears in globalGraph if it's genuinely used there too.
    */
    class MultiplicityResolver
    {
    public:
        /** More than this many simultaneous instance-allocating nodes (of
            any of the four Domain Extensions types, combined)
            is a compile error — a small, fixed ceiling, the same precedent
            BazaltAudioProcessor::numAuxBuses already sets for "more than a
            handful of these would need real UI/perf design first."
        */
        static constexpr int maxOrigins = 4;

        /** How many instance.sum nodes may reduce one origin. Each is one
            more summed output per voice, with its own preallocated buffer. */
        static constexpr int maxSumsPerOrigin = 4;

        static MultiplicityResult split (const NodeGraph& graph);
    };
}
