#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "delay.line". One Audio input, one Audio output,
        plus a Control input for the delay length (samples) — a plain
        circular-buffer delay line. Generic primitive; the Karplus-Strong
        proof graph's feedback loop closes through this (ARCHITECTURE.md
        §3.4) — the buffer itself is the pitch period.

        Buffer is sized to maxDelaySamples in prepare() (allocates — fine,
        that's compile-time, off the audio thread); the active delay length
        is clamped to that ceiling, changeable without reallocating.

        Delay length used to be a plain ParameterDescriptor (no cable could
        ever drive it). It's now a real Control-type input port instead
        (PortDescriptor::hasFallbackWhenUnconnected — see that field's own
        comment, and GraphCompiler.cpp's matching one, for the NaN-sentinel
        mechanism this relies on) so it can be modulated like any other
        signal — direct feedback: "modulating simple things such as delay
        time is the whole point of such [modular] software." setParameter()
        still exists and still drives `delaySamples` exactly as before;
        that's what processSample() falls back to whenever nothing's
        actually wired into the port, so every existing caller that sets
        this via setParameter() (compile-time graph parameters or a
        post-compile call on the live node — ProofGraphs.h, render-cli,
        ProofGraphRenderTests.cpp) keeps working completely unchanged.
    */
    class DelayNode : public Node
    {
    public:
        static constexpr int numInputs = 2;
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

        juce::String getTitle() const override { return "Delay"; }
        juce::String getCategory() const override { return "Effects"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ "in", SignalType::Audio }),
                ValueTypes::timeSamplesPort ("delay.line.samples", "By", maxDelaySamples, 200.0f),
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { perChannel ({ "out", SignalType::Audio }) };
        }

        std::vector<ParameterDescriptor> getParameters() const override { return {}; }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "delay.line.samples")
                delaySamples = juce::jlimit (1, maxDelaySamples, (int) std::lround (value));
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            // NaN means the port is unconnected (GraphCompiler.cpp's
            // hasFallbackWhenUnconnected sentinel) — fall back to whatever
            // setParameter() last set; a real connection overrides it,
            // re-clamped per sample since a live modulator isn't bound by
            // setParameter()'s own clamp.
            const auto modulation = inputs[1];
            const auto activeSamples = std::isnan (modulation) ? delaySamples : juce::jlimit (1, maxDelaySamples, (int) std::lround (modulation));

            auto readIndex = writeIndex - activeSamples;
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
