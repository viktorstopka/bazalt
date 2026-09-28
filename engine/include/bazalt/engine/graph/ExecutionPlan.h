#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/NoteEvent.h"
#include "bazalt/engine/AlignedBuffer.h"
#include "bazalt/engine/telemetry/Tap.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

namespace bazalt::engine
{
    /** Immutable, flat, compiled artifact (ARCHITECTURE.md §3.1/§3.2):
        node instances, a linear schedule of block-rate steps and
        per-sample region steps, and the buffers wiring them together.
        Built once by GraphCompiler, then run repeatedly by process().

        Buffer model: every ordinary (block-rate) node output owns one
        `float[maxBlockSize]` slot in `blockBuffers`. Every per-sample
        region-internal node output owns one persistent `float` scalar in
        `regionScalars` instead — deliberately just one value, overwritten
        every sample and never cleared between samples or blocks. That's
        what makes the cycle-breaking edge work with no special-casing: the
        compiler orders each region's nodes so the one edge that closes the
        cycle has its consumer scheduled before its producer, so reading
        "whatever's currently in the scalar" naturally reads last sample's
        value for that one edge and this-sample's value for every other
        (forward) edge — exactly the implicit one-sample delay
        ARCHITECTURE.md §3.4 describes.
    */
    class ExecutionPlan
    {
    public:
        // Most ports any one node may declare (inputs and outputs are each
        // bounded separately). Public because GraphCompiler enforces it as a
        // real compile error — process()'s per-step scratch arrays are this
        // size, so a node past it would be a stack overrun (undefined
        // behaviour in release; only a jassert in debug).
        //
        // M18: bumped from 8 — InstanceAllocatorNode already has 9 output
        // ports (and expects more) — an overrun that was never exercised
        // before M18 first ran instance.allocate.voice in a real graph. M21:
        // bumped from 16 to 32 for mix.sum, whose 16-input growable group
        // carries a level companion each (2 ports per member). Keep this and
        // Node.h's own maxPortsPerNode in sync — they bound the same
        // contract from two sides (the compiler's per-step scratch arrays
        // here, the default processBlock() loop's scratch arrays there).
        static constexpr int maxPortsPerNode = 32;

        uint64_t generation = 0;

        /** M21: the nodes that asked for host data (Node::wantsHostInputs()),
            recorded once at compile time so the audio thread never scans or
            RTTI-casts every node per block. Raw pointers into `nodes`, valid
            for exactly as long as this plan (which owns them) is.
        */
        std::vector<Node*> hostInputNodes;

        /** Hands `inputs` to every host-input node; call immediately before
            process(). Audio-thread safe (no allocation, no locking).
        */
        void applyHostInputs (const HostInputs& inputs) noexcept
        {
            for (auto* node : hostInputNodes)
                node->setHostInputs (inputs);
        }

        /** Where a node input port reads its sample(s) from. */
        struct InputRef
        {
            enum class Kind
            {
                Silence,       // unconnected — read 0
                BlockBuffer,   // index into blockBuffers
                RegionScalar   // index into regionScalars
            };

            Kind kind = Kind::Silence;
            int index = -1;
        };

        struct BlockStep
        {
            int nodeSlot = -1;
            std::vector<InputRef> inputs;             // one per input port
            std::vector<int> outputBufferIndices;     // one per output port, into blockBuffers

            // M18 (ADR-0024): a connected `SignalType::Note` port is routed
            // through `noteBuffers` instead of the ordinary `inputs`/
            // `outputBufferIndices` above (that port's own `inputs` entry,
            // if it has one, stays `Silence` and is unused). Only one Note
            // input and one Note output per node is supported — see
            // ADR-0024's own reasoning; -1 means "this node has none, or
            // its Note port is unconnected."
            int noteInputBufferIndex = -1;
            int noteOutputBufferIndex = -1;

            // docs/CLEANUP.md Priority 1 #1: runtime skip-and-passthrough
            // for a node with `NodeInstance::properties["bypassed"] == true`
            // (GraphCompiler resolves this once per compile, from the
            // node's own descriptor — process() must never call
            // getOutputPorts() itself, that allocates). When true,
            // process() skips node->processBlock() (and any Note routing)
            // entirely and instead copies `inputs[0]` (the node's first
            // declared input — "primary" by the same convention
            // NodeCard.tsx's splitPorts() uses; Silence if the node has no
            // inputs at all) straight into
            // blockBuffers[bypassOutputBufferIndex]. Any OTHER input is
            // silently ignored and any OTHER output is left untouched, not
            // zeroed — nothing in the current node catalog has a
            // bypass-relevant secondary output yet, so this hasn't needed a
            // real answer. -1 means "not bypassed, or bypassed but the node
            // has no output to fill."
            bool bypassed = false;
            int bypassOutputBufferIndex = -1;
        };

        struct PerSampleRegionStep
        {
            std::vector<int> nodeSlotsInOrder;                    // region-internal execution order
            std::vector<std::vector<InputRef>> inputsPerNode;     // [position][portIndex]
            std::vector<std::vector<int>> outputScalarIndices;    // [position][portIndex] -> regionScalars index

            // The region's own designated output, published into a
            // block-length buffer each sample for downstream block-rate
            // consumers (or for the plan's final output).
            int outputRegionPosition = -1;
            int outputPortIndexInNode = 0;
            int externalOutputBufferIndex = -1;
        };

        struct Step
        {
            enum class Kind { Block, PerSampleRegion };
            Kind kind = Kind::Block;
            BlockStep block;
            PerSampleRegionStep region;
        };

        // M17: shared, not unique, ownership — GraphCompiler reuses the
        // same Node object across a recompile when a node's (id, type) is
        // unchanged from the previous plan (the "(instanceIndex, nodeID)
        // state pool" ARCHITECTURE.md §3.2 originally specified — see
        // GraphCompiler::compile()'s `previousPlan` parameter), so DSP
        // state (filter memory, envelope stage, delay-line contents)
        // survives an edit that doesn't touch that specific node. This is
        // safe without any new synchronization: only two things ever touch
        // this vector or its shared_ptrs — the compiler thread (building a
        // new plan, copying shared_ptrs out of the previous one) and
        // PlanSwapper::reclaim() (destroying a superseded plan, dropping
        // its shared_ptrs) — both message-thread-only. The audio thread
        // only ever dereferences via a raw pointer obtained from whichever
        // plan PlanSwapper handed it; it never copies or destroys a
        // shared_ptr itself, so it can never be the thread that drops a
        // node's last reference (the exact hazard a shared_ptr would
        // otherwise risk on the audio thread — see Data.h's own comment on
        // why DataPublisher deliberately does NOT use shared_ptr for a
        // case where the audio thread genuinely could be that thread).
        std::vector<std::shared_ptr<Node>> nodes;   // owns (or shares) node instances, indexed by slot
        std::unordered_map<juce::String, int> nodeIdToSlot; // stable NodeGraph id -> slot, for driver lookups (noteOn/setFrequency/etc — never used on the audio thread)
        // M17: parallel to nodeIdToSlot — lets a future compile ask "does
        // my previous plan's node at this id have the same type as what
        // I'm about to create", without needing the original NodeGraph
        // (GraphCompiler.cpp's state-pool reuse check).
        std::unordered_map<juce::String, juce::String> nodeIdToType;

        // M17: the exact parameters applied to each node at compile time —
        // NOT just for bookkeeping. A reused node (same id, same type as
        // the previous plan) is only actually shared — and left
        // completely untouched — when its parameters are also byte-
        // identical to what's recorded here; otherwise a fresh node is
        // built instead. This is a real safety requirement, not an
        // optimization: the previous plan may still be live for the audio
        // thread to read (only PlanSwapper's epoch-gated reclaim() knows
        // for sure it's safe to free), so a reused node's shared_ptr must
        // never be mutated by the compiler thread after publish — calling
        // setParameter() on it would race the audio thread's own
        // processSample() calls on the SAME object. An earlier version of
        // this mechanism re-applied setParameter() to every reused node
        // unconditionally and was caught immediately by
        // GraphEditControllerTests.cpp's own swap-under-load test turning
        // up a real within-block discontinuity — this field is the fix.
        std::unordered_map<juce::String, std::unordered_map<juce::String, float>> nodeIdToAppliedParameters;

        // Empty for an ordinary plan. Set once, by whoever compiles this
        // plan, before it's ever published (PlanSwapper) — never mutated
        // after, so reading it from the audio thread via a published
        // pointer is exactly as safe as reading anything else on this
        // otherwise-immutable object. Generic on purpose (not
        // "instanceMixNodeId"): any future node type that needs a per-block
        // value supplied from outside its own graph (InstanceMixNode.h is
        // the first, superseding the original VoiceSumNode.h — M17) can reuse this
        // same field rather than each inventing its own thread-safe
        // driver-to-audio-thread handoff.
        juce::String externalInputNodeId;

        // Real bug found live (09-28-InstanceAllocator arc): PluginProcessor
        // used to find the plan's io.noteIn node by a HARDCODED instance id
        // ("noteIn") rather than by type — the UI's own Add-menu auto-
        // generates ordinary ids ("node2", "node3", ...) for a placed node,
        // never that specific magic string, so a patch built entirely
        // through the normal editor workflow silently never received a
        // single MIDI note: findNoteIn() always returned nullptr, gate
        // never moved, and nothing anywhere said why. Resolved here instead
        // — the id of whichever node has type "io.noteIn" in this graph
        // (empty if none), exactly once at compile time, the same pattern
        // externalInputNodeId above already established for "the driver
        // needs to find one well-known node by a property of the graph,
        // not by asking the user to type the right id." Only the first
        // io.noteIn found (graph declaration order) is used — today's real
        // usage never has more than one per voice region; a graph that
        // somehow did would silently pick one, exactly as today's
        // hardcoded-id version could already silently pick the wrong node
        // if a second one happened to be named "noteIn".
        juce::String noteInNodeId;

        std::vector<AlignedBuffer> blockBuffers;    // one per block-rate node-output port
        std::vector<float> regionScalars;           // one per per-sample-region-internal node-output port

        // M20: (nodeId, portId) -> blockBuffers index, for every OUTPUT port
        // GraphCompiler resolved to a real block buffer (a per-sample-
        // region-internal output that never escapes its region as an
        // external copy has no entry here — nothing to tap yet for those,
        // a known MVP limitation, not an oversight). Built once at compile
        // time from the exact same resolution GraphCompiler already does
        // for wiring connections. Message-thread lookup only
        // (GraphEditController, resolving a tap-subscribe request) — never
        // read on the audio thread.
        std::unordered_map<juce::String, std::unordered_map<juce::String, int>> outputBufferIndexByNodeAndPort;

        // ADR-0029: the same, for INPUT ports: (nodeId, inputPortId) -> the
        // blockBuffers index of whatever is wired into that input. A view
        // node has no outputs, so this is how its preview finds the signal it
        // is showing. An unconnected input, or one fed from inside a
        // per-sample region, has no entry.
        std::unordered_map<juce::String, std::unordered_map<juce::String, int>> inputSourceBufferIndexByNodeAndPort;

        // M20/ADR-0029: up to maxTapsPerBuffer taps per blockBuffers entry,
        // stored flat (bufferIndex * maxTapsPerBuffer + n), each holding a Tap*
        // currently subscribed to that buffer - or nullptr. Several are needed
        // because one buffer is often watched twice: a node's own preview on its
        // output, plus a view.scope/spectrum/meter wired to that same cable.
        // A unique_ptr<atomic<Tap*>[]> rather than vector<atomic<Tap*>>
        // specifically so ExecutionPlan stays movable (CompileResult returns a
        // plan by value; std::atomic is neither copyable nor movable, but the
        // pointer to this array is). Allocated once at compile time
        // (GraphCompiler::compile(), message thread), every element
        // value-initialised to nullptr. addTapForBufferIndex()/removeTap() are
        // the only ways to mutate an element afterward: single atomic pointer
        // stores from the ONE message thread, lock-free, safe while the audio
        // thread concurrently calls process() on this same plan - no recompile
        // needed to add or remove a tap.
        static constexpr int maxTapsPerBuffer = 4;
        std::unique_ptr<std::atomic<Tap*>[]> tapForBufferIndex;

        /** Message thread. Adds `tap` to a buffer's set; false if the index
            is out of range or all maxTapsPerBuffer places are taken. Adding a
            tap already present is a successful no-op.
        */
        bool addTapForBufferIndex (int bufferIndex, Tap* tap) noexcept
        {
            if (! tapForBufferIndex || tap == nullptr || bufferIndex < 0 || bufferIndex >= (int) blockBuffers.size())
                return false;

            auto* firstFree = static_cast<std::atomic<Tap*>*> (nullptr);
            for (int n = 0; n < maxTapsPerBuffer; ++n)
            {
                auto& slot = tapForBufferIndex[(size_t) (bufferIndex * maxTapsPerBuffer + n)];
                const auto current = slot.load (std::memory_order_relaxed);

                if (current == tap)
                    return true;
                if (current == nullptr && firstFree == nullptr)
                    firstFree = &slot;
            }

            if (firstFree == nullptr)
                return false;

            firstFree->store (tap, std::memory_order_release);
            return true;
        }

        /** Message thread. Removes `tap` from every buffer of this plan. */
        void removeTap (Tap* tap) noexcept
        {
            if (! tapForBufferIndex || tap == nullptr)
                return;

            const auto numSlots = blockBuffers.size() * (size_t) maxTapsPerBuffer;
            for (size_t i = 0; i < numSlots; ++i)
                if (tapForBufferIndex[i].load (std::memory_order_relaxed) == tap)
                    tapForBufferIndex[i].store (nullptr, std::memory_order_release);
        }

        /** The blockBuffers index of an OUTPUT port, or -1 if this plan has no
            such buffer (no such node/port, or a per-sample-region-internal
            output — see outputBufferIndexByNodeAndPort). Message thread.
        */
        int findOutputBufferIndex (const juce::String& nodeId, const juce::String& portId) const
        {
            const auto nodeIt = outputBufferIndexByNodeAndPort.find (nodeId);
            if (nodeIt == outputBufferIndexByNodeAndPort.end())
                return -1;

            const auto portIt = nodeIt->second.find (portId);
            return portIt == nodeIt->second.end() ? -1 : portIt->second;
        }

        /** The buffer a preview on (nodeId, portId) should tap: the port's own
            output buffer if it is an output, else the buffer feeding it if it
            is an input, else -1. Ids are unique across a node's inputs and
            outputs (asserted by a test over every registered node type), so
            the order only matters for a node that broke that rule.
        */
        int findTappableBufferIndex (const juce::String& nodeId, const juce::String& portId) const
        {
            if (const auto outputIndex = findOutputBufferIndex (nodeId, portId); outputIndex >= 0)
                return outputIndex;

            const auto nodeIt = inputSourceBufferIndexByNodeAndPort.find (nodeId);
            if (nodeIt == inputSourceBufferIndexByNodeAndPort.end())
                return -1;

            const auto portIt = nodeIt->second.find (portId);
            return portIt == nodeIt->second.end() ? -1 : portIt->second;
        }

        /** ADR-0029: whether process() pushes into this plan's taps at all.
            Every voice plan of a poly graph carries the same taps, but only
            the plan of the most recently triggered voice has this on, so a
            preview follows the note you just played (M20's design choice)
            without the audio thread ever needing to know a tap's buffer
            index - which changes on every recompile. On by default, so a
            plan used on its own (the global plan, tests) behaves as before.
            A std::atomic member would make ExecutionPlan immovable, and
            CompileResult returns one by value, hence the tiny movable wrapper.
        */
        struct MovableAtomicBool
        {
            std::atomic<bool> value { true };

            MovableAtomicBool() = default;
            MovableAtomicBool (MovableAtomicBool&& other) noexcept : value (other.value.load (std::memory_order_relaxed)) {}
            MovableAtomicBool& operator= (MovableAtomicBool&& other) noexcept
            {
                value.store (other.value.load (std::memory_order_relaxed), std::memory_order_relaxed);
                return *this;
            }
        };
        MovableAtomicBool previewTapsEnabled;

        void setPreviewTapsEnabled (bool enabled) noexcept { previewTapsEnabled.value.store (enabled, std::memory_order_relaxed); }
        bool arePreviewTapsEnabled() const noexcept { return previewTapsEnabled.value.load (std::memory_order_relaxed); }

        // M18 (ADR-0024): one entry per connected Note-typed output port,
        // each `maxBlockSize` long — BlockStep's noteInputBufferIndex/
        // noteOutputBufferIndex index into this. Per-sample-region (Note
        // ports aren't supported inside a feedback cycle — GraphCompiler
        // rejects that at compile time, see ADR-0024).
        std::vector<std::vector<NoteEvent>> noteBuffers;
        std::vector<Step> steps;                    // schedule, in execution order
        int finalOutputBufferIndex = -1;             // into blockBuffers; holds the plan's audible output after process()
        // Real stereo cable redesign (wiki/NODES.System.md §9): set only when
        // the graph's designated output port is itself a real 2-channel
        // (`Channels::Stereo`) Audio port — GraphCompiler's "Final output"
        // section resolves this the same way it resolves
        // finalOutputBufferIndex, right beside it. -1 for a Mono-only output
        // port (still every graph that predates this redesign, unaffected
        // byte for byte, and any future graph whose designated output stays
        // plain mono).
        int finalOutputBufferIndexRight = -1;
        int maxBlockSize = 0;

        // Read-only after compile time (never resized/written by process())
        // — deliberately NOT shared across plans/threads: a global shared
        // buffer here would be a data race between the compiler thread
        // building a new plan and the audio thread reading an older one.
        std::vector<float> silenceBuffer;

        /** Runs the whole schedule for numSamples (<= maxBlockSize this
            plan was compiled for). Audio-thread-safe: no allocation, no
            locking — every buffer and node was allocated at compile time.
        */
        void process (int numSamples) noexcept;

        void reset() noexcept
        {
            for (auto& node : nodes)
                node->reset();

            std::fill (regionScalars.begin(), regionScalars.end(), 0.0f);
        }

        /** Returns nullptr if no node with this id was compiled into the plan.
            The lookup itself never allocates, but a string LITERAL converts to
            a heap-allocated juce::String, so the audio thread must pass a
            String built beforehand (see BazaltAudioProcessor::findNoteIn) -
            never `getNodeById ("literal")` inside processBlock.
        */
        Node* getNodeById (const juce::String& nodeId) const
        {
            const auto it = nodeIdToSlot.find (nodeId);
            return it == nodeIdToSlot.end() ? nullptr : nodes[(size_t) it->second].get();
        }

    private:
        float readInput (const InputRef& ref, int sampleIndexForBlockBuffer) const noexcept;
    };
}
