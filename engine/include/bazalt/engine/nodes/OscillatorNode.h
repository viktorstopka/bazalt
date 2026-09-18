#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/PolyBlepOscillator.h"
#include <algorithm>

namespace bazalt::engine::nodes
{
    /** Stable type id: "osc.analog". No inputs; one Audio output. Frequency
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
