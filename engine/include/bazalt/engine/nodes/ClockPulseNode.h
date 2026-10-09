#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "clock.pulse" (wiki/NODES.md's `clock.*` row, the
        Clock+Seq batch, wiki/NODES.Status.md). The root timing source: a
        phase accumulator that fires an Event on every cycle, with optional
        swing/jitter and an internal free-run rate or a tempo-synced one.

        **Swing**, precisely: ticks are numbered k = 0, 1, 2, ... from the
        last reset. Tick k's nominal firing threshold (in whole periods) is
        `k + (k odd ? swing*0.5 : 0)` — the even-numbered tick of each pair
        lands exactly on time, the odd-numbered one is pushed later by up to
        half a period. Because tick k+2's threshold is always the next
        integer regardless of swing, the pair's *average* period is always
        preserved — this falls out of the formula, it isn't a separate
        compensation step.

        **Jitter** perturbs each tick's threshold by a random offset drawn
        fresh the moment the *previous* tick fires (so it's stable for the
        whole upcoming interval, not resampled every sample) — up to
        `jitter * 0.5` periods either direction. `seed` (a structural
        parameter, matching `random.stepped`/`random.drift`'s own
        convention) makes this reproducible — the catalog doesn't name a
        `seed` port for `clock.pulse`, but every other node in this codebase
        with internal randomness gets one, and jitter without one would make
        an otherwise deterministic render non-reproducible for no reason.

        **`rateMode`/`division`**: catalog names both but doesn't pin down
        the exact tempo-sync contract. This node's own design choice: in
        `Free` mode, `rate` is a plain Hz value. In `Division` mode, `rate`
        is read as *beats per second* (wire `io.transport.tempo` straight
        into it) and the effective tick rate is `rate * divisionMultiplier`
        — e.g. `division = 1/4` with `rate` = the transport's own tempo
        ticks exactly once per beat; `1/8` ticks twice per beat; `1/1` ticks
        once per bar (4 beats). This is a real, testable contract, not a
        placeholder.

        **`run`**: a real Boolean input (catalog: `run : bool·true`), same
        `hasFallbackWhenUnconnected` NaN-sentinel pattern as every other
        knob-style port — unconnected reads the stored value (default
        `true`). Same known, already-accepted UI limitation `env.adsr`'s own
        `gate` input has (`wiki/NODES.System.md`'s M21 gotcha note): a
        Boolean port with a fallback renders with no on-canvas toggle until
        the UI gains one: real, wireable, but only settable by a cable or
        `setParameter()` today.

        **`phase`** output is 0..1 progress toward the next tick given
        whatever swing/jitter target is currently in effect — not a strict
        nominal-tempo phase, since swing/jitter can move that target.
    */
    class ClockPulseNode : public Node
    {
    public:
        static constexpr float defaultRateHz = 2.0f;
        static constexpr int numInputs = 5;  // rate, swing, jitter, run, reset
        static constexpr int numOutputs = 2; // tick, phase

        enum class RateMode { Free, Division };
        enum class Division { Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond };

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }

        void reset() override
        {
            basePhase = 0.0;
            nextTickIndex = 0;
            previousThreshold = 0.0;
            random = juce::Random ((juce::int64) seed);
            nextThreshold = thresholdFor (nextTickIndex, storedSwing, storedJitter);
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Clock"; }
        juce::String getCategory() const override { return "Clock"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "clock.pulse.rate", .type = SignalType::Signal, .label = "Rate",
                                  .unit = "Hz", .minValue = 0.01f, .maxValue = 100.0f, .defaultValue = defaultRateHz,
                                  .isLogScale = true, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency,
                                  .curve = Curve::Logarithmic },
                PortDescriptor { .id = "clock.pulse.swing", .type = SignalType::Signal, .label = "Swing",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "clock.pulse.jitter", .type = SignalType::Signal, .label = "Jitter",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "clock.pulse.run", .type = SignalType::Signal, .label = "Run",
                                  .defaultValue = 1.0f, .hasFallbackWhenUnconnected = true, .kind = ValueKind::Bool, .quantity = Quantity::Boolean },
                PortDescriptor { .id = "reset", .type = SignalType::Event, .label = "Reset" },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "tick", .type = SignalType::Event, .label = "Tick", .isPrimaryOutput = true },
                PortDescriptor { .id = "phase", .type = SignalType::Signal, .label = "Phase",
                                  .minValue = 0.0f, .maxValue = 1.0f, .quantity = Quantity::Phase },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "clock.pulse.rateMode",
                                       .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                       .displayName = "Rate Mode", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "free", "Free" }, { "division", "Division" } },
                                       .isStructural = true },
                ParameterDescriptor { .id = "clock.pulse.division",
                                       .minValue = 0.0f, .maxValue = 5.0f, .defaultValue = 2.0f, // "quarter"
                                       .displayName = "Division", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "whole", "1/1" }, { "half", "1/2" }, { "quarter", "1/4" },
                                                         { "eighth", "1/8" }, { "sixteenth", "1/16" },
                                                         { "thirtySecond", "1/32" } },
                                       .isStructural = true },
                ParameterDescriptor { .id = "clock.pulse.seed",
                                       .minValue = 0.0f, .maxValue = 999999.0f, .defaultValue = 1.0f,
                                       .displayName = "Seed", .isInteger = true, .quantity = Quantity::Count,
                                       .step = 1.0f, .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "clock.pulse.rate")
                storedRateHz = juce::jmax (0.01f, value);
            else if (parameterId == "clock.pulse.swing")
                storedSwing = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "clock.pulse.jitter")
                storedJitter = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "clock.pulse.run")
                storedRunning = value > 0.5f;
            else if (parameterId == "clock.pulse.rateMode")
                rateMode = std::lround (value) == 1 ? RateMode::Division : RateMode::Free;
            else if (parameterId == "clock.pulse.division")
                division = (Division) juce::jlimit (0, 5, (int) std::lround (value));
            else if (parameterId == "clock.pulse.seed")
            {
                seed = (int) std::lround (value);
                random = juce::Random ((juce::int64) seed);
            }
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto rate = std::isnan (inputs[0]) ? storedRateHz : juce::jmax (0.01f, inputs[0]);
            const auto swing = std::isnan (inputs[1]) ? storedSwing : juce::jlimit (0.0f, 1.0f, inputs[1]);
            const auto jitter = std::isnan (inputs[2]) ? storedJitter : juce::jlimit (0.0f, 1.0f, inputs[2]);
            const auto running = std::isnan (inputs[3]) ? storedRunning : inputs[3] > 0.5f;
            const auto resetFired = std::fabs (inputs[4]) > 0.0f;

            if (resetFired)
            {
                basePhase = 0.0;
                nextTickIndex = 0;
                previousThreshold = 0.0;
                nextThreshold = thresholdFor (nextTickIndex, swing, jitter);
            }

            auto tickFired = false;

            if (running)
            {
                const auto effectiveHz = rateMode == RateMode::Division
                                              ? (double) rate * divisionMultiplier (division)
                                              : (double) rate;

                if (effectiveHz > 0.0)
                {
                    basePhase += effectiveHz / sampleRate;

                    if (basePhase >= nextThreshold)
                    {
                        tickFired = true;
                        previousThreshold = nextThreshold;
                        ++nextTickIndex;
                        nextThreshold = thresholdFor (nextTickIndex, swing, jitter);
                    }
                }
            }

            outputs[0] = tickFired ? 1.0f : 0.0f;

            const auto denom = juce::jmax (1.0e-9, nextThreshold - previousThreshold);
            outputs[1] = (float) juce::jlimit (0.0, 1.0, (basePhase - previousThreshold) / denom);
        }

    private:
        static float divisionMultiplier (Division d) noexcept
        {
            switch (d)
            {
                case Division::Whole:        return 0.25f;
                case Division::Half:         return 0.5f;
                case Division::Eighth:       return 2.0f;
                case Division::Sixteenth:    return 4.0f;
                case Division::ThirtySecond: return 8.0f;
                case Division::Quarter:
                default:                     return 1.0f;
            }
        }

        /** The firing threshold (in whole periods since the last reset) for
            tick index k, including swing's per-pair-odd-tick delay and a
            freshly-drawn jitter offset. Called once per tick, not per
            sample — jitter is deliberately stable for the whole upcoming
            interval.
        */
        double thresholdFor (int k, float swing, float jitter) noexcept
        {
            const auto base = (double) k + ((k % 2 == 1) ? (double) swing * 0.5 : 0.0);
            const auto jitterOffset = jitter > 0.0f
                                           ? (double) ((random.nextFloat() * 2.0f - 1.0f) * jitter * 0.5f)
                                           : 0.0;
            return base + jitterOffset;
        }

        double sampleRate = 44100.0;
        double basePhase = 0.0;
        double previousThreshold = 0.0;
        double nextThreshold = 0.0;
        int nextTickIndex = 0;
        float storedRateHz = defaultRateHz;
        float storedSwing = 0.0f;
        float storedJitter = 0.0f;
        bool storedRunning = true;
        RateMode rateMode = RateMode::Free;
        Division division = Division::Quarter;
        int seed = 1;
        juce::Random random { (juce::int64) 1 };
    };
}
