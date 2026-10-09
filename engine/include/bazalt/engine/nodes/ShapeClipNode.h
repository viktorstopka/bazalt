#pragma once

#include "bazalt/engine/nodes/InheritingPortsNode.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "shape.clip". Keeps a signal between **Low** and
        **High** — the one bounding node: the old Clamp (`math.clamp`, a plain
        range limit for values) and the old Clip (`±ceiling` safety for audio)
        merged (wiki/plans/DataAndWavetable.md §2). Hard mode with Knee 0 is
        exactly the old Clamp; Low = -c, High = c is exactly the old Clip.

        "The node you put in a feedback loop so a slider can't destroy a
        speaker" — a wireable, mid-chain complement to the plugin's always-on
        master safety net (`OutputLimiter`), and equally a plain range limit
        for any value. `in` takes on the type and quantity of what feeds it
        (an audio signal, a frequency, a modulation), and Low/High share that
        quantity. Stereo runs per channel (Inherited).

        The range is handled around its centre: with c = (Low+High)/2 and
        r = (High-Low)/2, each mode bounds the deviation x - c to ±r:
        - **hard**: an exact clamp, with `knee` (0..1 of r) giving a quadratic
          soft-knee just inside the bounds — knee 0 is an instant clamp.
        - **soft**: tanh saturation that approaches the bounds and never
          reaches them; `knee` sets how early it starts bending.
        - **limiter**: hard mode's correction as a target gain, smoothed with
          the master limiter's attack (~1 ms) / release (~100 ms), with a hard
          clamp underneath as a backstop for a single isolated spike.

        **clipping** is true while this sample is actually being altered.
    */
    class ShapeClipNode : public InheritingPortsNode
    {
    public:
        static constexpr float attackSeconds = 0.001f;
        static constexpr float releaseSeconds = 0.100f;
        static constexpr float defaultLow = -1.0f;
        static constexpr float defaultHigh = 1.0f;
        static constexpr float defaultKnee = 0.1f;
        static constexpr int numInputs = 4;  // in, low, high, knee
        static constexpr int numOutputs = 2; // out, clipping

        enum class Mode { Hard, Soft, Limiter };

        ShapeClipNode() noexcept : InheritingPortsNode (SignalType::Audio) {}

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

        // `in` is what is being bounded, so it wins over the range ports.
        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            if (toPortId == "in")
                offer (0, source, true);
            else if (toPortId == "shape.clip.low")
                offer (1, source, false);
            else if (toPortId == "shape.clip.high")
                offer (2, source, false);
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            auto rangePort = [this] (const char* id, const char* label, float defaultValue)
            {
                return PortDescriptor { .id = id, .type = SignalType::Control, .label = label,
                                        .defaultValue = defaultValue, .hasFallbackWhenUnconnected = true,
                                        .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::Quantity };
            };
            return {
                PortDescriptor { .id = "in", .type = resolvedType, .label = "In", .quantity = resolvedQuantity,
                                 .channels = Channels::Inherited, .polymorphism = PortPolymorphism::SignalAndQuantity },
                rangePort ("shape.clip.low", "Low", defaultLow),
                rangePort ("shape.clip.high", "High", defaultHigh),
                PortDescriptor { .id = "shape.clip.knee", .type = SignalType::Control, .label = "Knee",
                                 .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultKnee,
                                 .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar,
                                 .polarity = Polarity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = resolvedType, .label = "Out", .isPrimaryOutput = true,
                                 .quantity = resolvedQuantity, .channels = Channels::Inherited,
                                 .polymorphism = PortPolymorphism::SignalAndQuantity },
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
            if (parameterId == "shape.clip.low")
                storedLow = value;
            else if (parameterId == "shape.clip.high")
                storedHigh = value;
            else if (parameterId == "shape.clip.knee")
                storedKnee = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "shape.clip.mode")
                mode = (Mode) juce::jlimit (0, 2, (int) std::lround (value));
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto lowIn = std::isnan (inputs[1]) ? storedLow : inputs[1];
            const auto highIn = std::isnan (inputs[2]) ? storedHigh : inputs[2];
            const auto knee = std::isnan (inputs[3]) ? storedKnee : juce::jlimit (0.0f, 1.0f, inputs[3]);
            const auto low = std::min (lowIn, highIn);
            const auto high = std::max (lowIn, highIn);
            const auto centre = 0.5f * (low + high);
            const auto radius = 0.5f * (high - low);

            const auto d = inputs[0] - centre;
            const auto mag = std::fabs (d);
            const auto sign = d < 0.0f ? -1.0f : 1.0f;

            if (radius <= 0.0f)
            {
                outputs[0] = centre;
                outputs[1] = mag > 0.0f ? 1.0f : 0.0f;
                return;
            }

            if (mode == Mode::Soft)
            {
                const auto kneeStart = radius * (1.0f - knee);
                if (mag <= kneeStart)
                {
                    outputs[0] = inputs[0];
                    outputs[1] = 0.0f;
                }
                else
                {
                    const auto range = juce::jmax (0.0001f, radius - kneeStart);
                    const auto saturated = kneeStart + range * std::tanh ((mag - kneeStart) / range);
                    outputs[0] = centre + sign * saturated;
                    outputs[1] = 1.0f;
                }
                return;
            }

            // Hard and Limiter share the quadratic-knee correction; Limiter
            // smooths the resulting gain over time instead of applying it.
            const auto correctedMag = quadraticKneeClip (mag, radius, knee * radius);
            if (mode == Mode::Hard)
            {
                outputs[0] = centre + sign * correctedMag;
                outputs[1] = correctedMag < mag ? 1.0f : 0.0f;
                return;
            }

            const auto targetGain = mag > 0.0f ? correctedMag / mag : 1.0f;
            const auto coeff = targetGain < gainReduction ? attackCoeff : releaseCoeff;
            gainReduction = targetGain + coeff * (gainReduction - targetGain);
            outputs[0] = centre + juce::jlimit (-radius, radius, d * gainReduction);
            outputs[1] = gainReduction < 0.999f ? 1.0f : 0.0f;
        }

    private:
        // A quadratic soft-knee: unchanged below (ceiling - halfKnee), an
        // exact clamp from (ceiling + halfKnee) up, and between them a slope
        // that flattens from 1:1 to land exactly on the ceiling — never above
        // the input, never above the ceiling. halfKnee 0 is an instant clamp.
        static float quadraticKneeClip (float mag, float ceiling, float kneeWidth) noexcept
        {
            const auto halfKnee = kneeWidth * 0.5f;
            const auto lowerBound = ceiling - halfKnee;
            const auto upperBound = ceiling + halfKnee;
            if (mag <= lowerBound)
                return mag;
            if (halfKnee <= 0.0f || mag >= upperBound)
                return ceiling;
            const auto t = (mag - lowerBound) / (upperBound - lowerBound);
            const auto pulledDown = lowerBound + (mag - lowerBound) * (1.0f - t * 0.5f);
            return juce::jmin (pulledDown, ceiling);
        }

        float storedLow = defaultLow;
        float storedHigh = defaultHigh;
        float storedKnee = defaultKnee;
        Mode mode = Mode::Hard;
        float attackCoeff = 0.0f, releaseCoeff = 0.0f;
        float gainReduction = 1.0f;
    };
}
