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

        M20 (direct feedback: "there is no reason why ADSR ... wouldn't be
        modulatable"): attack/decay/sustain/release move from
        ParameterDescriptor to real Control-type input ports, same
        NaN-sentinel pattern, same dotted ids (`env.adsr.attack` etc. —
        unchanged so an existing saved patch's stored value still applies
        via the exact same setParameter() call, whether or not anything's
        actually wired to it). `juce::ADSR::setParameters()` is cheap
        (coefficient recalculation only, no allocation — it's already
        called from the host-automation path, which the audio thread's own
        allocation trap already proves RT-safe) so calling it every sample
        while modulated costs nothing worth guarding against.
    */
    class AdsrNode : public Node
    {
    public:
        static constexpr int numInputs = 5; // gate, attack, decay, sustain, release
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
            return {
                PortDescriptor { .id = "gate", .type = SignalType::Boolean, .hasFallbackWhenUnconnected = true },
                ValueTypes::timeSecondsPort ("env.adsr.attack", "Attack", 0.01f),
                ValueTypes::timeSecondsPort ("env.adsr.decay", "Decay", 0.1f),
                PortDescriptor { .id = "env.adsr.sustain", .type = SignalType::Control, .label = "Sustain",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.7f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar, .polarity = Polarity::Unipolar },
                ValueTypes::timeSecondsPort ("env.adsr.release", "Release", 0.2f),
            };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Control } };
        }

        std::vector<ParameterDescriptor> getParameters() const override { return {}; }

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

            // Same NaN fallback for each stage's own value — unconnected
            // means "leave whatever setParameter() last set," connected
            // means "track this signal live," per-input, independently.
            bool changed = false;
            if (! std::isnan (inputs[1])) { parameters.attack = inputs[1]; changed = true; }
            if (! std::isnan (inputs[2])) { parameters.decay = inputs[2]; changed = true; }
            if (! std::isnan (inputs[3])) { parameters.sustain = inputs[3]; changed = true; }
            if (! std::isnan (inputs[4])) { parameters.release = inputs[4]; changed = true; }
            if (changed)
                adsr.setParameters (parameters);

            outputs[0] = adsr.getNextSample();
        }

    private:
        juce::ADSR adsr;
        juce::ADSR::Parameters parameters;
        bool previousGate = false;
    };
}
