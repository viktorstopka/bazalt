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
        format. See PatchSerializer.h for JSON (de)serialization.

        **Compatibility (wiki/plans/Release.md):** every patch made before
        2026-10-17 was thrown away and the format restarted at version 1, with
        no migrations. Until the ID freeze (Release.md R0.6) a patch in another
        version simply doesn't load; from the freeze on, each change to this
        shape bumps the version and adds a migration in PatchSerializer.cpp.
        `git log` holds the old v1-v16 history and its migrations.
    */
    struct PatchDocument
    {
        static constexpr int currentSchemaVersion = 1;

        int schemaVersion = currentSchemaVersion;
        std::vector<NodeInstance> nodes;
        std::vector<Connection> connections;
        juce::String outputNodeId;
        juce::String outputPortId;
        std::vector<float> macroValues; // index-aligned with the macro pool
        PatchViewState view;
        PatchMeta meta;
        std::map<juce::String, PatchAsset> assets;

        NodeGraph toNodeGraph() const
        {
            NodeGraph graph;
            for (const auto& node : nodes)
                graph.addNode (node);
            for (const auto& connection : connections)
                graph.addConnection (connection);
            graph.setOutput (outputNodeId, outputPortId);
            for (const auto& [id, asset] : assets)
                graph.setAsset (id, asset);
            return graph;
        }

        static PatchDocument fromNodeGraph (const NodeGraph& graph)
        {
            PatchDocument doc;
            doc.nodes = graph.getNodes();
            doc.connections = graph.getConnections();
            doc.outputNodeId = graph.getOutputNodeId();
            doc.outputPortId = graph.getOutputPortId();
            auto pruned = graph;
            pruned.pruneUnreferencedAssets();
            doc.assets = pruned.getAssets();
            return doc;
        }
    };
}
