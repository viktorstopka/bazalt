#pragma once

#include "bazalt/engine/nodes/InheritingPortsNode.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.blend" — Blend (wiki/plans/DataAndWavetable.md
        D8). `out = A` at Amount 0, `B` at Amount 1, in between a blend — the
        old Crossfade (`mix.crossfade`) and Select (`logic.select`, which was
        a Blend with a boolean Amount) as one node. Works for any value: two
        sounds, two modulations, two frequencies. A, B and Out share one type
        and quantity, taken from what is wired (A outranks B); stereo runs per
        channel.

        Amount glides (**Glide**, 5 ms by default) so a jump — a Boolean
        flipping, a stepped value — never clicks. Glide 0 switches on the
        sample: a valve, a sample-accurate select (the old Select migrates to
        it — a 4 ms gate through a 5 ms glide never fully closed). The law decides how the two gains follow Amount:
        - linear: `A*(1-t) + B*t` — unity gain, right for correlated material
          and for any value that is not a sound.
        - equal power: `A*cos(t·π/2) + B*sin(t·π/2)` — constant power for two
          different sounds, at the cost of a ~3 dB bump for identical ones.
    */
    class BlendNode : public InheritingPortsNode
    {
    public:
        static constexpr float smoothingSeconds = 0.005f;
        static constexpr float defaultAmount = 0.5f;

        BlendNode() noexcept : InheritingPortsNode (Quantity::Audio) {}

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = (float) info.sampleRate;
            glideCache = -1.0f;
            reset();
        }

        void reset() override { smoothedAmount = -1.0f; }

        int getNumInputPorts() const noexcept override { return 4; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return "Blend"; }
        juce::String getCategory() const override { return "Math"; }

        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            if (toPortId == "a")
                offer (0, source, true);
            else if (toPortId == "b")
                offer (1, source, true);
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            auto valuePort = [this] (const char* id, const char* label)
            {
                return PortDescriptor { .id = id, .type = resolvedType, .label = label, .quantity = resolvedQuantity,
                                        .channels = Channels::Inherited, .polymorphism = PortPolymorphism::SignalAndQuantity };
            };
            return {
                valuePort ("a", "A"),
                valuePort ("b", "B"),
                PortDescriptor { .id = "math.blend.amount", .type = SignalType::Signal, .label = "Amount",
                                 .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultAmount,
                                 .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar,
                                 .polarity = Polarity::Unipolar },
                PortDescriptor { .id = "math.blend.glide", .type = SignalType::Signal, .label = "Glide", .unit = "s",
                                 .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = smoothingSeconds,
                                 .hasFallbackWhenUnconnected = true, .quantity = Quantity::Time },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = resolvedType, .label = "Out", .isPrimaryOutput = true,
                                      .quantity = resolvedQuantity, .channels = Channels::Inherited,
                                      .polymorphism = PortPolymorphism::SignalAndQuantity } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "math.blend.law", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                           .displayName = "Law", .isInteger = true, .kind = ValueKind::Enum,
                                           .enumOptions = { { "linear", "Linear" }, { "equalPower", "Equal Power" } } } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "math.blend.law")
                equalPower = std::lround (value) == 1;
            else if (parameterId == "math.blend.amount")
                storedAmount = value;
            else if (parameterId == "math.blend.glide")
                storedGlide = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto raw = std::isnan (inputs[2]) ? storedAmount : inputs[2];
            const auto target = std::isfinite (raw) ? std::clamp (raw, 0.0f, 1.0f) : defaultAmount;

            const auto glide = juce::jmax (0.0f, std::isnan (inputs[3]) ? storedGlide : inputs[3]);
            if (glide != glideCache)
            {
                glideCache = glide;
                smoothingCoeff = glide > 0.0f ? std::exp (-1.0f / (glide * sampleRate)) : 0.0f;
            }

            // The first sample starts where it is asked to, never glides in from 0.
            smoothedAmount = smoothedAmount < 0.0f ? target : target + smoothingCoeff * (smoothedAmount - target);
            const auto t = smoothedAmount;

            float gainA, gainB;
            if (equalPower)
            {
                constexpr auto halfPi = 1.57079632679489661923f;
                gainA = std::cos (t * halfPi);
                gainB = std::sin (t * halfPi);
            }
            else
            {
                gainA = 1.0f - t;
                gainB = t;
            }
            outputs[0] = inputs[0] * gainA + inputs[1] * gainB;
        }

    private:
        bool equalPower = false;
        float storedAmount = defaultAmount;
        float storedGlide = smoothingSeconds;
        float sampleRate = 48000.0f, glideCache = -1.0f;
        float smoothingCoeff = 0.0f;
        float smoothedAmount = -1.0f;
    };
}
