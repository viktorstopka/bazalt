#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "io.output". Two Audio inputs, two Audio outputs,
        unity pass-through each — the "Master Out" node in the design
        reference. Deliberately just a conventional, visually-meaningful
        anchor point in the graph, not a compiler special case:
        `NodeGraph::setOutput()` designates this node's own primary output
        port exactly as it would any other node's, so the existing
        final-output mechanism needs no changes to support it.

        Milestone 0.2 (wiki/NODES.System.md §9): gained a real second
        channel — `"in"`/`"out"` keep their exact shipped ids (CLAUDE.md
        rule 3; this is the node behind the graph's real, already-saved
        Master Out), now meaning the left channel, and a new `"right"`
        port sits beside each. Both pairs are marked `Channels::Stereo`,
        which is what `GraphCompiler.cpp`'s "Final output" resolution
        (§9's declaration-order pairing rule) and `PluginProcessor.cpp`'s
        `finalizeInstanceMixIntoOutput` read to find the second channel — a
        graph that never wires anything into `"right"` behaves exactly as
        before (an unconnected Audio input reads silence, matching the old
        node's own mono behaviour byte for byte).
    */
    class OutputNode : public Node
    {
    public:
        static constexpr int numInputs = 2;  // in (left), right
        static constexpr int numOutputs = 2; // out (left), right

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Master Out"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "in", .type = SignalType::Audio, .label = "In", .channels = Channels::Stereo },
                PortDescriptor { .id = "right", .type = SignalType::Audio, .label = "Right", .channels = Channels::Stereo },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            // "outRight", not "right": a port id must be unique across a
            // node's inputs AND outputs (findTappableBufferIndex()/tap
            // naming identify a port by id alone, asserted for every
            // registered type by ExecutionPlanTapTests.cpp) — WidthNode.h
            // hit this exact issue first and is why its own inputs are
            // "in.left"/"in.right" rather than reusing "left"/"right".
            return {
                PortDescriptor { .id = "out", .type = SignalType::Audio, .label = "Out", .isPrimaryOutput = true, .channels = Channels::Stereo },
                PortDescriptor { .id = "outRight", .type = SignalType::Audio, .label = "Right", .channels = Channels::Stereo },
            };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = inputs[0];
            outputs[1] = inputs[1];
        }
    };
}
