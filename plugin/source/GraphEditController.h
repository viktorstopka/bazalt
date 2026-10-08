#pragma once

#include "bazalt/engine/graph/NodeGraph.h"
#include <functional>
#include <unordered_map>
#include <juce_core/juce_core.h>

namespace bazalt
{
    class BazaltAudioProcessor;

    /** Owns the live, editable NodeGraph and applies commands to it
        (NODE_EDITOR.md §6) — message-thread only. Every graph-shaped
        command recompiles and republishes fresh plans: 8 voice plans per
        active "instance.allocate.voice" origin (up to
        MultiplicityResolver::maxOrigins simultaneously,
        wiki/plans/DomainRedesign.md Batch 2), plus one shared global plan
        whenever the graph has real Scalar-resolved content (supersedes the
        original util.voiceSum boundary, RECONCILIATION.md 3.1); a command
        that would produce a graph that doesn't compile is rejected and the
        live graph/plans are rolled back to exactly what they were before
        the attempt (CLAUDE.md rule 5 — a bad edit never reaches the audio
        thread).

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
            // connectWithAutoAdapt only: the connection needs the user to pick
            // how information is reduced (CanConnectResult::choices) — call
            // again with one of these.
            juce::StringArray choices {};
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

        /** M16 — resolves the two ports' real descriptors and calls
            `canConnect` (`CanConnect.h`) itself, rather than relying on
            `connect()`'s own rejection inside `recompileAndPublish()` ->
            `GraphCompiler::compile()`: that path only ever sees a flat
            reject once compilation has already been attempted, with no
            chance to insert an adapter chain first. `connectWithAutoAdapt`
            gives every one of `canConnect`'s three outcomes real behavior:
            `Ok` connects directly (identical to `connect()`); `Reject`
            fails with `canConnect`'s own reason, no mutation attempted at
            all; `NeedsAdapters` builds the chain (1-2 adapter nodes,
            wired source -> adapter(s) -> destination) via `applyBatch` —
            one recompile, one undo step, per SIGNAL_TYPES.md §5. A chain
            this milestone can't actually realize as a single-input splice
            is rejected with a message pointing at manual insertion, not
            silently attempted wrong. A chain that loses information (stereo
            into a mono-only port, CanConnectResult::choices) needs `choice`:
            without one this returns failure carrying the choices, so the UI
            can ask; with one, the adapter's mode is set to it.
        */
        CommandResult connectWithAutoAdapt (const juce::String& fromNodeId, const juce::String& fromPortId,
                                             const juce::String& toNodeId, const juce::String& toPortId,
                                             const juce::String& choice = {});

        CommandResult disconnect (const juce::String& fromNodeId, const juce::String& fromPortId,
                                   const juce::String& toNodeId, const juce::String& toPortId);
        /** Whether a running node can take this value live (a port's
            fallback value or a non-structural parameter) — see
            LiveParameterEdits.h. Message thread. */
        bool isLiveEditable (const juce::String& nodeId, const juce::String& parameterId) const;

        CommandResult setParameterValue (const juce::String& nodeId, const juce::String& parameterId, float value);

        /** Adds one fully-configured `util.macro` node (id/position/every
            structural parameter and its cosmetic `unit` property, all in
            the SAME NodeInstance) in ONE recompile via `applyBatch`,
            replacing what used to be the
            UI's own addNode + 5x setParameterValue + setProperty sequence
            (6 separate recompiles). Does NOT connect it — callers still
            call `connectWithAutoAdapt` separately afterward (reusing its
            existing adapter-chain insertion rather than duplicating it
            here, since a macro dragged out of a real Event-typed input,
            e.g. `random.stepped`'s own `trigger` port, genuinely needs a
            Threshold adapter inserted, not a raw connection). Two
            round trips instead of 7-8, and — since every structural
            parameter is now set atomically with the node's own creation —
            the transient "unclaimed slot" window Batch 2's `-1` sentinel
            was built to paper over doesn't exist for this path any more:
            a slot collision is now rejected as ONE atomic no-op (nothing
            added at all), never a half-configured orphan node.
        */
        CommandResult createMacro (const juce::String& macroNodeId, float x, float y,
                                     int slot, float min, float max, bool isInteger, int quantity,
                                     const juce::String& unit, float value, int type = 0);

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

        /** M19 — a pure position update, no DSP implications at all
            (`GraphCompiler`'s state-pool reuse check doesn't compare
            `position`, so this never disrupts a sounding voice's state).
            Still goes through the ordinary recompile+publish path for
            consistency with every other command, not because a position
            change could plausibly fail to compile.
        */
        CommandResult moveNode (const juce::String& nodeId, float x, float y);

        /** M19 — the generic `setProperty` command `NODE_EDITOR.md` §6
            always planned ("rename, Macro constraints, ..."), writing into
            `NodeInstance::properties` (the `var`-typed bag that's existed
            since M7 with no command ever writing it). Used for a node's
            display-name override (`"title"`) and its bypass flag
            (`"bypassed"`) — both real, persisted, round-tripped metadata;
            neither has any DSP-level effect yet (no bypass audio behaviour
            exists in `GraphCompiler`/`ExecutionPlan`), which is a real,
            documented gap, not silently assumed solved.
        */
        CommandResult setProperty (const juce::String& nodeId, const juce::String& propertyKey, juce::var value);

        /** The most image data one patch may carry (wiki/plans/Decorations.md
            §4): an image that would take it past this is refused. */
        static constexpr size_t maxPatchAssetBytes = 5 * 1024 * 1024;

        /** Adds a deco.image node at (x, y) showing `base64` (already
            compressed by the UI), `width` x `height` world units, in one
            undo step. The image is stored once per patch, by content. */
        CommandResult addImage (const juce::String& nodeId, float x, float y, const juce::String& mimeType,
                                const juce::String& base64, float width, float height);

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

        /** The graph as it is saved — a patch file, the host's plugin state,
            the dev export: `view.listen` nodes left out (wiki/ROADMAP.md
            stage 0 — listening is a working state, never part of a patch).
            Undo snapshots use getGraph() and keep them. */
        bazalt::engine::NodeGraph getGraphForSaving() const;
        bool getHasGlobalDomain() const noexcept { return hasGlobalDomain; }

        /** Dev-convenience export, direct instruction ("build that", after
            being asked whether Claude has any quick way to see a patch as
            it's built): dumps the live graph to `file` as pretty-printed
            PatchDocument JSON — the same graphGetSnapshot()/
            graphRestoreSnapshot() round-trip already proven for undo/redo,
            just written to disk and pretty-printed (readable) instead of
            minified and kept only in a JS-side history stack. NOT a real
            save/load feature: no macro/view/meta content (matching
            graphGetSnapshot's own scope exactly), not undo-tracked, and
            never read back automatically by anything — purely so an
            external tool (or an AI session working alongside this
            project) can read the current patch straight off disk without
            a live IPC channel into this process. Overwrites `file`
            unconditionally on every call; the caller decides the path (the
            native function wiring this up picks a fixed one — see
            PluginEditor.cpp).
        */
        CommandResult exportSnapshotToFile (const juce::File& file) const;

        /** Which region ("voice"/"global"/"mono") each node
            id landed in as of the LAST successful recompile — a debugging
            aid (the UI's DomainDot, 09-28-InstanceAllocator arc), computed
            once right after `DomainSplitter::split()` succeeds and reused
            for whichever of the mono/bridged/unbridged-independent branches
            that result took, rather than duplicating the classification
            logic per branch. Never mutated on a REJECTED command (the
            rollback contract means the live graph — and therefore its real
            domain membership — didn't change either). Empty before the
            first successful compile.
        */
        const std::unordered_map<juce::String, juce::String>& getNodeDomains() const noexcept { return nodeDomains; }

        /** wiki/plans/DomainRedesign.md Batch 4: DomainDot's real
            replacement — per PORT (not per node, so the two boundary node
            types' own mixed per-port shape, §2.4, needs no UI-side special
            casing), computed once right after the last successful
            recompile. `kind` is "scalar" or "poly"; `originId` is set only
            when `kind == "poly"`. A port not present here for a node that
            IS present (a growable-group member beyond the throwaway
            default this was computed from) shares that node's other ports'
            kind — every ordinary node's ports resolve uniformly, so the UI
            falls back to any listed port of the same node id.
        */
        struct PortMultiplicityInfo
        {
            juce::String kind;
            juce::String originId;
        };
        const std::unordered_map<juce::String, std::unordered_map<juce::String, PortMultiplicityInfo>>& getPortMultiplicity() const noexcept
        {
            return portMultiplicity;
        }

        /** Every "instance.allocate.voice" node id -> the origin bundle
            slot it currently occupies on the processor, as of the last
            successful recompile — the instance-count badge's own LIVE
            activeCount/maxCount are deliberately NOT cached here (they
            change on every voice on/off, far more often than a recompile);
            the native function reads them fresh from the processor at this
            index instead.
        */
        const std::unordered_map<juce::String, int>& getOriginBundleIndices() const noexcept { return originBundleIndexByNodeId; }

        /** Node id -> its output ports that carry stereo, as of the last
            successful recompile (wiki/plans/StereoChannels.md) — what the
            editor draws as a stereo cable. */
        const std::unordered_map<juce::String, std::vector<juce::String>>& getStereoOutputs() const noexcept { return stereoOutputs; }

    private:
        CommandResult recompileAndPublish();
        uint64_t nextGeneration() noexcept { return generationCounter++; }

        BazaltAudioProcessor& processor;
        bazalt::engine::NodeGraph graph;

        bool isPrepared = false;
        double sampleRate = 44100.0;
        int blockSize = 512;
        bool hasGlobalDomain = false;
        std::unordered_map<juce::String, juce::String> nodeDomains;
        std::unordered_map<juce::String, std::unordered_map<juce::String, PortMultiplicityInfo>> portMultiplicity;
        std::unordered_map<juce::String, int> originBundleIndexByNodeId;
        std::unordered_map<juce::String, std::vector<juce::String>> stereoOutputs;
        uint64_t generationCounter = 1;
    };
}
