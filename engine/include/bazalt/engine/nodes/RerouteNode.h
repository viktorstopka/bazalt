#pragma once

#include "bazalt/engine/graph/Node.h"
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "deco.reroute". The Knob decoration (NODE_EDITOR.md
        §6.6): one input, passes it straight through. Fan-out to multiple
        destinations needs no special support here — any node's output can
        already feed multiple downstream inputs (GraphCompiler resolves
        each consumer's input independently against the same producer
        location), so Reroute's only job is to give that fan-out point a
        place to sit and be moved/renamed on the canvas.

        docs/CLEANUP.md Priority 1 #2: a real type-polymorphic port, not a
        hardcoded Audio one — it starts as an audio Signal (`Quantity::Audio`)
        (preserving today's behaviour for an unconnected Reroute, or one
        fed by an Audio source) and GraphCompiler calls
        `resolveIncomingPort()` once per compile, before validating
        any connection touching this node, to make both ports report
        whatever type actually feeds this node's input (Node.h's own doc
        comment on `hasPolymorphicPorts()`/`resolveIncomingPort()` has
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
        (`resolveIncomingPort` ignores a Data source, leaving
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
        juce::String getCategory() const override { return "Decorations"; } // cable management (wiki/plans/Decorations.md)
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Decoration; }

        bool hasPolymorphicPorts() const noexcept override { return true; }

        // Adopts the source's SignalType AND Quantity: a Frequency cable rerouted
        // stays a Frequency cable, so canConnect still sees the real unit on the
        // far side (a Pitch port beyond it needs its adapter, exactly as if the
        // Reroute weren't there). Always takes the latest offer (the compiler
        // re-offers to a fixed point, so a chain of Reroutes declared sink-first
        // still resolves). A Reroute has one input, and the compiler's
        // one-source-per-input check runs after this pass, so which of two
        // conflicting cables "wins" here never reaches the audio.
        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            juce::ignoreUnused (toPortId);

            if (source.type == SignalType::Data)
                return;

            resolvedType = source.type;
            resolvedQuantity = source.quantity;
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = resolvedType, .quantity = resolvedQuantity, .channels = Channels::Inherited, .polymorphism = PortPolymorphism::SignalAndQuantity } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = resolvedType, .isPrimaryOutput = true, .quantity = resolvedQuantity, .channels = Channels::Inherited, .polymorphism = PortPolymorphism::SignalAndQuantity } };
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
        SignalType resolvedType = SignalType::Signal;
        Quantity resolvedQuantity = Quantity::Audio;
        std::vector<NoteEvent> noteScratch;
    };
}
