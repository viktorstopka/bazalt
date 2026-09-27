#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "stereo.split". Milestone 0.2 (wiki/NODES.System.md
        §9.4): the bridge node independent per-channel wiring needs now
        that stereo-capable nodes present one paired socket instead of two
        separately-wireable mono ports. One stereo input (`in.left`/
        `in.right`, both `Channels::Stereo` — the UI wires a stereo pair
        into it as one gesture); two ordinary, individually-wireable mono
        outputs (`left`/`right`, deliberately NOT marked Stereo) for
        whatever needs to treat the two channels asymmetrically from here
        on (different filters per side, a Haas-style delay offset, ...).
        Trivial passthrough — Pattern B inline DSP, nothing here worth a
        standalone primitive.
    */
    class StereoSplitNode : public Node
    {
    public:
        static constexpr int numInputs = 2;
        static constexpr int numOutputs = 2;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Stereo Split"; }
        juce::String getCategory() const override { return "Adapters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "in.left", .type = SignalType::Audio, .label = "In L", .channels = Channels::Stereo },
                PortDescriptor { .id = "in.right", .type = SignalType::Audio, .label = "In R", .channels = Channels::Stereo },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "left", .type = SignalType::Audio, .label = "Left", .isPrimaryOutput = true },
                PortDescriptor { .id = "right", .type = SignalType::Audio, .label = "Right" },
            };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = inputs[0];
            outputs[1] = inputs[1];
        }
    };
}
