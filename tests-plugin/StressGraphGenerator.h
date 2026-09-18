#pragma once

#include "GraphEditController.h"
#include <deque>

namespace bazalt
{
    /** Procedurally builds an N-source binary-reduction graph through the
        real M7 command path (GraphEditController::applyBatch, M8's
        addition - see ADR-0009) - MILESTONES.md M8's "stress-test patch
        generator... via the M7 command path". Not a musically meaningful
        patch: `numSources` util.constant nodes feed a binary tree of
        math.add nodes reducing them to one value, designated directly as
        the graph's output (NodeGraph::setOutput, same as
        buildVoiceProofGraph()'s own mix.gain - no separate io.output node
        needed, setOutput can point at any node's port directly). This
        proves the command path and GraphCompiler/DomainSplitter scale to
        hundreds of nodes - the UI's separate rendering-performance stress
        test (M8's WebGL spike) uses synthetic node/cable positions instead
        of this real compiled graph; the two test different things (engine
        scalability vs. rendering scalability) and are deliberately not the
        same graph.

        M16 note: this used to route the final Control-typed math.add
        result into an io.output node's Audio-only "in" port - a real,
        pre-existing type error `canConnect` (M16) now correctly rejects
        (RECONCILIATION.md 2.3's "anything constructing the graph outside
        the UI can wire nonsense today" gap, closed) - fixed by designating
        the output directly instead of routing through a mismatched node,
        not by weakening the check.

        With `numSources` sources: total nodes = 2*numSources - 1
        (numSources constants + numSources-1 reduction adds), total
        connections = 2*numSources - 2. Built as ONE command (one
        recompile+publish) via applyBatch, not numSources*3-ish individual
        commands - building this same graph one addNode/connect call at a
        time (each its own full 8-voice recompile) measured in the tens of
        seconds for 250 sources; batched, it's a single compile pass.
    */
    struct StressGraphResult
    {
        int numNodes = 0;
        int numConnections = 0;
    };

    inline StressGraphResult buildStressReductionGraph (GraphEditController& controller, int numSources)
    {
        jassert (numSources >= 2);

        const auto result = controller.applyBatch ([numSources] (bazalt::engine::NodeGraph& graph)
        {
            std::deque<juce::String> pending;

            for (int i = 0; i < numSources; ++i)
            {
                const auto id = "src" + juce::String (i);

                bazalt::engine::NodeInstance instance;
                instance.id = id;
                instance.type = "util.constant";
                instance.position = { (float) (i % 32) * 40.0f, (float) (i / 32) * 40.0f };
                instance.parameters["util.constant.value"] = (float) i / (float) numSources;

                graph.addNode (std::move (instance));
                pending.push_back (id);
            }

            int addNodeCount = 0;

            while (pending.size() > 1)
            {
                const auto lhs = pending.front();
                pending.pop_front();
                const auto rhs = pending.front();
                pending.pop_front();

                const auto id = "reduce" + juce::String (addNodeCount++);
                graph.addNode ({ id, "math.add", {}, {}, {} });
                graph.addConnection ({ lhs, "out", id, "a" });
                graph.addConnection ({ rhs, "out", id, "b" });

                pending.push_back (id);
            }

            graph.setOutput (pending.front(), "out");
        });

        jassert (result.success);
        juce::ignoreUnused (result);

        StressGraphResult stats;
        stats.numNodes = numSources * 2 - 1;
        stats.numConnections = numSources * 2 - 2;
        return stats;
    }
}
