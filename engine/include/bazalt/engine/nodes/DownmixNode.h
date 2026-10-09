#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "channels.downmix" (M16). Real stereo cable redesign
        (`wiki/NODES.System.md` §9): now a genuine 1-in-1-out node — one
        `Channels::Stereo` `in` port, one mono `out` — which finally makes it
        fit `connectWithAutoAdapt`'s 1-in-1-out `AdapterStep` splice
        mechanism the same way `math.map`/`logic.threshold` already do. Before this redesign it took two
        separate mono inputs (`left`/`right`), which never fit that
        mechanism (`CanConnect.cpp`'s own comment used to explain why) —
        that limitation is closed now, not worked around.
    */
    class DownmixNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }
        int getNumInputChannels() const noexcept override { return 2; }

        juce::String getTitle() const override { return "Downmix"; }
        juce::String getCategory() const override { return "Channels"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio, .channels = Channels::Stereo },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { .id = "out", .type = SignalType::Signal, .quantity = Quantity::Audio } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "channels.downmix.mode",
                                            .minValue = 0.0f,
                                            .maxValue = 4.0f,
                                            .defaultValue = 2.0f, // "mid" — see modeForValue()
                                            .displayName = "Mode",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "sum", "Sum" },
                                                              { "left", "Left" },
                                                              { "mid", "Mid" },
                                                              { "right", "Right" },
                                                              { "side", "Side" } },
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "channels.downmix.mode")
                mode = modeForValue (value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto left = inputs[0];
            const auto right = inputs[1];

            switch (mode)
            {
                case Mode::Sum:   outputs[0] = left + right; break;
                case Mode::Left:  outputs[0] = left; break;
                case Mode::Right: outputs[0] = right; break;
                case Mode::Side:  outputs[0] = (left - right) * 0.5f; break;
                case Mode::Mid:
                default:          outputs[0] = (left + right) * 0.5f; break;
            }
        }

    private:
        enum class Mode
        {
            Sum,
            Left,
            Mid,
            Right,
            Side
        };

        // Matches enumOptions' declaration order above (sum=0, left=1,
        // mid=2, right=3, side=4) — same index-coupling caveat as
        // OscillatorNode.h's waveformForShapeValue(), same reason.
        static Mode modeForValue (float value) noexcept
        {
            const auto index = (int) (value + 0.5f);
            switch (index)
            {
                case 0:  return Mode::Sum;
                case 1:  return Mode::Left;
                case 3:  return Mode::Right;
                case 4:  return Mode::Side;
                default: return Mode::Mid;
            }
        }

        Mode mode = Mode::Mid;
    };
}
