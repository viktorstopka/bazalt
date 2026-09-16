#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "delay.basic". One Audio input, one Audio output,
        a plain circular-buffer delay line. Generic primitive; the
        Karplus-Strong proof graph's feedback loop closes through this
        (ARCHITECTURE.md §3.4) — the buffer itself is the pitch period.

        Buffer is sized to maxDelaySamples in prepare() (allocates — fine,
        that's compile-time, off the audio thread); the active delay length
        is a parameter clamped to that ceiling, changeable without
        reallocating.
    */
    class DelayNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        explicit DelayNode (int maxDelaySamplesIn = 4096) noexcept : maxDelaySamples (maxDelaySamplesIn) {}

        void prepare (const NodePrepareInfo&) override
        {
            buffer.assign ((size_t) maxDelaySamples, 0.0f);
            writeIndex = 0;
        }

        void reset() override
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writeIndex = 0;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { { "in", SignalType::Audio } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { { "delay.basic.samples", 1.0f, (float) maxDelaySamples, 200.0f, 1.0f, "samples", "Delay length" } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "delay.basic.samples")
                delaySamples = juce::jlimit (1, maxDelaySamples, (int) std::lround (value));
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            auto readIndex = writeIndex - delaySamples;
            if (readIndex < 0)
                readIndex += maxDelaySamples;

            outputs[0] = buffer[(size_t) readIndex];

            buffer[(size_t) writeIndex] = inputs[0];
            writeIndex = (writeIndex + 1) % maxDelaySamples;
        }

    private:
        int maxDelaySamples;
        int delaySamples = 200;
        std::vector<float> buffer;
        int writeIndex = 0;
    };
}
