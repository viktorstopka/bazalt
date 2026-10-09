#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>
#include <cstdint>

namespace bazalt::engine::nodes
{
    /** Stable type id: "noise.colored" (wiki/plans/SoundPalette.md, Batch 1).
        White, pink, brown, blue or violet noise — the spectral slope is the
        colour: 0, -3, -6, +3, +6 dB per octave. Each colour is normalised to
        roughly the same loudness, so switching colour changes the tone, not
        the level.

        - white: a xorshift generator, uniform in [-1, 1).
        - pink: Paul Kellet's refined filter (seven one-poles summed), whose
          slope holds within ±0.5 dB from 9 Hz to Nyquist at 44.1-48 kHz.
        - brown: white integrated through a leaky integrator (corner ~5 Hz,
          derived from the sample rate in prepare() — CLAUDE.md rule 6).
        - blue / violet: the first difference of pink / white (+6 dB/oct on
          top of the parent slope).

        `stereo` (structural) adds a second, independently seeded generator,
        so the output becomes a decorrelated stereo pair — what a generator
        must declare itself, since it has no input to inherit a width from
        (wiki/plans/StereoChannels.md §3). `seed` makes the stream
        reproducible.
    */
    class NoiseColoredNode : public Node
    {
    public:
        enum class Colour { White, Pink, Brown, Blue, Violet };

        void prepare (const NodePrepareInfo& info) override
        {
            brownLeak = (float) std::exp (-juce::MathConstants<double>::twoPi * 5.0 / info.sampleRate);
            reset();
        }

        void reset() override
        {
            for (int c = 0; c < 2; ++c)
            {
                generators[c] = Generator {};
                generators[c].state = seedFor (c);
            }
        }

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 1; }
        int getNumOutputChannels() const noexcept override { return stereo ? 2 : 1; }

        juce::String getTitle() const override { return "Noise"; }
        juce::String getCategory() const override { return "Generators"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "noise.colored.level", .type = SignalType::Signal, .label = "Level",
                                      .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f,
                                      .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true,
                                      .quantity = Quantity::Audio, .channels = stereo ? Channels::Stereo : Channels::Mono } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "noise.colored.colour", .minValue = 0.0f, .maxValue = 4.0f, .defaultValue = 1.0f,
                                       .displayName = "Colour", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "white", "White" }, { "pink", "Pink" }, { "brown", "Brown" },
                                                         { "blue", "Blue" }, { "violet", "Violet" } } },
                ParameterDescriptor { .id = "noise.colored.stereo", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                       .displayName = "Stereo", .isInteger = true, .kind = ValueKind::Bool, .isStructural = true },
                ParameterDescriptor { .id = "noise.colored.seed", .minValue = 0.0f, .maxValue = 9999.0f, .defaultValue = 0.0f,
                                       .displayName = "Seed", .isInteger = true, .kind = ValueKind::Int, .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "noise.colored.colour")
                colour = (Colour) juce::jlimit (0, 4, (int) std::lround (value));
            else if (parameterId == "noise.colored.stereo")
                stereo = value > 0.5f;
            else if (parameterId == "noise.colored.seed")
            {
                seed = (uint32_t) juce::jmax (0, (int) std::lround (value));
                reset();
            }
            else if (parameterId == "noise.colored.level")
                storedLevel = juce::jlimit (0.0f, 1.0f, value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto level = std::isnan (inputs[0]) ? storedLevel : juce::jlimit (0.0f, 1.0f, inputs[0]);
            outputs[0] = generators[0].next (colour, brownLeak) * level;
            if (stereo)
                outputs[1] = generators[1].next (colour, brownLeak) * level;
        }

    private:
        struct Generator
        {
            uint32_t state = 1;
            float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
            float brown = 0, previous = 0;

            float white() noexcept
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                return (float) (int32_t) state * (1.0f / 2147483648.0f);
            }

            float pink (float w) noexcept
            {
                b0 = 0.99886f * b0 + w * 0.0555179f;
                b1 = 0.99332f * b1 + w * 0.0750759f;
                b2 = 0.96900f * b2 + w * 0.1538520f;
                b3 = 0.86650f * b3 + w * 0.3104856f;
                b4 = 0.55000f * b4 + w * 0.5329522f;
                b5 = -0.7616f * b5 - w * 0.0168980f;
                const auto p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
                b6 = w * 0.115926f;
                return p * 0.168f;
            }

            float next (Colour colour, float brownLeak) noexcept
            {
                const auto w = white();
                switch (colour)
                {
                    case Colour::White: return w * 0.6f;
                    case Colour::Pink:  return pink (w);
                    case Colour::Brown:
                        brown = brown * brownLeak + w * 0.0177f;
                        return brown;
                    case Colour::Blue:
                    {
                        const auto p = pink (w);
                        const auto d = (p - previous) * 1.6f;
                        previous = p;
                        return d;
                    }
                    case Colour::Violet:
                    {
                        const auto d = (w - previous) * 0.42f;
                        previous = w;
                        return d;
                    }
                }
                return 0.0f;
            }
        };

        uint32_t seedFor (int channel) const noexcept
        {
            // Never zero (xorshift's one fixed point); channels well apart.
            const auto mixed = (seed + 1u) * 2654435761u ^ (uint32_t) (channel + 1) * 0x9e3779b9u;
            return mixed == 0 ? 1u : mixed;
        }

        Colour colour = Colour::Pink;
        bool stereo = false;
        uint32_t seed = 0;
        float storedLevel = 0.5f;
        float brownLeak = 0.999f;
        Generator generators[2];
    };
}
