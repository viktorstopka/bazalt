#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>
#include <cstdint>

namespace bazalt::engine::nodes
{
    /** Stable type id: "noise.dust" (wiki/plans/SoundPalette.md, Batch 2).
        Sparse single-sample impulses at random moments — rain, crackle,
        Geiger clicks, the trigger for a swarm of short sounds. `density` is
        the average number of impulses per second (a Poisson process: each
        sample fires with probability density/sampleRate, so timing is
        sample-accurate and independent of block size). `randomness` mixes
        each impulse's height from fixed (0) to fully random (1); bipolar
        polarity also randomises the sign. `trigger` fires an Event on every
        impulse, to drive other nodes. Seeded, so a patch plays the same
        crackle every time. */
    class NoiseDustNode : public Node
    {
    public:
        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            reset();
        }
        void reset() override { state = (seed + 1u) * 2654435761u | 1u; }

        int getNumInputPorts() const noexcept override { return 2; }
        int getNumOutputPorts() const noexcept override { return 2; }
        juce::String getTitle() const override { return "Dust"; }
        juce::String getCategory() const override { return "Generators"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "noise.dust.density", .type = SignalType::Control, .label = "Density", .unit = "/s",
                                  .minValue = 0.1f, .maxValue = 10000.0f, .defaultValue = 20.0f, .isLogScale = true,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency, .curve = Curve::Logarithmic },
                PortDescriptor { .id = "noise.dust.randomness", .type = SignalType::Control, .label = "Randomness",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .hasFallbackWhenUnconnected = true,
                                  .quantity = Quantity::Unipolar },
            };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = SignalType::Audio, .label = "Out", .isPrimaryOutput = true },
                PortDescriptor { .id = "trigger", .type = SignalType::Event, .label = "Trigger" },
            };
        }
        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "noise.dust.polarity", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                       .displayName = "Polarity", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "unipolar", "Unipolar" }, { "bipolar", "Bipolar" } } },
                ParameterDescriptor { .id = "noise.dust.seed", .minValue = 0.0f, .maxValue = 9999.0f, .defaultValue = 0.0f,
                                       .displayName = "Seed", .isInteger = true, .kind = ValueKind::Int, .isStructural = true },
            };
        }
        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "noise.dust.polarity")
                bipolar = value > 0.5f;
            else if (parameterId == "noise.dust.seed")
            {
                seed = (uint32_t) juce::jmax (0, (int) std::lround (value));
                reset();
            }
            else if (parameterId == "noise.dust.density")
                storedDensity = value;
            else if (parameterId == "noise.dust.randomness")
                storedRandomness = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto density = std::isnan (inputs[0]) ? storedDensity : juce::jlimit (0.0f, 48000.0f, inputs[0]);
            const auto randomness = std::isnan (inputs[1]) ? storedRandomness : juce::jlimit (0.0f, 1.0f, inputs[1]);

            const auto probability = sampleRate > 0.0 ? (double) density / sampleRate : 0.0;
            if (uniform() < probability)
            {
                auto height = 1.0f - randomness * (float) uniform();
                if (bipolar && uniform() < 0.5)
                    height = -height;
                outputs[0] = height;
                outputs[1] = 1.0f;
            }
            else
            {
                outputs[0] = 0.0f;
                outputs[1] = 0.0f;
            }
        }

    private:
        double uniform() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (double) state * (1.0 / 4294967296.0);
        }

        double sampleRate = 48000.0;
        uint32_t seed = 0, state = 1;
        bool bipolar = false;
        float storedDensity = 20.0f, storedRandomness = 0.5f;
    };
}
