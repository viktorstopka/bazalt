#pragma once

#include "bazalt/engine/graph/NodeGraph.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace bazalt::engine
{
    /** One macro's binding to a node parameter (ARCHITECTURE.md §4.3). A
        macro may map to zero or more targets; each target is addressed by
        stable node id + parameter id, never an index. `rangeMin`/`rangeMax`
        remap the macro's 0..1 value onto the target parameter's own range.
    */
    struct MacroMapping
    {
        int macroIndex = 0;
        juce::String targetNodeId;
        juce::String targetParameterId;
        float rangeMin = 0.0f;
        float rangeMax = 1.0f;
    };

    struct PatchMeta
    {
        juce::String name;
        juce::String author;
        juce::int64 createdAtMs = 0;
        juce::int64 modifiedAtMs = 0;
    };

    /** View state (M7, NODE_EDITOR.md §8): pan/zoom, the part of
        ARCHITECTURE.md §4.4's originally-planned `ui` section that isn't
        just a NodeInstance itself. Frames/headers/images are NodeInstances
        with a Decoration layout variant (NODE_EDITOR.md §4) and already
        round-trip through `nodes` — they don't need a field here.
    */
    struct PatchViewState
    {
        float panX = 0.0f;
        float panY = 0.0f;
        float zoom = 1.0f;
    };

    /** Versioned document (ARCHITECTURE.md §4.4): graph (nodes/connections,
        stable ids), macro mapping table + current values, view state, meta.
        Plugin state IS this document — getStateInformation/
        setStateInformation serialize/deserialize it directly, no separate
        format. See PatchSerializer.h for JSON (de)serialization and the
        migration dispatcher.

        Schema v2 (M7): `outputPortIndex` (int) became `outputPortId`
        (string, matching Connection's port-ID addressing, NodeGraph.h) and
        `view` was added. `PatchSerializer`'s v1→v2 migration resolves old
        index-based connections/output ports to IDs using each node type's
        *current* registered port order — the only source of truth
        available for old data; a patch saved before a node's ports were
        reordered is a real (accepted) migration risk, not silently solved.

        Schema v3 (M21): math.add, math.multiply and mix.sum became growable
        port groups (PortGroups.h), so their `a`/`b` inputs are now
        `in.0`/`in.1`. `PatchSerializer`'s v2→v3 migration renames those two
        port IDs on every connection into a node of those types — the
        CLAUDE.md rule-3 way of changing a shipped port ID (never a bare
        rename): an old patch keeps loading and wiring exactly as before.

        Schema v4 (wiki/NODES_Gaps.md's `redundant-composable-param`
        finding): `mix.sum` lost its baked-in `level.N` companion port —
        it duplicated what a `mix.gain` node placed in front of an input
        already does. `PatchSerializer`'s v3→v4 migration doesn't just
        rename a port id this time (there's nothing to rename to — the port
        is gone); a `level.N` that was ever touched (a non-default stored
        value, or a real connection feeding it) becomes a real, visible
        `mix.gain` node spliced between that input's original source and
        `mix.sum` itself, so the old patch keeps sounding the same. A
        `level.N` left at its default (1.0, unconnected) needs nothing — a
        plain `in.N` connection already behaves identically.
    */
    struct PatchDocument
    {
        // Schema v5 (real stereo cable redesign, wiki/NODES.System.md §9):
        // space.pan/space.width/io.output/mix.downmix/stereo.split/
        // stereo.combine collapsed their left/right port pairs into real
        // Channels::Stereo ports. No migration was written for this bump -
        // CLAUDE.md rule 3 ("port ids never renamed once shipped") is
        // suspended for now on the user's own explicit instruction (see
        // that rule's own note): nothing real depends on the pre-v5 shape
        // yet, no distributed patches, nothing saved that anyone relies on.
        // The version number still bumps for hygiene - a marker in case a
        // stray old file ever surfaces - but a v4 (or earlier) patch that
        // actually reaches this version today has no dispatch-table entry
        // and will fail to load rather than silently misinterpreting old
        // port ids as new ones. If a real migration is ever needed later
        // (once rule 3's suspension ends), PatchSerializer.cpp's
        // migrateV3ToV4 is the template: walk nodes/connections as
        // juce::var, insert a bridge node (stereo.combine/stereo.split) for
        // a genuinely asymmetric old pair, retarget a matching pair
        // straight onto the new single port.
        //
        // Schema v6 (wiki/plans/DomainRedesign.md Batch 1b): "instance.mix"
        // renamed to "instance.sum" (its own parameter ids too:
        // "instance.mix.mode" etc. -> "instance.sum.mode"). Same rule-3-
        // suspended, hygiene-only bump as v5's — see migrateV5ToV6 in
        // PatchSerializer.cpp.
        //
        // Schema v7 (wiki/plans/UtilMacro.md, archive_docs/decisions/
        // 0030-util-macro-is-a-real-wireable-node.md): `macroMappings`
        // dropped from this struct entirely. A real `util.macro` node now
        // claims its own host slot via an ordinary structural parameter
        // ("util.macro.slot", on the node itself) and is wired into the
        // graph like any other node — `GraphEditController::
        // recompileAndPublish()` derives the runtime `MacroMapping` table
        // fresh, every compile, from whichever util.macro nodes exist and
        // what slot each claims. There is nothing left for this field to
        // persist: it was never node-derived before this, so a v6 (or
        // earlier) patch's own macroMappings content is silently dropped
        // on migration — see migrateV6ToV7 in PatchSerializer.cpp.
        // `macroValues` stays: each of the 32 AudioParameterFloats' own
        // current automated value is real, independent state, unrelated
        // to which nodes claim which slot.
        static constexpr int currentSchemaVersion = 9;

        int schemaVersion = currentSchemaVersion;
        std::vector<NodeInstance> nodes;
        std::vector<Connection> connections;
        juce::String outputNodeId;
        juce::String outputPortId;
        std::vector<float> macroValues; // index-aligned with the macro pool
        PatchViewState view;
        PatchMeta meta;

        NodeGraph toNodeGraph() const
        {
            NodeGraph graph;
            for (const auto& node : nodes)
                graph.addNode (node);
            for (const auto& connection : connections)
                graph.addConnection (connection);
            graph.setOutput (outputNodeId, outputPortId);
            return graph;
        }

        static PatchDocument fromNodeGraph (const NodeGraph& graph)
        {
            PatchDocument doc;
            doc.nodes = graph.getNodes();
            doc.connections = graph.getConnections();
            doc.outputNodeId = graph.getOutputNodeId();
            doc.outputPortId = graph.getOutputPortId();
            return doc;
        }
    };
}
