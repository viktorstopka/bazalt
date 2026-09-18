#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "excite.burst". No inputs, one Audio output: a
        decaying burst of white noise on trigger(), silence otherwise. The
        Karplus-Strong proof graph's "pluck" excitation (ARCHITECTURE.md
        §3.4) — fed into the feedback loop's Mix node from outside the
        per-sample region.
    */
    class NoiseBurstNode : public Node
    {
    public:
        static constexpr int numInputs = 0;
        static constexpr int numOutputs = 1;

        void reset() override { remainingSamples = 0; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Noise Burst"; }
        juce::String getCategory() const override { return "Generators"; }

        std::vector<PortDescriptor> getInputPorts() const override { return {}; }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        /** Starts a burst lasting durationSamples, amplitude decaying
            linearly to zero over that span.
        */
        void trigger (int durationSamples) noexcept
        {
            remainingSamples = durationSamples;
            totalSamples = juce::jmax (1, durationSamples);
        }

        void processSample (const float*, float* outputs) noexcept override
        {
            if (remainingSamples <= 0)
            {
                outputs[0] = 0.0f;
                return;
            }

            const auto envelope = (float) remainingSamples / (float) totalSamples;
            outputs[0] = (random.nextFloat() * 2.0f - 1.0f) * envelope;
            --remainingSamples;
        }

    private:
        juce::Random random;
        int remainingSamples = 0;
        int totalSamples = 1;
    };
}
