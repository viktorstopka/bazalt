#pragma once

#include "bazalt/engine/graph/PortDescriptor.h"
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
            // not allocate. Every M2 node stays well under this port count.
            static constexpr int maxPortsPerNode = 8;
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
    };
}
