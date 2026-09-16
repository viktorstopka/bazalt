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

    /** Versioned document (ARCHITECTURE.md §4.4): graph (nodes/connections,
        stable ids), macro mapping table + current values, meta. Plugin
        state IS this document — getStateInformation/setStateInformation
        serialize/deserialize it directly, no separate format. See
        PatchSerializer.h for JSON (de)serialization and the migration
        dispatcher.

        No `ui` (view state) section yet — there's no node-graph editor to
        have view state (pan/zoom/layout) for until one exists; adding the
        field now would just be dead schema. Add it in the same commit as
        the editor, not speculatively.
    */
    struct PatchDocument
    {
        static constexpr int currentSchemaVersion = 1;

        int schemaVersion = currentSchemaVersion;
        std::vector<NodeInstance> nodes;
        std::vector<Connection> connections;
        juce::String outputNodeId;
        int outputPortIndex = 0;
        std::vector<MacroMapping> macroMappings;
        std::vector<float> macroValues; // index-aligned with the macro pool
        PatchMeta meta;

        NodeGraph toNodeGraph() const
        {
            NodeGraph graph;
            for (const auto& node : nodes)
                graph.addNode (node);
            for (const auto& connection : connections)
                graph.addConnection (connection);
            graph.setOutput (outputNodeId, outputPortIndex);
            return graph;
        }

        static PatchDocument fromNodeGraph (const NodeGraph& graph)
        {
            PatchDocument doc;
            doc.nodes = graph.getNodes();
            doc.connections = graph.getConnections();
            doc.outputNodeId = graph.getOutputNodeId();
            doc.outputPortIndex = graph.getOutputPortIndex();
            return doc;
        }
    };
}
