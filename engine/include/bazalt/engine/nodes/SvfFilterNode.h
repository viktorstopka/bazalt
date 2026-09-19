#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/SvfFilter.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "filter.svf". One Audio input, one Audio output.
        `type` (lowpass/bandpass/highpass) stays host-only (setType(), no
        descriptor at all yet — not asked for, and it's a discrete
        algorithmic switch like osc.analog's shape, not a continuous
        value). Cutoff/resonance were plain parameters until M20 (direct
        feedback: "there is no reason why ... Oscillator Frequency
        wouldn't be modulatable" — the same reasoning applies here);
        they're real Control-type input ports now, same dotted ids the old
        parameters used, same `hasFallbackWhenUnconnected` NaN-sentinel
        pattern as everywhere else. `juce::dsp::StateVariableTPTFilter`
        (SvfFilter.h) is a topology-preserving-transform filter
        specifically chosen for staying stable under audio-rate
        cutoff/resonance modulation (ARCHITECTURE.md §5) — calling
        setCutoffFrequency()/setResonance() every sample while modulated is
        exactly the case it's designed for, not a new risk.
    */
    class SvfFilterNode : public Node
    {
    public:
        static constexpr int numInputs = 3; // in, cutoff, resonance
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override
        {
            filter.prepare (info.sampleRate, (uint32_t) info.maxBlockSize, 1);
        }

        void reset() override { filter.reset(); }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "SVF Filter"; }
        juce::String getCategory() const override { return "Filters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { "in", SignalType::Audio },
                ValueTypes::frequencyPort ("filter.svf.cutoff", "Cutoff", 1000.0f),
                PortDescriptor { .id = "filter.svf.resonance", .type = SignalType::Control, .label = "Resonance",
                                  .minValue = 0.01f, .maxValue = 10.0f, .defaultValue = 0.70710678f,
                                  .hasFallbackWhenUnconnected = true },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        std::vector<ParameterDescriptor> getParameters() const override { return {}; }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "filter.svf.cutoff")
                filter.setCutoffFrequency (value);
            else if (parameterId == "filter.svf.resonance")
                filter.setResonance (value);
        }

        void setType (SvfFilterType newType) noexcept { filter.setType (newType); }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            if (! std::isnan (inputs[1]))
                filter.setCutoffFrequency (inputs[1]);
            if (! std::isnan (inputs[2]))
                filter.setResonance (inputs[2]);

            outputs[0] = filter.processSample (0, inputs[0]);
        }

    private:
        SvfFilter filter;
    };
}
