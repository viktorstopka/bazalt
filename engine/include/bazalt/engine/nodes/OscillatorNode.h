#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/PolyBlepOscillator.h"
#include <algorithm>

namespace bazalt::engine::nodes
{
    /** Stable type id: "osc.basic". No inputs; one Audio output. Frequency
        and shape are set directly (not through a port — the M2 proof
        graphs drive them from outside via setParameter/setFrequency, ahead
        of full Note-port routing which lands once a real node-graph editor
        needs it — see CLAUDE.md's "known interim simplifications").
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

        juce::String getTitle() const override { return "Oscillator"; }
        juce::String getCategory() const override { return "Generators"; }

        std::vector<PortDescriptor> getInputPorts() const override { return {}; }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { { "osc.basic.frequency", 20.0f, 20000.0f, 440.0f, 0.3f, "Hz", "Frequency" },
                     { "osc.basic.shape", 0.0f, 3.0f, 1.0f, 1.0f, "", "Shape" } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "osc.basic.frequency")
                oscillator.setFrequency (value);
            else if (parameterId == "osc.basic.shape")
                setWaveform (waveformForShapeValue (value));
        }

        void setWaveform (OscillatorWaveform waveform) noexcept { oscillator.setWaveform (waveform); }

        void processSample (const float*, float* outputs) noexcept override
        {
            outputs[0] = oscillator.renderNextSample();
        }

    private:
        // "shape" is a macro-automatable stand-in for waveform selection —
        // continuous input, quantized to one of the four PolyBLEP
        // waveforms, matching the "oscillator shape" macro target
        // ARCHITECTURE.md §4.3 asks for. Sine=0, Saw=1, Square=2,
        // Triangle=3, matching OscillatorWaveform's declaration order.
        static OscillatorWaveform waveformForShapeValue (float value) noexcept
        {
            const auto index = (int) std::round (std::clamp (value, 0.0f, 3.0f));

            switch (index)
            {
                case 0:  return OscillatorWaveform::Sine;
                case 2:  return OscillatorWaveform::Square;
                case 3:  return OscillatorWaveform::Triangle;
                default: return OscillatorWaveform::Saw;
            }
        }

        PolyBlepOscillator oscillator;
    };
}
