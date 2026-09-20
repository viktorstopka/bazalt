#pragma once

#include "bazalt/engine/graph/Node.h"
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "util.reroute". The Knob decoration (NODE_EDITOR.md
        §6.6): one input, passes it straight through. Fan-out to multiple
        destinations needs no special support here — any node's output can
        already feed multiple downstream inputs (GraphCompiler resolves
        each consumer's input independently against the same producer
        location), so Reroute's only job is to give that fan-out point a
        place to sit and be moved/renamed on the canvas.

        docs/CLEANUP.md Priority 1 #2: a real type-polymorphic port, not a
        hardcoded Audio one — `resolvedType` starts at `SignalType::Audio`
        (preserving today's behaviour for an unconnected Reroute, or one
        fed by an Audio source) and GraphCompiler calls
        `resolveIncomingSignalType()` once per compile, before validating
        any connection touching this node, to make both ports report
        whatever type actually feeds this node's input (Node.h's own doc
        comment on `hasPolymorphicPorts()`/`resolveIncomingSignalType()` has
        the full compiler-side mechanism, including how a chain of several
        Reroutes resolves to a fixed point).

        Covers Audio/Control/Boolean/Event/Spectral (the plain block-buffer
        family — `processSample()` already handles all of these, being
        just a float copy) and Note (via `consumeNoteBlock()`/
        `produceNoteBlock()`, store-and-forward through `noteScratch`,
        exactly like any other Note-capable node — GraphCompiler's Note
        routing is keyed by (nodeSlot, port), never node type, so this
        needed no compiler changes beyond the polymorphic-resolution pass
        itself). Deliberately does NOT support `SignalType::Data`
        (`resolveIncomingSignalType` ignores a Data request, leaving
        `resolvedType` at whatever it already was) — Data is a
        `DataPublisher`-swapped pointer, not a per-sample/per-block value,
        a fundamentally different mechanism this node doesn't implement;
        refusing to adopt it means `canConnect()` naturally rejects a Data
        cable into Reroute with its own clear type-mismatch error instead
        of silently accepting a connection nothing actually forwards.
    */
    class RerouteNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override
        {
            noteScratch.resize ((size_t) info.maxBlockSize);
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Reroute"; }
        juce::String getCategory() const override { return "Utility"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Decoration; }

        bool hasPolymorphicPorts() const noexcept override { return true; }

        void resolveIncomingSignalType (SignalType incomingType) noexcept override
        {
            if (incomingType != SignalType::Data)
                resolvedType = incomingType;
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { "in", resolvedType } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = resolvedType, .isPrimaryOutput = true } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override { outputs[0] = inputs[0]; }

        void consumeNoteBlock (const NoteEvent* input, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
                noteScratch[(size_t) i] = input[i];
        }

        void produceNoteBlock (NoteEvent* output, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
                output[i] = noteScratch[(size_t) i];
        }

    private:
        SignalType resolvedType = SignalType::Audio;
        std::vector<NoteEvent> noteScratch;
    };
}
