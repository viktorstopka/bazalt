#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "env.follower" (NODE_CATALOG.md's `env.*` row: "the
        Audio -> Control adapter, and the way an external signal drives
        anything"). Asymmetric one-pole ballistics — the same
        attack/release-coefficient math
        `engine/include/bazalt/engine/telemetry/MeterBallistics.h` already
        applies once per analysis-thread drain for the meter preview,
        mirrored here as real per-sample audio-rate DSP (not literally
        reused — that class is telemetry-side and drain-interval-driven,
        this is audio-thread and sample-rate-driven) rather than a new
        algorithm invented from scratch.

        `detection` (structural: Peak/RMS) — **Peak** tracks `|in|` directly
        with the attack/release ballistics below. **RMS** smooths `in^2`
        with the same ballistics, then takes the square root at the output —
        an attack/release-shaped mean-square estimate, not a fixed-window
        RMS; genuine windowed RMS would need a delay line this node doesn't
        have and the catalog doesn't ask for one. Peak is the default — the
        more common "audio in, useful control signal out" case, and what a
        general-purpose patch cable expects first.

        Coefficients (CLAUDE.md rule 6): `exp(-1/(time*sampleRate))`,
        `sampleRate` cached in `prepare()`, memoised per direction exactly
        like `SlewNode.h`'s `CoefficientCache` — recomputed only when the
        live attack/release time actually changed since the last sample.
    */
    class EnvelopeFollowerNode : public Node
    {
    public:
        static constexpr float defaultAttackMs = 5.0f;
        static constexpr float defaultReleaseMs = 100.0f;
        static constexpr int numInputs = 3; // in, attack, release
        static constexpr int numOutputs = 1;

        enum class Detection { Peak, Rms };

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }

        void reset() override { state = 0.0f; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Envelope Follower"; }
        juce::String getCategory() const override { return "Envelopes"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { "in", SignalType::Audio },
                ValueTypes::timeMsPort ("env.follower.attack", "Attack", defaultAttackMs, 500.0f),
                ValueTypes::timeMsPort ("env.follower.release", "Release", defaultReleaseMs),
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true,
                                       .minValue = 0.0f, .maxValue = 1.0f, .quantity = Quantity::Unipolar } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "env.follower.detection",
                                            .minValue = 0.0f,
                                            .maxValue = 1.0f,
                                            .defaultValue = 0.0f,
                                            .displayName = "Detection",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "peak", "Peak" }, { "rms", "RMS" } },
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "env.follower.attack")
                storedAttackMs = juce::jmax (0.0f, value);
            else if (parameterId == "env.follower.release")
                storedReleaseMs = juce::jmax (0.0f, value);
            else if (parameterId == "env.follower.detection")
                detection = std::lround (value) == 1 ? Detection::Rms : Detection::Peak;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto attackMs = std::isnan (inputs[1]) ? storedAttackMs : juce::jmax (0.0f, inputs[1]);
            const auto releaseMs = std::isnan (inputs[2]) ? storedReleaseMs : juce::jmax (0.0f, inputs[2]);

            const auto rectified = detection == Detection::Peak ? std::fabs (inputs[0]) : inputs[0] * inputs[0];
            const auto rising = rectified > state;
            const auto timeSeconds = (rising ? attackMs : releaseMs) * 0.001f;

            state += (rectified - state) * (1.0f - coefficientFor (timeSeconds, rising ? attackCache : releaseCache));
            outputs[0] = detection == Detection::Peak ? state : std::sqrt (std::max (0.0f, state));
        }

    private:
        struct CoefficientCache
        {
            float time = -1.0f;
            float coefficient = 0.0f;
        };

        float coefficientFor (float timeSeconds, CoefficientCache& cache) noexcept
        {
            if (timeSeconds != cache.time)
            {
                cache.time = timeSeconds;
                cache.coefficient = timeSeconds > 0.0f ? (float) std::exp (-1.0 / ((double) timeSeconds * sampleRate)) : 0.0f;
            }
            return cache.coefficient;
        }

        double sampleRate = 44100.0;
        float storedAttackMs = defaultAttackMs;
        float storedReleaseMs = defaultReleaseMs;
        Detection detection = Detection::Peak;
        float state = 0.0f;
        CoefficientCache attackCache;
        CoefficientCache releaseCache;
    };
}
