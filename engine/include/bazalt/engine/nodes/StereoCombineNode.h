#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "stereo.combine". Milestone 0.2 (wiki/NODES.System.md
        §9.4): the other half of the independent-per-channel bridge —
        `StereoSplitNode.h`'s inverse. Two ordinary, individually-wireable
        mono inputs (`left`/`right`, deliberately NOT marked Stereo, so two
        genuinely different sources can each feed one side); one paired
        stereo output (`left`/`right`, both `Channels::Stereo`) that plugs
        into any stereo-shaped input as one gesture. Trivial passthrough —
        Pattern B inline DSP, nothing here worth a standalone primitive.
    */
    class StereoCombineNode : public Node
    {
    public:
        static constexpr int numInputs = 2;
        static constexpr int numOutputs = 2;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Stereo Combine"; }
        juce::String getCategory() const override { return "Adapters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            // "in.left"/"in.right", not "left"/"right": a port id must be
            // unique across a node's inputs AND outputs, and this node's
            // own outputs are genuinely "left"/"right" (see getOutputPorts()
            // below) — WidthNode.h's own naming precedent for exactly this
            // same-node-both-sides-stereo shape.
            return {
                PortDescriptor { .id = "in.left", .type = SignalType::Audio, .label = "Left" },
                PortDescriptor { .id = "in.right", .type = SignalType::Audio, .label = "Right" },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "left", .type = SignalType::Audio, .label = "Left", .isPrimaryOutput = true, .channels = Channels::Stereo },
                PortDescriptor { .id = "right", .type = SignalType::Audio, .label = "Right", .channels = Channels::Stereo },
            };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = inputs[0];
            outputs[1] = inputs[1];
        }
    };
}
