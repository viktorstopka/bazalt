#pragma once

#include "bazalt/engine/graph/NodeGraph.h"

namespace bazalt::engine
{
    struct DomainSplitResult
    {
        bool success = false;
        juce::String errorMessage;

        /** False for any graph with no "util.voiceSum" node — every M2-M6
            graph, and every graph a future editor builds before the user
            ever adds a voice/global boundary. `voiceGraph` is then just a
            copy of the original graph and `globalGraph` is unused, so
            nothing downstream needs a special case for "no boundary yet".
        */
        bool hasGlobalDomain = false;

        /** The util.voiceSum node's own (user-chosen) id — set only when
            hasGlobalDomain is true. The compiled global ExecutionPlan's
            getNodeById(voiceSumNodeId) is how the driver (PluginProcessor)
            finds the node to call setExternalBlock() on each block; there
            is no other well-known name to look it up by, since the node's
            id is whatever the user (or a command) gave it.
        */
        juce::String voiceSumNodeId;

        NodeGraph voiceGraph;
        NodeGraph globalGraph;
    };

    /** Splits one editable NodeGraph into a per-voice subgraph and (if
        exactly one "util.voiceSum" node is present) a global subgraph, at
        the domain boundary NODE_EDITOR.md §7 describes — see
        VoiceSumNode.h for the runtime half of this mechanism (how the
        global subgraph's compiled plan actually receives the per-voice sum
        each block).

        Each resulting NodeGraph compiles through the existing,
        unmodified GraphCompiler::compile() — this pass only reshapes the
        editable graph, it doesn't change how either half gets scheduled.
    */
    class DomainSplitter
    {
    public:
        static DomainSplitResult split (const NodeGraph& graph);
    };
}
