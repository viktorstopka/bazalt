#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/PolyBlepOscillator.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "osc.analog". One Audio output. Shape stays a plain
        ParameterDescriptor — it's a discrete waveform *selector*
        (isStructural: switching it swaps which PolyBLEP correction table
        renderNextSample() uses), not a continuous value, so modulating it
        at signal rate would just be jarring rather than something a
        listener would call "modulation" (the same reasoning that keeps
        instance.mix/instance.allocator's own mode-style settings static).

        M18 (ADR-0024) added a real "pitch" input port — absolute
        semitones, continuous, so a live pitch-bend needs no special-cased
        path — using the `hasFallbackWhenUnconnected` NaN-sentinel
        `DelayNode.h` established: unconnected reads NaN and this node
        falls back to whatever setParameter("osc.analog.frequency") last
        set; connected, the port's value (converted from semitones to Hz)
        drives the oscillator every sample instead.

        M20 (direct feedback: "there is no reason why ... Oscillator
        Frequency wouldn't be modulatable") adds a second, parallel
        "osc.analog.frequency" *port* alongside pitch (same dotted id the
        old parameter used, so an existing saved patch's stored value still
        applies unchanged) — a direct Hz value rather than pitch's
        semitone-relative-to-a-note framing, for modulating frequency
        directly (an LFO into a drone oscillator with no note-tracking
        upstream, say) without needing a pitch-domain conversion first.
        Pitch wins whenever both are connected (see processSample) since it
        already unconditionally overwrote frequency every sample connected
        or not; neither connected leaves the oscillator at whatever
        setParameter() last configured, exactly like before this change.
    */
    class OscillatorNode : public Node
    {
    public:
        static constexpr float defaultFrequencyHz = 440.0f; // also osc.analog.frequency's descriptor default
        static constexpr int numInputs = 2; // pitch, frequency
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override
        {
            oscillator.prepare (info.sampleRate);

            // Start at the frequency the card DISPLAYS (the port's own default,
            // 440 Hz). PolyBlepOscillator starts at 0 Hz - silent DC - so an
            // oscillator with nothing wired to its pitch used to be silent while
            // showing "Frequency 440 Hz". It never mattered while every
            // oscillator sat in a voice graph (the allocator always drives pitch);
            // it matters now that a graph with no allocator plays (M21). Runs
            // before the compiler applies the instance's saved parameters, so a
            // saved frequency still wins.
            oscillator.setFrequency (defaultFrequencyHz);
        }
        void reset() override { oscillator.reset(); }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Oscillator"; }
        juce::String getCategory() const override { return "Generators"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "pitch", .type = SignalType::Control, .unit = "st",
                                  .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 60.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Pitch },
                ValueTypes::frequencyPort ("osc.analog.frequency", "Frequency", defaultFrequencyHz),
            };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        /** M20 step 8: the first real node wired end to end onto the
            visualization system — a plain Waveform on the oscillator's own
            audio output, the simplest possible proof of the whole pipeline
            (declaration -> subscribeVisualizationTap -> tap push -> UI
            render). VisualizationTapTests.cpp already exercises this exact
            (nodeId "osc", portId "out") pair.
        */
        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::Waveform, .portId = "out" } };
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
            return { ParameterDescriptor { .id = "osc.analog.shape",
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
            // NaN means the port is unconnected (GraphCompiler.cpp's
            // hasFallbackWhenUnconnected sentinel). Pitch (semitones,
            // converted to Hz) wins whenever connected, matching its
            // pre-M20 behaviour of unconditionally overwriting frequency
            // every sample; frequency (direct Hz) drives it only when
            // pitch isn't connected; if neither is connected, the
            // oscillator is left at whatever setParameter() last
            // configured, exactly like before either port existed.
            if (! std::isnan (inputs[0]))
                oscillator.setFrequency (440.0f * std::pow (2.0f, (inputs[0] - 69.0f) / 12.0f));
            else if (! std::isnan (inputs[1]))
                oscillator.setFrequency (inputs[1]);

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
