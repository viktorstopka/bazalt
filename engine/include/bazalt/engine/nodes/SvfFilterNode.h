#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "filter.svf". A state-variable filter with all five
        responses out at once — `out` (lowpass, primary), `bandpass`,
        `highpass`, `notch`, `peak` — rather than a mode switch: wire the one
        you want, or several (a crossover, a morph via mix.crossfade). The
        lowpass keeps the id `out` it always had, so existing patches are
        unchanged.

        Zavalishin's topology-preserving transform in Andy Simper's
        formulation: stable under audio-rate cutoff/resonance modulation,
        coefficients recomputed only when cutoff or resonance actually
        change. `resonance` is Q (0.707 = Butterworth). Per channel (each
        lane its own state).
    */
    class SvfFilterNode : public Node
    {
    public:
        static constexpr int numInputs = 3;  // in, cutoff, resonance
        static constexpr int numOutputs = 5; // lowpass (out), bandpass, highpass, notch, peak

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            cachedCutoff = cachedResonance = -1.0f;
            reset();
        }

        void reset() override { ic1 = ic2 = 0.0; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "SVF Filter"; }
        juce::String getCategory() const override { return "Filters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ "in", SignalType::Audio }),
                ValueTypes::frequencyPort ("filter.svf.cutoff", "Cutoff", 1000.0f),
                PortDescriptor { .id = "filter.svf.resonance", .type = SignalType::Control, .label = "Resonance",
                                  .minValue = 0.01f, .maxValue = 10.0f, .defaultValue = 0.70710678f,
                                  .hasFallbackWhenUnconnected = true },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            const auto output = [] (const char* id, const char* label, bool primary = false)
            {
                return perChannel (PortDescriptor { .id = id, .type = SignalType::Audio, .label = label, .isPrimaryOutput = primary });
            };
            return { output ("out", "Lowpass", true), output ("bandpass", "Bandpass"), output ("highpass", "Highpass"),
                     output ("notch", "Notch"), output ("peak", "Peak") };
        }

        std::vector<ParameterDescriptor> getParameters() const override { return {}; }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "filter.svf.cutoff")
                storedCutoff = value;
            else if (parameterId == "filter.svf.resonance")
                storedResonance = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto cutoff = std::isnan (inputs[1]) ? storedCutoff : inputs[1];
            const auto resonance = std::isnan (inputs[2]) ? storedResonance : inputs[2];
            if (cutoff != cachedCutoff || resonance != cachedResonance)
                updateCoefficients (cutoff, resonance);

            const auto x = (double) inputs[0];
            const auto v3 = x - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;

            const auto low = v2, band = v1, high = x - k * v1 - v2;
            outputs[0] = (float) low;
            outputs[1] = (float) band;
            outputs[2] = (float) high;
            outputs[3] = (float) (low + high);
            outputs[4] = (float) (low - high);
        }

    private:
        void updateCoefficients (float cutoff, float resonance) noexcept
        {
            cachedCutoff = cutoff;
            cachedResonance = resonance;
            const auto fc = juce::jlimit (1.0, sampleRate * 0.49, (double) cutoff);
            const auto g = std::tan (juce::MathConstants<double>::pi * fc / sampleRate);
            k = 1.0 / juce::jlimit (0.01, 100.0, (double) resonance);
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        double sampleRate = 48000.0;
        double ic1 = 0.0, ic2 = 0.0, a1 = 1.0, a2 = 0.0, a3 = 0.0, k = 1.414;
        float storedCutoff = 1000.0f, storedResonance = 0.70710678f;
        float cachedCutoff = -1.0f, cachedResonance = -1.0f;
    };
}
