#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "mix.crossfade" (NODE_CATALOG.md's `mix.*` row: `a`,
        `b` Audio; `position : float·Unipolar·0–1·linear·0.5`; structural
        `law` linear / equal power). `position` 0 is all `a`, 1 is all `b`,
        and it's a live-modulatable port (an LFO or envelope sweeping one
        source into the other).

        The two laws differ only in how the gains follow `position`:
        - linear: `a*(1-p) + b*p` — sums to unity gain, the right choice for
          correlated material (two copies of the same signal), but dips ~3dB
          at the midpoint for uncorrelated sources.
        - equal power: `a*cos(p*pi/2) + b*sin(p*pi/2)` — constant power for
          uncorrelated sources (two different sounds), at the cost of a ~3dB
          bump if they're actually the same signal.

        The gain pair is cached and only recomputed when `position` or the
        law changes — `position` is very often a constant, and a
        `cos()`+`sin()` per sample would be pure waste then.
    */
    class CrossfadeNode : public Node
    {
    public:
        static constexpr int numInputs = 3; // a, b, position
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Crossfade"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ "a", SignalType::Audio }),
                perChannel ({ "b", SignalType::Audio }),
                PortDescriptor { .id = "mix.crossfade.position", .type = SignalType::Control, .label = "Position",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { perChannel (PortDescriptor { .id = "out", .type = SignalType::Audio, .isPrimaryOutput = true }) };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "mix.crossfade.law",
                                            .minValue = 0.0f,
                                            .maxValue = 1.0f,
                                            .defaultValue = 0.0f,
                                            .displayName = "Law",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "linear", "Linear" }, { "equalPower", "Equal Power" } },
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "mix.crossfade.law")
            {
                const auto newLaw = (int) std::round (value) == 1 ? Law::EqualPower : Law::Linear;
                if (newLaw != law)
                {
                    law = newLaw;
                    cachedPosition = -1.0f; // invalidate the cached gains
                }
            }
            else if (parameterId == "mix.crossfade.position")
            {
                storedPosition = value;
            }
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto rawPosition = std::isnan (inputs[2]) ? storedPosition : inputs[2];
            const auto position = std::isfinite (rawPosition) ? std::clamp (rawPosition, 0.0f, 1.0f) : 0.5f;

            if (position != cachedPosition)
            {
                cachedPosition = position;

                if (law == Law::EqualPower)
                {
                    constexpr auto halfPi = 1.57079632679489661923f;
                    gainA = std::cos (position * halfPi);
                    gainB = std::sin (position * halfPi);
                }
                else
                {
                    gainA = 1.0f - position;
                    gainB = position;
                }
            }

            outputs[0] = inputs[0] * gainA + inputs[1] * gainB;
        }

    private:
        enum class Law
        {
            Linear,
            EqualPower
        };

        Law law = Law::Linear; // matches enumOptions' declaration order (linear=0, equalPower=1)
        float storedPosition = 0.5f; // matches the port's own declared defaultValue
        float cachedPosition = -1.0f; // never a valid position, so the first sample always computes
        float gainA = 0.5f;
        float gainB = 0.5f;
    };
}
