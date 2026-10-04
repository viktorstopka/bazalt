#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "shape.clip" (wiki/NODES.md's `shape.*` row). "The
        node you put in a feedback loop so a slider can't destroy a
        speaker" — the catalog's own framing, and a real, wireable
        complement to the plugin's own always-on master-output safety net
        (`bazalt::engine::OutputLimiter`, `PluginProcessor.cpp`): that one
        protects the final mix unconditionally, but has no concept of
        "inside a patch" — it can't be inserted mid-chain to tame a
        resonant feedback loop (`resonator.comb`/`modal`/`string`/`plate`)
        BEFORE it reaches another node, which is exactly where this one
        belongs instead.

        **A concrete, tested contract this session had to design** (the
        catalog names `ceiling`/`knee`/the three `mode`s, not their exact
        curves):
        - **`hard`**: an exact clamp to `±ceiling`, with `knee` (0..1,
          scaled by `ceiling`) giving a real quadratic soft-knee region
          immediately below the ceiling — `knee = 0` is a literal instant
          clamp; `knee > 0` smoothly pulls the signal down toward the
          ceiling over a small region below it, rather than a single sharp
          corner, the same quadratic-knee idea compressors use around their
          own threshold.
        - **`soft`**: a `tanh`-based saturation that asymptotically
          approaches `±ceiling` and never hard-clips at all — `knee`
          controls how early the curve starts bending (`0` = stays linear
          right up to `ceiling` before bending; `1` = the whole range is
          shaped by the curve, bending from zero).
        - **`limiter`**: the SAME attack (~1ms) / release (~100ms) smoothed
          gain-reduction envelope `OutputLimiter.h` uses for the master
          output, exposed here as real per-instance node state — `knee`
          reuses `hard` mode's own quadratic region to compute the
          INSTANTANEOUS target gain each sample, which the envelope then
          smooths over time (so "where correction begins" means the same
          thing in `hard` and `limiter` modes; only the TIME behavior
          differs between them).

        **`clipping`** is `true` whenever this sample's processing is
        actually altering the signal (above the knee's own start point in
        `hard`/`soft`, or whenever `limiter` mode's gain reduction is
        measurably below unity) — a real, live indicator you can wire into
        `view.glance`/`logic.*` to see or react to when it's doing anything
        at all, not a static always-on/off flag.
    */
    class ShapeClipNode : public Node
    {
    public:
        static constexpr float minCeiling = 0.05f;
        static constexpr float maxCeiling = 2.0f;
        static constexpr float attackSeconds = 0.001f;
        static constexpr float releaseSeconds = 0.100f;
        static constexpr int numInputs = 3;  // in, ceiling, knee
        static constexpr int numOutputs = 2; // out, clipping

        enum class Mode { Hard, Soft, Limiter };

        void prepare (const NodePrepareInfo& info) override
        {
            attackCoeff = std::exp (-1.0f / (attackSeconds * (float) info.sampleRate));
            releaseCoeff = std::exp (-1.0f / (releaseSeconds * (float) info.sampleRate));
            gainReduction = 1.0f;
        }

        void reset() override { gainReduction = 1.0f; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Clip"; }
        juce::String getCategory() const override { return "Shape"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ "in", SignalType::Audio }),
                PortDescriptor { .id = "shape.clip.ceiling", .type = SignalType::Control, .label = "Ceiling",
                                  .minValue = minCeiling, .maxValue = maxCeiling, .defaultValue = 1.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Dimensionless },
                PortDescriptor { .id = "shape.clip.knee", .type = SignalType::Control, .label = "Knee",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.1f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar,
                                  .polarity = Polarity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                perChannel (PortDescriptor { .id = "out", .type = SignalType::Audio, .label = "Out", .isPrimaryOutput = true }),
                PortDescriptor { .id = "clipping", .type = SignalType::Boolean, .label = "Clipping" },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "shape.clip.mode",
                                       .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 0.0f, // hard
                                       .displayName = "Mode", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "hard", "Hard" }, { "soft", "Soft" }, { "limiter", "Limiter" } },
                                       .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "shape.clip.ceiling")
                storedCeiling = juce::jlimit (minCeiling, maxCeiling, value);
            else if (parameterId == "shape.clip.knee")
                storedKnee = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "shape.clip.mode")
                mode = (Mode) juce::jlimit (0, 2, (int) std::lround (value));
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto ceiling = std::isnan (inputs[1]) ? storedCeiling : juce::jlimit (minCeiling, maxCeiling, inputs[1]);
            const auto knee = std::isnan (inputs[2]) ? storedKnee : juce::jlimit (0.0f, 1.0f, inputs[2]);
            const auto x = inputs[0];
            const auto mag = std::fabs (x);
            const auto sign = x < 0.0f ? -1.0f : 1.0f;

            if (mode == Mode::Soft)
            {
                const auto kneeStart = ceiling * (1.0f - knee);
                if (mag <= kneeStart)
                {
                    outputs[0] = x;
                    outputs[1] = 0.0f;
                }
                else
                {
                    const auto range = juce::jmax (0.0001f, ceiling - kneeStart);
                    const auto excess = mag - kneeStart;
                    const auto saturated = kneeStart + range * std::tanh (excess / range);
                    outputs[0] = sign * saturated;
                    outputs[1] = 1.0f;
                }
                return;
            }

            // Hard and Limiter both start from the same quadratic-knee
            // instantaneous correction - Limiter just smooths the RESULT
            // over time instead of applying it immediately.
            const auto correctedMag = quadraticKneeClip (mag, ceiling, knee * ceiling);

            if (mode == Mode::Hard)
            {
                outputs[0] = sign * correctedMag;
                outputs[1] = correctedMag < mag ? 1.0f : 0.0f;
                return;
            }

            // Limiter: the instantaneous target gain this sample would need
            // to reach correctedMag, smoothed by the same attack/release
            // envelope OutputLimiter.h uses for the master output. Same
            // defense-in-depth as that class: the smoothed envelope alone
            // can't fully react to a single isolated spike within the ~1ms
            // attack time, so a hard clamp to ±ceiling underneath it is a
            // real, deliberate backstop, not a redundant belt-and-braces
            // gesture - this node's whole purpose is safety, so an
            // unclamped output here (even if only momentarily, for one
            // extreme sample) would be a real gap, not a cosmetic one.
            const auto targetGain = mag > 0.0f ? correctedMag / mag : 1.0f;
            const auto coeff = targetGain < gainReduction ? attackCoeff : releaseCoeff;
            gainReduction = targetGain + coeff * (gainReduction - targetGain);

            const auto limited = x * gainReduction;
            outputs[0] = juce::jlimit (-ceiling, ceiling, limited);
            outputs[1] = gainReduction < 0.999f ? 1.0f : 0.0f;
        }

    private:
        // A real quadratic soft-knee, the same idea compressors use around
        // their own threshold: unchanged below (ceiling - halfKnee), an
        // exact clamp at/above (ceiling + halfKnee), and a smooth quadratic
        // pull-down across the knee region between them. halfKnee == 0
        // collapses to an exact instant clamp at `ceiling`.
        static float quadraticKneeClip (float mag, float ceiling, float kneeWidth) noexcept
        {
            const auto halfKnee = kneeWidth * 0.5f;
            const auto lowerBound = ceiling - halfKnee;
            const auto upperBound = ceiling + halfKnee;

            if (mag <= lowerBound)
                return mag;
            if (halfKnee <= 0.0f || mag >= upperBound)
                return ceiling;

            // A real bug this session's own tests caught: `mag - t*t*(mag -
            // ceiling)` looks like it should pull the signal down toward
            // `ceiling`, but `(mag - ceiling)` is NEGATIVE for any `mag`
            // between `lowerBound` and `ceiling` (the common case for a
            // signal just inside the knee) — subtracting a negative number
            // INCREASES the output past its own input, the opposite of what
            // a safety knee must do. The correct construction interpolates
            // the SLOPE instead: 1:1 (unchanged) at `lowerBound`, flattening
            // smoothly to land exactly on `ceiling` at `upperBound` — always
            // between `lowerBound` and `ceiling`, never above the input's
            // own value, by construction.
            const auto t = (mag - lowerBound) / (upperBound - lowerBound); // 0..1 across the knee
            const auto pulledDown = lowerBound + (mag - lowerBound) * (1.0f - t * 0.5f);
            return juce::jmin (pulledDown, ceiling);
        }

        float storedCeiling = 1.0f;
        float storedKnee = 0.1f;
        Mode mode = Mode::Hard;
        float attackCoeff = 0.0f, releaseCoeff = 0.0f;
        float gainReduction = 1.0f;
    };
}
