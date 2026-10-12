#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/SvfCore.h"
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
        change (`SvfCore.h`, shared with the EQ). `resonance` is Q (0.707 =
        Butterworth). Per channel (each lane its own state).
    */
    class SvfFilterNode : public Node
    {
    public:
        static constexpr int numInputs = 3;  // in, cutoff, resonance
        static constexpr int numOutputs = 5; // lowpass (out), bandpass, highpass, notch, peak

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            core.invalidate();
            reset();
        }

        void reset() override { core.reset(); }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "SVF Filter"; }
        juce::String getCategory() const override { return "Filters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio }),
                ValueTypes::frequencyPort ("filter.svf.cutoff", "Cutoff", 1000.0f),
                PortDescriptor { .id = "filter.svf.resonance", .type = SignalType::Signal, .label = "Resonance",
                                  .minValue = 0.01f, .maxValue = 10.0f, .defaultValue = 0.70710678f,
                                  .hasFallbackWhenUnconnected = true },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            const auto output = [] (const char* id, const char* label, bool primary = false)
            {
                return perChannel (PortDescriptor { .id = id, .type = SignalType::Signal, .label = label, .isPrimaryOutput = primary, .quantity = Quantity::Audio });
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
            core.setCoefficients (sampleRate, cutoff, resonance);

            const auto [low, band, high] = core.process (inputs[0]);
            outputs[0] = (float) low;
            outputs[1] = (float) band;
            outputs[2] = (float) high;
            outputs[3] = (float) (low + high);
            outputs[4] = (float) (low - high);
        }

    private:
        double sampleRate = 48000.0;
        SvfCore core;
        float storedCutoff = 1000.0f, storedResonance = 0.70710678f;
    };
}
