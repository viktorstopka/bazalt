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
    */
    struct PatchDocument
    {
        static constexpr int currentSchemaVersion = 3;

        int schemaVersion = currentSchemaVersion;
        std::vector<NodeInstance> nodes;
        std::vector<Connection> connections;
        juce::String outputNodeId;
        juce::String outputPortId;
        std::vector<MacroMapping> macroMappings;
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
