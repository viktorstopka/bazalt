#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/PolyBlepOscillator.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "osc.basic". No inputs; one Audio output. Frequency
        is set directly (not through a port — the M2 proof graphs drive it
        from outside via setParameter/setFrequency, ahead of full Note-port
        routing which lands in M3 with the real MIDI-driven voice model).
    */
    class OscillatorNode : public Node
    {
    public:
        static constexpr int numInputs = 0;
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override { oscillator.prepare (info.sampleRate); }
        void reset() override { oscillator.reset(); }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        std::vector<PortDescriptor> getInputPorts() const override { return {}; }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { { "osc.basic.frequency", 20.0f, 20000.0f, 440.0f, 0.3f, "Hz", "Frequency" } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "osc.basic.frequency")
                oscillator.setFrequency (value);
        }

        void setWaveform (OscillatorWaveform waveform) noexcept { oscillator.setWaveform (waveform); }

        void processSample (const float*, float* outputs) noexcept override
        {
            outputs[0] = oscillator.renderNextSample();
        }

    private:
        PolyBlepOscillator oscillator;
    };
}
