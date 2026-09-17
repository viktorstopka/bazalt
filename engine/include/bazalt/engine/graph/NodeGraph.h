#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <vector>
#include <unordered_map>

namespace bazalt::engine
{
    /** A plain 2D position — deliberately not `juce::Point` (that lives in
        `juce_graphics`, which `engine/` must never depend on, CLAUDE.md
        rule 4 / ADR-0002). World-space units, meaning defined by the UI
        (NODE_EDITOR.md's canvas), opaque to the engine itself.
    */
    struct NodePosition
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    /** One node instance in the editable graph. `id` is the stable,
        hand-assigned string identity (ARCHITECTURE.md §3.6) — this is what
        a recompile uses to keep a node's per-voice state across an edit
        (§3.2). `type` selects which Node implementation to instantiate,
        via the NodeFactory the graph is compiled with. `parameters` are
        initial values applied via Node::setParameter after construction.

        `position` and `properties` were added in M7 (NODE_EDITOR.md §4/§8):
        `position` matches ARCHITECTURE.md §3.1's original description of
        NodeGraph ("nodes... UI position"), which the code had simply never
        implemented until now. `properties` is a second, `var`-typed bag
        alongside `parameters` for non-float configuration — text (Frame/
        Header labels), enum selections, embedded image data — that a plain
        `float` can't represent. Decorations (Frame/Header/Image, the
        Reroute utility) are NodeInstances with a Decoration-layout node
        type, not a separate list — see NODE_EDITOR.md §4.
    */
    struct NodeInstance
    {
        juce::String id;
        juce::String type;
        NodePosition position;
        std::unordered_map<juce::String, float> parameters;
        std::unordered_map<juce::String, juce::var> properties;
    };

    /** One connection, referencing endpoints by node id + stable port ID
        string (M7 — NODE_EDITOR.md §2/§8; previously port INDEX, which
        broke if a node's declared port order ever changed and couldn't be
        reasoned about by identity the way splice/reconnect operations need
        to). `GraphCompiler` resolves port ids to indices once per compile,
        via each node's `getInputPorts()`/`getOutputPorts()` — the compiled
        `ExecutionPlan` itself stays index-addressed throughout (that's an
        audio-thread-facing artifact, not something the UI ever touches by
        identity).
    */
    struct Connection
    {
        juce::String fromNodeId;
        juce::String fromPortId;
        juce::String toNodeId;
        juce::String toPortId;
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

        /** Removes the node and every connection touching it (M7 command
            API, NODE_EDITOR.md §6's `deleteNodes`). No-op if the id doesn't
            exist — callers that need to distinguish "not found" check
            first via findNode().
        */
        void removeNode (const juce::String& nodeId)
        {
            nodes.erase (std::remove_if (nodes.begin(), nodes.end(),
                                          [&] (const NodeInstance& n) { return n.id == nodeId; }),
                         nodes.end());

            connections.erase (std::remove_if (connections.begin(), connections.end(),
                                                [&] (const Connection& c)
                                                { return c.fromNodeId == nodeId || c.toNodeId == nodeId; }),
                                connections.end());
        }

        /** Removes one connection by full endpoint match (M7 command API's
            `disconnect`). No-op if no matching connection exists.
        */
        void removeConnection (const juce::String& fromNodeId, const juce::String& fromPortId,
                                const juce::String& toNodeId, const juce::String& toPortId)
        {
            connections.erase (std::remove_if (connections.begin(), connections.end(),
                                                [&] (const Connection& c)
                                                {
                                                    return c.fromNodeId == fromNodeId && c.fromPortId == fromPortId
                                                           && c.toNodeId == toNodeId && c.toPortId == toPortId;
                                                }),
                                connections.end());
        }

        const std::vector<NodeInstance>& getNodes() const noexcept { return nodes; }
        const std::vector<Connection>& getConnections() const noexcept { return connections; }

        NodeInstance* findNode (const juce::String& nodeId) noexcept
        {
            for (auto& node : nodes)
                if (node.id == nodeId)
                    return &node;
            return nullptr;
        }

        const NodeInstance* findNode (const juce::String& nodeId) const noexcept
        {
            for (const auto& node : nodes)
                if (node.id == nodeId)
                    return &node;
            return nullptr;
        }

        /** Designates which node's output port is the graph's audible
            output — the compiler needs a root to schedule backwards from.
            Addressed by port ID (M7), same reasoning as Connection above.
        */
        void setOutput (const juce::String& nodeId, const juce::String& portId)
        {
            outputNodeId = nodeId;
            outputPortId = portId;
        }

        const juce::String& getOutputNodeId() const noexcept { return outputNodeId; }
        const juce::String& getOutputPortId() const noexcept { return outputPortId; }

    private:
        std::vector<NodeInstance> nodes;
        std::vector<Connection> connections;
        juce::String outputNodeId;
        juce::String outputPortId;
    };
}
