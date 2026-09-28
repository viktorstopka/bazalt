#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "stereo.combine". Real stereo cable redesign
        (`wiki/NODES.System.md` §9): the other half of the independent-per-
        channel bridge — `StereoSplitNode.h`'s inverse. Two ordinary,
        individually-wireable mono inputs (`left`/`right`, deliberately NOT
        marked Stereo, so two genuinely different sources can each feed one
        side); one real stereo output (`out`, `Channels::Stereo`) that plugs
        into any stereo-shaped destination as one cable. Trivial
        passthrough — Pattern B inline DSP, nothing here worth a standalone
        primitive.

        `getNumOutputChannels()` must be overridden (see `Node.h`'s own
        comment) since the default would otherwise report 1 (one
        descriptor), not the 2 real flat channels `processBlock`'s scratch
        loop needs to actually reach the right channel.
    */
    class StereoCombineNode : public Node
    {
    public:
        static constexpr int numInputs = 2;  // left, right
        static constexpr int numOutputs = 1; // out (Stereo)

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }
        int getNumOutputChannels() const noexcept override { return 2; }

        juce::String getTitle() const override { return "Stereo Combine"; }
        juce::String getCategory() const override { return "Adapters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "left", .type = SignalType::Audio, .label = "Left" },
                PortDescriptor { .id = "right", .type = SignalType::Audio, .label = "Right" },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = SignalType::Audio, .label = "Out", .isPrimaryOutput = true, .channels = Channels::Stereo },
            };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = inputs[0];
            outputs[1] = inputs[1];
        }
    };
}
