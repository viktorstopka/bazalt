#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "mix.downmix" (M16). `NODE_CATALOG (1).md`'s
        stereo -> mono adapter — two Audio inputs ("left"/"right"), one
        Audio output. Real and useful on its own (any hand-built stereo
        fold-down), but **not yet auto-insertable** by `canConnect`'s
        channels rule the way `adapt.map`/`adapt.normalise`/
        `adapt.threshold` are: those are 1-in-1-out transformers a single
        `AdapterStep` can splice inline; this is 2-in-1-out, and no real
        node produces a genuine multi-channel-per-port signal yet for it
        to receive from automatically (`CanConnect.cpp`'s own comment on
        `connectAudio` has the full explanation). Build it now because
        it's real, correct, and needed the moment a real stereo node
        exists — just don't expect `connectWithAutoAdapt` to wire it in
        for you yet.
    */
    class DownmixNode : public Node
    {
    public:
        static constexpr int numInputs = 2;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Downmix"; }
        juce::String getCategory() const override { return "Adapters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { "left", SignalType::Audio }, { "right", SignalType::Audio } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "mix.downmix.mode",
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
            if (parameterId == "mix.downmix.mode")
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
