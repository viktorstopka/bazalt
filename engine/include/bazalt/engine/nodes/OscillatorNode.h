#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/PolyBlepOscillator.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "osc.analog". One Audio output. Frequency and shape
        default to being set directly via setParameter (the M2 proof
        graphs' own pattern — still exactly how a graph that leaves "pitch"
        unconnected behaves). M18 (ADR-0024) adds a real "pitch" input
        port — absolute semitones, continuous, so a live pitch-bend needs
        no special-cased path — using the same `hasFallbackWhenUnconnected`
        NaN-sentinel `DelayNode.h` established: unconnected reads NaN and
        this node falls back to whatever setParameter("osc.analog.frequency")
        last set, exactly today's behaviour; connected, the port's value
        (converted from semitones to Hz) drives the oscillator every
        sample instead.
    */
    class OscillatorNode : public Node
    {
    public:
        static constexpr int numInputs = 1; // pitch
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override { oscillator.prepare (info.sampleRate); }
        void reset() override { oscillator.reset(); }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Oscillator"; }
        juce::String getCategory() const override { return "Generators"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "pitch", .type = SignalType::Control, .unit = "st",
                                       .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 60.0f,
                                       .hasFallbackWhenUnconnected = true, .quantity = Quantity::Pitch } };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            // "shape" is quantized to one of 4 waveforms by
            // waveformForShapeValue() below regardless of what's set here.
            // M14: given a real kind=Enum + enumOptions now (the first
            // node migrated to the new value contract, VALUE_MODEL.md §2)
            // — a real dropdown of named waveforms instead of a raw 0-3
            // slider. isStructural=true because switching waveform swaps
            // which PolyBLEP correction table renderNextSample() uses, not
            // because it reallocates (the strict VALUE_MODEL.md §5 test
            // is arguably borderline here; NODE_CATALOG.md's rewritten
            // catalog calls osc.analog.shape Structural explicitly, so
            // this migration follows that call rather than relitigating
            // it). Known, deliberate half-migration: the underlying
            // storage stays the same index-coupled float 0-3
            // waveformForShapeValue() switches on — VALUE_MODEL.md §2's
            // "nothing may rely on option index" isn't fully met yet,
            // that would mean changing setParameter()'s own storage
            // representation, a DSP-touching change out of scope for a
            // schema-only migration (this milestone's own exit criteria
            // requires bit-identical render-cli output). enumOptions'
            // order below matches waveformForShapeValue()'s switch only
            // because nothing has migrated off that coupling yet, not
            // because order is meant to matter.
            return { ValueTypes::frequencyParameter ("osc.analog.frequency", "Frequency", 440.0f),
                     ParameterDescriptor { .id = "osc.analog.shape",
                                            .minValue = 0.0f,
                                            .maxValue = 3.0f,
                                            .defaultValue = 1.0f,
                                            .displayName = "Shape",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "sine", "Sine" },
                                                              { "saw", "Saw" },
                                                              { "square", "Square" },
                                                              { "triangle", "Triangle" } },
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "osc.analog.frequency")
                oscillator.setFrequency (value);
            else if (parameterId == "osc.analog.shape")
                setWaveform (waveformForShapeValue (value));
        }

        void setWaveform (OscillatorWaveform waveform) noexcept { oscillator.setWaveform (waveform); }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            // NaN means "pitch" is unconnected (GraphCompiler.cpp's
            // hasFallbackWhenUnconnected sentinel) — fall back to whatever
            // setParameter("osc.analog.frequency") last set. A real
            // connection converts semitones -> Hz every sample instead,
            // re-evaluated live so a continuous pitch-bend needs no
            // special path (ADR-0024).
            if (! std::isnan (inputs[0]))
                oscillator.setFrequency (440.0f * std::pow (2.0f, (inputs[0] - 69.0f) / 12.0f));

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
