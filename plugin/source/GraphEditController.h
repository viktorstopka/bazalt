#pragma once

#include "bazalt/engine/graph/NodeGraph.h"
#include <functional>
#include <juce_core/juce_core.h>

namespace bazalt
{
    class BazaltAudioProcessor;

    /** Owns the live, editable NodeGraph and applies commands to it
        (NODE_EDITOR.md §6) — message-thread only. Every graph-shaped
        command recompiles and republishes fresh plans (voice ×numVoices,
        plus one global plan if a util.voiceSum node is present,
        NODE_EDITOR.md §7); a command that would produce a graph that
        doesn't compile is rejected and the live graph/plans are rolled
        back to exactly what they were before the attempt (CLAUDE.md rule
        5 — a bad edit never reaches the audio thread).

        This is the M7 implementation of the command list; the transport
        that calls these methods from the WebView (JUCE's
        withNativeFunction/emitEvent, ADR-0006 once written) is wired in
        PluginEditor. Tests drive this class directly, with no WebView
        involved at all (NODE_EDITOR.md's own M7 scope: "commands are
        driven by a test harness... not real UI").
    */
    class GraphEditController
    {
    public:
        explicit GraphEditController (BazaltAudioProcessor& processorToUse);

        struct CommandResult
        {
            bool success = false;
            juce::String errorMessage;
        };

        /** Called once prepareToPlay knows the sample rate/block size (and
            again on any subsequent prepareToPlay, e.g. a sample-rate
            change) — compiles and publishes the current graph for the
            first time. Idempotent to call before construction completes
            isn't needed: BazaltAudioProcessor's constructor never calls
            this, only prepareToPlay does.
        */
        void prepare (double sampleRateIn, int blockSizeIn);

        CommandResult addNode (const juce::String& typeId, const juce::String& nodeId, float x, float y);
        CommandResult deleteNode (const juce::String& nodeId);
        CommandResult connect (const juce::String& fromNodeId, const juce::String& fromPortId,
                                const juce::String& toNodeId, const juce::String& toPortId);
        CommandResult disconnect (const juce::String& fromNodeId, const juce::String& fromPortId,
                                   const juce::String& toNodeId, const juce::String& toPortId);
        CommandResult setParameterValue (const juce::String& nodeId, const juce::String& parameterId, float value);

        /** Designates which node's output port is the graph's audible
            output (NodeGraph::setOutput) — M8 addition: M7's own command
            list (addNode/deleteNode/connect/disconnect/setParameterValue)
            didn't include this, which is fine for editing *around* the
            existing default graph's output but not for a command sequence
            that needs to establish its own from scratch (M8's stress-test
            generator is exactly that case). Same rollback-on-failure
            contract as every other command.
        */
        CommandResult setOutput (const juce::String& nodeId, const juce::String& portId);

        /** Replaces the whole graph in one step (patch load) — rolled back
            to the previous graph, same as any other command, if the new
            one doesn't compile.
        */
        CommandResult setGraph (bazalt::engine::NodeGraph newGraph);

        /** Applies several raw NodeGraph mutations (addNode/addConnection/
            removeNode/removeConnection/setOutput, called directly on the
            graph reference `mutate` receives) as ONE command: one
            recompile+publish at the end, one rollback if it fails — not
            8*N recompiles for N individual addNode/connect calls.

            This is the "composite operations... are one undo step"
            mechanism NODE_EDITOR.md §6 asks for (splice insert, Unwrap,
            recipe insertion, Alt-drag Mix/Add/Multiply — none built yet,
            but this is what they'll use) — added in M8 because the
            stress-test generator needed it for real: building a 500-node
            graph via 750 individual one-recompile-each commands measured
            in the tens of seconds (8 voice recompiles x a graph that grows
            across ~750 sequential calls informs why — see ADR-0009).
            `mutate` does no validation of its own; a malformed result is
            caught the same way any other command's bad edit is, at the
            recompile, and rolled back in full.
        */
        CommandResult applyBatch (const std::function<void (bazalt::engine::NodeGraph&)>& mutate);

        const bazalt::engine::NodeGraph& getGraph() const noexcept { return graph; }
        bool getHasGlobalDomain() const noexcept { return hasGlobalDomain; }

        /** The util.voiceSum node's id in the current graph — valid only
            when getHasGlobalDomain() is true. This is what the audio
            thread looks up via the global ExecutionPlan's getNodeById() to
            call setExternalBlock() each block (VoiceSumNode.h).
        */
        const juce::String& getVoiceSumNodeId() const noexcept { return voiceSumNodeId; }

    private:
        CommandResult recompileAndPublish();
        uint64_t nextGeneration() noexcept { return generationCounter++; }

        BazaltAudioProcessor& processor;
        bazalt::engine::NodeGraph graph;

        bool isPrepared = false;
        double sampleRate = 44100.0;
        int blockSize = 512;
        bool hasGlobalDomain = false;
        juce::String voiceSumNodeId;
        uint64_t generationCounter = 1;
    };
}
