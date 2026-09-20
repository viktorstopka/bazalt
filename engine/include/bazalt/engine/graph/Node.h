#pragma once

#include "bazalt/engine/graph/PortDescriptor.h"
#include "bazalt/engine/graph/PreviewDescriptor.h"
#include "bazalt/engine/graph/NoteEvent.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace bazalt::engine
{
    struct NodePrepareInfo
    {
        double sampleRate = 44100.0;
        int maxBlockSize = 512;
    };

    /** NODE_EDITOR.md §3 — how a node's ports/controls are arranged.
        Decoration nodes (Frame/Header/Image, the Reroute utility) carry no
        signal through the ordinary schedule; GraphCompiler skips them.
    */
    enum class NodeLayoutVariant
    {
        Standard,
        Horizontal,
        Singleton,
        Decoration
    };

    /** Base class every DSP node implements (ARCHITECTURE.md §3.6):
        declared ports/parameters (metadata only — the UI can describe a
        node generically from this alone, never touching DSP code),
        prepare/reset/process, and state save/restore for patch load.

        Every concrete node in M2 implements processSample() directly (the
        two hardcoded proof graphs are small enough that per-sample-only
        nodes are simplest); processBlock() defaults to looping it. A node
        backed by genuinely block-rate DSP (e.g. a future FFT-based node)
        would override processBlock() instead and leave supportsPerSample()
        false — the compiler rejects placing such a node inside a detected
        feedback cycle (ARCHITECTURE.md §3.4) rather than silently breaking.

        Ports are addressed by index at runtime (the compiler resolves
        stable string IDs from getInputPorts()/getOutputPorts() once, at
        compile time) — process() itself never sees the string IDs, only
        flat pointer arrays in port-declaration order.
    */
    class Node
    {
    public:
        virtual ~Node() = default;

        virtual void prepare (const NodePrepareInfo&) {}
        virtual void reset() {}

        // Metadata for UI/compile-time introspection only — these allocate
        // (return by value) and must never be called from the audio-thread
        // process path. getNumInputPorts()/getNumOutputPorts() below are
        // the audio-thread-safe equivalents the default processBlock() and
        // the compiler's per-sample region loop actually use.
        virtual std::vector<PortDescriptor> getInputPorts() const { return {}; }
        virtual std::vector<PortDescriptor> getOutputPorts() const { return {}; }
        virtual std::vector<ParameterDescriptor> getParameters() const { return {}; }

        /** docs/CLEANUP.md Priority 1 #2 — true for a node whose declared
            port type is decided by what's connected to it (currently only
            RerouteNode) rather than fixed at construction. GraphCompiler
            resolves these (resolveIncomingSignalType()) before running
            canConnect() on any connection touching this node, iterating to
            a fixed point so a chain of several such nodes resolves
            correctly regardless of declaration order. Most nodes have
            fixed ports and never override either method below.
        */
        virtual bool hasPolymorphicPorts() const noexcept { return false; }

        /** Growable port groups (SIGNAL_TYPES.md §6; PortGroups.h has the
            whole mechanism). -1 (the default) means this node has no
            growable group. Otherwise: how many members of the group the
            node currently declares from getInputPorts(), which
            GraphCompiler sets via setGroupPortCount() — derived from the
            connections, never stored in the graph — before it reads the
            node's ports. Message-thread only; never called from the audio
            thread, and never on a node the audio thread might be running
            (the compiler won't reuse a group node whose count changes).
        */
        virtual int getGroupPortCount() const noexcept { return -1; }
        virtual void setGroupPortCount (int count) noexcept { juce::ignoreUnused (count); }

        /** Called with the resolved SignalType of whatever currently feeds
            this node's own polymorphic input, once per compile, before that
            connection (or any connection from this node's own output) is
            validated. Only meaningful when hasPolymorphicPorts() is true.
            Message-thread only (compile time), never called from the audio
            thread. Scoped to the SignalTypes that share the ordinary
            block-buffer/Note mechanisms (Audio/Control/Boolean/Event/
            Spectral/Note) — SignalType::Data is a fundamentally different
            runtime representation (a DataPublisher-swapped pointer, not a
            per-sample value) and isn't handled by this mechanism; a node
            overriding this should reject/ignore a Data-typed incoming
            connection rather than claim to support it.
        */
        virtual void resolveIncomingSignalType (SignalType incomingType) noexcept { juce::ignoreUnused (incomingType); }

        /** M20 — a node's own default visualization(s), declared once here
            rather than hardcoded anywhere in the UI (NodeCard.tsx needs no
            edit for a new node to get a working preview). Empty default:
            most nodes declare nothing and keep their current layout
            unchanged; a node like random.stepped (once built) declares one
            entry on its "out" port. See PreviewDescriptor.h for the full
            taxonomy and which kinds are real vs. documented-for-later.
        */
        virtual std::vector<PreviewDescriptor> getPreviews() const { return {}; }

        // Node-level UI metadata (M7, NODE_EDITOR.md §3) — defaulted so
        // every pre-M7 node keeps compiling unchanged; retrofitting real
        // values is cheap and expected wherever a node's identity actually
        // matters to the Add menu (NodeFactory::describeAll() is what
        // reads these, never the audio thread).
        virtual juce::String getTitle() const { return {}; }       // falls back to the type id in the UI if empty
        virtual juce::String getCategory() const { return "Uncategorized"; }
        virtual NodeLayoutVariant getLayoutVariant() const { return NodeLayoutVariant::Standard; }
        virtual juce::String getIcon() const { return {}; }

        virtual int getNumInputPorts() const noexcept { return 0; }
        virtual int getNumOutputPorts() const noexcept { return 0; }

        virtual void setParameter (const juce::String& parameterId, float value)
        {
            juce::ignoreUnused (parameterId, value);
        }

        virtual bool supportsPerSample() const noexcept { return true; }

        /** inputs[i]/outputs[j] are single sample values, in the same order
            as getInputPorts()/getOutputPorts(). Must not allocate, lock,
            log, or block — this is the audio-thread-safe entry point.
        */
        virtual void processSample (const float* inputs, float* outputs) noexcept
        {
            juce::ignoreUnused (inputs, outputs);
            jassertfalse; // node claims supportsPerSample() but didn't override this
        }

        /** inputs[i]/outputs[j] point to numSamples-length buffers, in port
            order. Default implementation loops processSample() once per
            sample — correct for every M2 node, and exists so the compiler
            has one call for block-rate schedule steps regardless of
            whether the node happens to also support per-sample execution.
        */
        virtual void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept
        {
            // Fixed-size scratch, not std::vector: this default runs on the
            // audio thread once wired into a live plugin (M3), so it must
            // not allocate. Bumped from 8 in M18 and to 32 in M21 — keep in sync
            // with ExecutionPlan.h's own maxPortsPerNode (see its comment).
            static constexpr int maxPortsPerNode = 32;
            const auto numInputs = getNumInputPorts();
            const auto numOutputs = getNumOutputPorts();
            jassert (numInputs <= maxPortsPerNode && numOutputs <= maxPortsPerNode);

            float inSample[maxPortsPerNode] {};
            float outSample[maxPortsPerNode] {};

            for (int i = 0; i < numSamples; ++i)
            {
                for (int in = 0; in < numInputs; ++in)
                    inSample[in] = inputs[in][i];

                processSample (inSample, outSample);

                for (int out = 0; out < numOutputs; ++out)
                    outputs[out][i] = outSample[out];
            }
        }

        /** M18 (ADR-0024) — only meaningful for a node with a connected
            output port of `SignalType::Note`. Called once per block-rate
            schedule step, immediately before `processBlock()`, filling
            `output[0..numSamples)` with this node's per-sample Note state.
            Must not allocate, lock, log, or block, exactly like
            `processSample()`/`processBlock()`.
        */
        virtual void produceNoteBlock (NoteEvent* output, int numSamples) noexcept
        {
            juce::ignoreUnused (output, numSamples);
        }

        /** M18 (ADR-0024) — only meaningful for a node with a connected
            input port of `SignalType::Note`. Called once per block-rate
            schedule step, immediately before `processBlock()`, handing the
            node `numSamples` of the producer's per-sample Note state.
            `input` is owned by the `ExecutionPlan` and is only valid for
            the duration of this call — a node that needs it during its own
            `processBlock()` override should store the pointer as a member
            for that one call, not retain it past it.
        */
        virtual void consumeNoteBlock (const NoteEvent* input, int numSamples) noexcept
        {
            juce::ignoreUnused (input, numSamples);
        }
    };
}
