#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/AlignedBuffer.h"
#include <juce_core/juce_core.h>
#include <algorithm>
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
        uint64_t generation = 0;

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

        std::vector<std::unique_ptr<Node>> nodes;   // owns node instances, indexed by slot
        std::unordered_map<juce::String, int> nodeIdToSlot; // stable NodeGraph id -> slot, for driver lookups (noteOn/setFrequency/etc — never used on the audio thread)

        // Empty for an ordinary plan. Set once, by whoever compiles this
        // plan, before it's ever published (PlanSwapper) — never mutated
        // after, so reading it from the audio thread via a published
        // pointer is exactly as safe as reading anything else on this
        // otherwise-immutable object. Generic on purpose (not
        // "voiceSumNodeId"): any future node type that needs a per-block
        // value supplied from outside its own graph (VoiceSumNode.h is the
        // first; NODE_EDITOR.md doesn't rule out others) can reuse this
        // same field rather than each inventing its own thread-safe
        // driver-to-audio-thread handoff.
        juce::String externalInputNodeId;
        std::vector<AlignedBuffer> blockBuffers;    // one per block-rate node-output port
        std::vector<float> regionScalars;           // one per per-sample-region-internal node-output port
        std::vector<Step> steps;                    // schedule, in execution order
        int finalOutputBufferIndex = -1;             // into blockBuffers; holds the plan's audible output after process()
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

        /** Message-thread/driver convenience — not for the audio thread.
            Returns nullptr if no node with this id was compiled into the plan.
        */
        Node* getNodeById (const juce::String& nodeId) const
        {
            const auto it = nodeIdToSlot.find (nodeId);
            return it == nodeIdToSlot.end() ? nullptr : nodes[(size_t) it->second].get();
        }

    private:
        static constexpr int maxPortsPerNode = 8;

        float readInput (const InputRef& ref, int sampleIndexForBlockBuffer) const noexcept;
    };
}
