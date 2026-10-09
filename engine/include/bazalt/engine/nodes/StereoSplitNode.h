#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "stereo.split". Real stereo cable redesign
        (`wiki/NODES.System.md` §9): the bridge node independent per-channel
        wiring needs — takes one real stereo cable (`in`, `Channels::Stereo`)
        and re-exposes each side as an ordinary, individually-wireable mono
        output (`left`/`right`, deliberately NOT marked Stereo) for whatever
        needs to treat the two channels asymmetrically from here on
        (different filters per side, a Haas-style delay offset, ...).
        Trivial passthrough — Pattern B inline DSP, nothing here worth a
        standalone primitive.

        `getNumInputChannels()` must be overridden (see `Node.h`'s own
        comment) since the default would otherwise report 1 (one
        descriptor), not the 2 real flat channels `processBlock`'s scratch
        loop needs to actually reach the right channel.
    */
    class StereoSplitNode : public Node
    {
    public:
        static constexpr int numInputs = 1;  // in (Stereo)
        static constexpr int numOutputs = 2; // left, right

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }
        int getNumInputChannels() const noexcept override { return 2; }

        juce::String getTitle() const override { return "Stereo Split"; }
        juce::String getCategory() const override { return "Adapters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio, .channels = Channels::Stereo },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "left", .type = SignalType::Signal, .label = "Left", .isPrimaryOutput = true, .quantity = Quantity::Audio },
                PortDescriptor { .id = "right", .type = SignalType::Signal, .label = "Right", .quantity = Quantity::Audio },
            };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = inputs[0];
            outputs[1] = inputs[1];
        }
    };
}
