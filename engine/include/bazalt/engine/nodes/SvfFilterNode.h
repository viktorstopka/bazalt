#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/SvfFilter.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "filter.svf". One Audio input, one Audio output.
        Type/cutoff/resonance are node parameters (not modulation inputs
        yet — a Control-rate cutoff input is a natural extension once a
        graph needs it, not required by either M2 proof graph).
    */
    class SvfFilterNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
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
            return { { "in", SignalType::Audio } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { { "filter.svf.cutoff", 20.0f, 20000.0f, 1000.0f, 0.3f, "Hz", "Cutoff" },
                     { "filter.svf.resonance", 0.01f, 10.0f, 0.70710678f, 0.5f, "", "Resonance" } };
        }

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
            outputs[0] = filter.processSample (0, inputs[0]);
        }

    private:
        SvfFilter filter;
    };
}
