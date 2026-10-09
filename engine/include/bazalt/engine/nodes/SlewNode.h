#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.slew" (NODE_CATALOG.md's `math.*` row: `in`,
        `rise`/`fall : float·Time·0–10s·log·10ms` — "portamento,
        smoothing, envelope-like shaping").

        An exponential lag, with a separate time constant per direction:
        `rise` applies while the target is above the output, `fall` while it
        is below. Each time is the one-pole time constant (the time to cover
        ~63% of a step), and 0 means "no smoothing that way — jump". Chosen
        over a constant-rate (linear) slew because this node is
        quantity-agnostic (it may smooth Hz, semitones, or a 0..1 level) and
        a time constant is scale-free where "units per second" isn't.

        Sample-rate handling (CLAUDE.md rule 6): the per-sample coefficient
        `exp(-1 / (time * sampleRate))` is derived from the live sample rate
        saved in `prepare()`, never hardcoded, and depends on nothing about
        block size. `rise`/`fall` are modulatable ports, so the coefficient
        is recomputed only when the incoming time actually changes (an
        `exp()` per sample otherwise). The very first sample initialises the
        output to the input instead of gliding up from 0 — a portamento
        should not sweep in from silence when the patch starts.
    */
    class SlewNode : public Node
    {
    public:
        static constexpr int numInputs = 3; // in, rise, fall
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }

        void reset() override
        {
            initialised = false;
            state = 0.0f;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Slew"; }
        juce::String getCategory() const override { return "Math"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { .id = "in", .type = SignalType::Signal },
                ValueTypes::timeSecondsPort ("math.slew.rise", "Rise", 0.01f),
                ValueTypes::timeSecondsPort ("math.slew.fall", "Fall", 0.01f),
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Signal, .isPrimaryOutput = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "math.slew.rise")
                storedRise = value;
            else if (parameterId == "math.slew.fall")
                storedFall = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            auto target = inputs[0];
            if (! std::isfinite (target))
                target = initialised ? state : 0.0f; // hold, don't let NaN/inf enter the filter state

            if (! initialised)
            {
                state = target;
                initialised = true;
                outputs[0] = state;
                return;
            }

            const auto rising = target > state;
            const auto connectedTime = inputs[rising ? 1 : 2];
            const auto storedTime = rising ? storedRise : storedFall;
            const auto time = juce::jmax (0.0f, std::isnan (connectedTime) ? storedTime : connectedTime);

            state += (target - state) * (1.0f - coefficientFor (time, rising ? riseCache : fallCache));
            outputs[0] = state;
        }

    private:
        struct CoefficientCache
        {
            float time = -1.0f;
            float coefficient = 0.0f;
        };

        // The pole for a given time constant, memoised per direction. time
        // <= 0 -> coefficient 0 -> `state += (target - state) * 1`, i.e. jump.
        float coefficientFor (float time, CoefficientCache& cache) noexcept
        {
            if (time != cache.time)
            {
                cache.time = time;
                cache.coefficient = time > 0.0f ? (float) std::exp (-1.0 / ((double) time * sampleRate)) : 0.0f;
            }
            return cache.coefficient;
        }

        double sampleRate = 44100.0;
        float storedRise = 0.01f; // match the ports' own declared defaultValue
        float storedFall = 0.01f;
        float state = 0.0f;
        bool initialised = false;
        CoefficientCache riseCache;
        CoefficientCache fallCache;
    };
}
