#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "env.adsr". One Control output (the envelope value,
        0..1). Gate can still be triggered directly via noteOn()/noteOff()
        (the render-cli driver, tests) — M18 (ADR-0024) adds a real "gate"
        input port using the same `hasFallbackWhenUnconnected` NaN-sentinel
        `DelayNode.h`/`OscillatorNode.h`'s "pitch" established: unconnected
        reads NaN and this node keeps behaving exactly as before (external
        noteOn()/noteOff() calls drive `adsr` directly); connected, a
        rising/falling edge on the incoming boolean level calls the SAME
        noteOn()/noteOff() internally instead — real gate-port wiring and a
        direct C++ poke are two paths to one implementation, never two.
    */
    class AdsrNode : public Node
    {
    public:
        static constexpr int numInputs = 1; // gate
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override
        {
            adsr.setSampleRate (info.sampleRate);
            adsr.setParameters (parameters);
        }

        void reset() override
        {
            adsr.reset();
            previousGate = false;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "ADSR"; }
        juce::String getCategory() const override { return "Envelopes"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "gate", .type = SignalType::Boolean, .hasFallbackWhenUnconnected = true } };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Control } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ValueTypes::timeSecondsParameter ("env.adsr.attack", "Attack", 0.01f),
                     ValueTypes::timeSecondsParameter ("env.adsr.decay", "Decay", 0.1f),
                     { "env.adsr.sustain", 0.0f, 1.0f, 0.7f, 1.0f, "", "Sustain" },
                     ValueTypes::timeSecondsParameter ("env.adsr.release", "Release", 0.2f) };
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

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            // NaN means "gate" is unconnected (GraphCompiler.cpp's
            // hasFallbackWhenUnconnected sentinel) — leave `adsr` exactly
            // as external noteOn()/noteOff() calls left it. A real
            // connection edge-detects the incoming boolean level instead
            // (0.5 threshold) and calls the same noteOn()/noteOff().
            if (! std::isnan (inputs[0]))
            {
                const auto gate = inputs[0] > 0.5f;
                if (gate && ! previousGate)
                    adsr.noteOn();
                else if (! gate && previousGate)
                    adsr.noteOff();
                previousGate = gate;
            }

            outputs[0] = adsr.getNextSample();
        }

    private:
        juce::ADSR adsr;
        juce::ADSR::Parameters parameters;
        bool previousGate = false;
    };
}
