#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <unordered_map>

namespace bazalt::engine
{
    /** One node instance in the editable graph. `id` is the stable,
        hand-assigned string identity (ARCHITECTURE.md §3.6) — this is what
        a recompile uses to keep a node's per-voice state across an edit
        (§3.2). `type` selects which Node implementation to instantiate,
        via the NodeFactory the graph is compiled with. `parameters` are
        initial values applied via Node::setParameter after construction.
    */
    struct NodeInstance
    {
        juce::String id;
        juce::String type;
        std::unordered_map<juce::String, float> parameters;
    };

    /** One connection, referencing endpoints by node id + port INDEX (not
        port id string) — the graph is small and hand-built for M2, and
        resolving string port ids to indices happens once at compile time,
        not stored redundantly here. If/when a real editor needs
        string-addressed connections, that's a NodeGraph-level change, not
        a compiler one.
    */
    struct Connection
    {
        juce::String fromNodeId;
        int fromPortIndex = 0;
        juce::String toNodeId;
        int toPortIndex = 0;
    };

    /** The editable graph representation (ARCHITECTURE.md §3.1). Lives on
        the message thread; mutating it never touches audio. Compiled into
        an ExecutionPlan by GraphCompiler. No node-graph editor UI exists
        yet (M2 builds the two proof graphs directly in C++), but this is
        the same model a future editor would produce.
    */
    class NodeGraph
    {
    public:
        void addNode (NodeInstance node) { nodes.push_back (std::move (node)); }
        void addConnection (Connection connection) { connections.push_back (std::move (connection)); }

        const std::vector<NodeInstance>& getNodes() const noexcept { return nodes; }
        const std::vector<Connection>& getConnections() const noexcept { return connections; }

        /** Designates which node's output port is the graph's audible
            output — the compiler needs a root to schedule backwards from.
        */
        void setOutput (const juce::String& nodeId, int portIndex = 0)
        {
            outputNodeId = nodeId;
            outputPortIndex = portIndex;
        }

        const juce::String& getOutputNodeId() const noexcept { return outputNodeId; }
        int getOutputPortIndex() const noexcept { return outputPortIndex; }

    private:
        std::vector<NodeInstance> nodes;
        std::vector<Connection> connections;
        juce::String outputNodeId;
        int outputPortIndex = 0;
    };
}
