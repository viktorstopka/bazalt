#pragma once

#include "bazalt/engine/graph/EqCurve.h"
#include "bazalt/engine/graph/NodeGraph.h"

namespace bazalt::engine
{
    /** Unwrap (wiki/plans/Factories.md §1 and §3 decision 5): replace a
        factory node with the ordinary nodes it is made of, wired the same,
        sounding the same — one-way, one undoable edit. A factory may never
        be more capable than the primitives it unwraps into; this is where
        that promise is kept.

        Today: `filter.eq` → its bands as `filter.peak` / `filter.shelf` /
        `filter.svf` in series (`EqCurve.h`'s stages, value for value — the
        render is bit-identical, `tests/EqTests.cpp`). An EQ playing a wired
        EQ Curve unwraps that curve's bands; the EQ Curve stays for whatever
        else reads it. */
    struct UnwrapResult
    {
        bool success = false;
        juce::String error;
        std::vector<juce::String> newNodeIds;
    };

    inline bool canUnwrap (const juce::String& typeId) noexcept { return typeId == "filter.eq"; }

    namespace detail
    {
        inline juce::String uniqueNodeId (const NodeGraph& graph, const juce::String& base)
        {
            for (int i = 1;; ++i)
            {
                const auto candidate = base + "_" + juce::String (i);
                if (graph.findNode (candidate) == nullptr)
                    return candidate;
            }
        }

        struct Primitive
        {
            NodeInstance node;
            juce::String outPort;
        };

        inline Primitive primitiveFor (const EqStage& stage)
        {
            Primitive p;
            switch (stage.kind)
            {
                case EqStage::Kind::Peak:
                    p.node.type = "filter.peak";
                    p.node.parameters = { { "filter.peak.frequency", stage.frequency },
                                          { "filter.peak.gain", stage.gain },
                                          { "filter.peak.q", stage.q } };
                    p.outPort = "out";
                    break;
                case EqStage::Kind::LowShelf:
                case EqStage::Kind::HighShelf:
                    p.node.type = "filter.shelf";
                    p.node.parameters = { { "filter.shelf.frequency", stage.frequency },
                                          { "filter.shelf.gain", stage.gain },
                                          { "filter.shelf.type", stage.kind == EqStage::Kind::HighShelf ? 1.0f : 0.0f },
                                          { "filter.shelf.slope", stage.slope } };
                    p.outPort = "out";
                    break;
                case EqStage::Kind::SvfLowpass:
                case EqStage::Kind::SvfHighpass:
                case EqStage::Kind::SvfNotch:
                    p.node.type = "filter.svf";
                    p.node.parameters = { { "filter.svf.cutoff", stage.frequency }, { "filter.svf.resonance", stage.q } };
                    p.outPort = stage.kind == EqStage::Kind::SvfLowpass ? "out" : stage.kind == EqStage::Kind::SvfHighpass ? "highpass" : "notch";
                    break;
            }
            return p;
        }

        inline UnwrapResult unwrapEq (NodeGraph& graph, const NodeInstance eq)
        {
            // The curve it plays: the wired EQ Curve's, else its own.
            auto content = eq.content;
            for (const auto& c : graph.getConnections())
                if (c.toNodeId == eq.id && c.toPortId == "curve")
                    if (const auto* source = graph.findNode (c.fromNodeId))
                        content = source->content;
            const auto doc = EqCurveDocument::fromVar (content);

            juce::String sourceNode, sourcePort;
            std::vector<Connection> outgoing;
            for (const auto& c : graph.getConnections())
            {
                if (c.toNodeId == eq.id && c.toPortId == "in")
                {
                    sourceNode = c.fromNodeId;
                    sourcePort = c.fromPortId;
                }
                if (c.fromNodeId == eq.id && c.fromPortId == "out")
                    outgoing.push_back (c);
            }
            const auto wasOutput = graph.getOutputNodeId() == eq.id;

            std::vector<Primitive> chain;
            for (const auto& band : doc.bands)
            {
                const auto stages = EqCurveDocument::stagesFor (band);
                for (int i = 0; i < stages.count; ++i)
                    chain.push_back (primitiveFor (stages.stages[(size_t) i]));
            }

            graph.removeNode (eq.id);

            UnwrapResult result;
            result.success = true;
            auto previousNode = sourceNode, previousPort = sourcePort;
            for (size_t i = 0; i < chain.size(); ++i)
            {
                auto node = chain[i].node;
                node.id = uniqueNodeId (graph, eq.id);
                node.position = { eq.position.x + (float) i * 170.0f, eq.position.y };
                graph.addNode (node);
                if (previousNode.isNotEmpty())
                    graph.addConnection ({ previousNode, previousPort, node.id, "in" });
                previousNode = node.id;
                previousPort = chain[i].outPort;
                result.newNodeIds.push_back (node.id);
            }

            // A flat EQ is a wire: its source feeds what it fed.
            if (previousNode.isNotEmpty())
            {
                for (const auto& c : outgoing)
                    graph.addConnection ({ previousNode, previousPort, c.toNodeId, c.toPortId });
                if (wasOutput)
                    graph.setOutput (previousNode, previousPort);
            }
            return result;
        }
    }

    /** Replaces `nodeId` in `graph` with what it unwraps into. Leaves the
        graph untouched when the node is missing or has no unwrap. */
    inline UnwrapResult unwrapNode (NodeGraph& graph, const juce::String& nodeId)
    {
        const auto* node = graph.findNode (nodeId);
        if (node == nullptr)
            return { false, "No node " + nodeId, {} };
        if (! canUnwrap (node->type))
            return { false, node->type + " has nothing to unwrap into", {} };
        return detail::unwrapEq (graph, *node);
    }
}
