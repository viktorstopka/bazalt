#pragma once

#include "bazalt/engine/graph/Node.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace bazalt::engine::nodes
{
    /** Stable type id: "env.adsr". No inputs; one Control output (the
        envelope value, 0..1). Gate is triggered via noteOn()/noteOff(),
        called directly by whoever owns the voice (the render-cli driver in
        M2; the real voice manager once MIDI input lands in M3) — not
        routed through an Event port yet, matching OscillatorNode's
        frequency.
    */
    class AdsrNode : public Node
    {
    public:
        static constexpr int numInputs = 0;
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override
        {
            adsr.setSampleRate (info.sampleRate);
            adsr.setParameters (parameters);
        }

        void reset() override { adsr.reset(); }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "ADSR"; }
        juce::String getCategory() const override { return "Envelopes"; }

        std::vector<PortDescriptor> getInputPorts() const override { return {}; }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Control } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { { "env.adsr.attack", 0.0f, 10.0f, 0.01f, 0.5f, "s", "Attack" },
                     { "env.adsr.decay", 0.0f, 10.0f, 0.1f, 0.5f, "s", "Decay" },
                     { "env.adsr.sustain", 0.0f, 1.0f, 0.7f, 1.0f, "", "Sustain" },
                     { "env.adsr.release", 0.0f, 10.0f, 0.2f, 0.5f, "s", "Release" } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "env.adsr.attack")
                parameters.attack = value;
            else if (parameterId == "env.adsr.decay")
                parameters.decay = value;
            else if (parameterId == "env.adsr.sustain")
                parameters.sustain = value;
            else if (parameterId == "env.adsr.release")
                parameters.release = value;
            else
                return;

            adsr.setParameters (parameters);
        }

        void noteOn() noexcept { adsr.noteOn(); }
        void noteOff() noexcept { adsr.noteOff(); }
        bool isActive() const noexcept { return adsr.isActive(); }

        void processSample (const float*, float* outputs) noexcept override
        {
            outputs[0] = adsr.getNextSample();
        }

    private:
        juce::ADSR adsr;
        juce::ADSR::Parameters parameters;
    };
}
