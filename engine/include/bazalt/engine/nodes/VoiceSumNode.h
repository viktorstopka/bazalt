#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cstring>

namespace bazalt::engine::nodes
{
    /** Stable type id: "util.voiceSum". The domain boundary (NODE_EDITOR.md
        §7, ADR pending): everything upstream of this node's "in" port
        compiles into the per-voice domain; this node and everything
        downstream of its "out" port compile into the one global domain
        (`GraphCompiler`'s domain-partitioning pass, `DomainSplitter.h`).

        In the ORIGINAL (unpartitioned) graph this looks like an ordinary
        one-in-one-out node. After splitting, the edge feeding its "in"
        port isn't part of the global subgraph at all (the source lives in
        the voice domain) — `DomainSplitter` drops that edge when building
        the global subgraph, so this node's "in" port compiles to Silence
        via the ordinary unconnected-input path, no compiler special-casing
        needed. The REAL per-voice sum arrives through `setExternalBlock()`
        instead: whoever drives both the 8 per-voice plans and the one
        global plan (`PluginProcessor`) sums the 8 voice plans' final
        output blocks into its own scratch buffer and calls
        `setExternalBlock()` on this node (via `ExecutionPlan::getNodeById`
        + `dynamic_cast`, the same "poke a concrete node type directly"
        pattern `PluginProcessor::handleMidiEvent` already uses for
        noteOn/setParameter — CLAUDE.md's documented interim
        simplification) immediately before calling the global plan's own
        `process()` — same thread, same call sequence, no synchronization
        needed. `processBlock` copies that externally-supplied block
        straight to its output, ignoring its (Silence) "in" port entirely.

        Declares `supportsPerSample() == false`: this node is a domain
        seam, not a per-sample DSP primitive, and must never legally end up
        inside a feedback cycle — GraphCompiler already rejects any cycle
        containing a node that can't run per-sample (ARCHITECTURE.md §3.4),
        which is exactly the right behaviour here too.
    */
    class VoiceSumNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Sum Voices"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { "in", SignalType::Audio } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        bool supportsPerSample() const noexcept override { return false; }

        /** Audio-thread call, from the plan driver only (PluginProcessor),
            immediately before running the global plan for this block.
            `samples` must remain valid for the duration of that call —
            it's read synchronously inside processBlock() below, never
            stored past it.
        */
        void setExternalBlock (const float* samples, int numSamples) noexcept
        {
            externalSamples = samples;
            externalNumSamples = numSamples;
        }

        void processBlock (const float* const*, float* const* outputs, int numSamples) noexcept override
        {
            if (externalSamples != nullptr && numSamples == externalNumSamples)
                std::memcpy (outputs[0], externalSamples, (size_t) numSamples * sizeof (float));
            else
                std::memset (outputs[0], 0, (size_t) numSamples * sizeof (float));
        }

        void processSample (const float*, float* outputs) noexcept override
        {
            outputs[0] = 0.0f; // never legally reached — supportsPerSample() is false
        }

    private:
        const float* externalSamples = nullptr;
        int externalNumSamples = 0;
    };
}
