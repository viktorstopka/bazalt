#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "space.width" (NODE_CATALOG.md's `space.*` row).
        Stereo in, stereo out — one real `Channels::Stereo` port each side
        (real stereo cable redesign — see `PanNode.h`'s own comment and
        `wiki/NODES.System.md` §9 for the full reasoning). `processSample()`
        is byte-for-byte unchanged: `inputs[0]`/`inputs[1]` and
        `outputs[0]`/`outputs[1]` were always the left/right flat positions.
        `getNumInputChannels()`/`getNumOutputChannels()` must be overridden
        (see `Node.h`'s own comment) since the default would otherwise
        report descriptor counts, not real flat-channel counts.

        **`width`**: the same equal-power mid/side cross-mix `space.pan`
        applies to its own already-panned pair — `width=1` is a true no-op,
        `0` collapses to mono, `2` exaggerates the stereo difference.

        **`bassMonoBelow`**: an ordinary mastering-chain technique, not
        anything novel — a one-pole crossover (the same `CoefficientCache`
        idiom `SlewNode.h` established, one shared coefficient since both
        channels share one crossover frequency) splits each channel into a
        low band (summed to mono, unaffected by `width`) and a high band
        (the only part `width` touches), then the two bands are added back
        per channel. Below the crossover, a mono bass signal never loses
        low-frequency phase coherence to widening — the reason this control
        exists at all in a real mastering chain.
    */
    class WidthNode : public Node
    {
    public:
        static constexpr float defaultBassMonoBelowHz = 120.0f;
        static constexpr int numInputs = 3;  // in (Stereo), width, bassMonoBelow
        static constexpr int numOutputs = 1; // out (Stereo)

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }

        void reset() override
        {
            lowStateLeft = 0.0f;
            lowStateRight = 0.0f;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }
        int getNumInputChannels() const noexcept override { return 4; }  // in(2) + width(1) + bassMonoBelow(1)
        int getNumOutputChannels() const noexcept override { return 2; }

        juce::String getTitle() const override { return "Width"; }
        juce::String getCategory() const override { return "Space"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "in", .type = SignalType::Audio, .channels = Channels::Stereo },
                PortDescriptor { .id = "space.width.width", .type = SignalType::Control, .label = "Width",
                                  .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 1.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "space.width.bassMonoBelow", .type = SignalType::Control, .label = "Bass Mono Below",
                                  .unit = "Hz", .minValue = 20.0f, .maxValue = 500.0f, .defaultValue = defaultBassMonoBelowHz,
                                  .isLogScale = true, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency,
                                  .curve = Curve::Logarithmic },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = SignalType::Audio, .label = "Out", .isPrimaryOutput = true, .channels = Channels::Stereo },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "space.width.width")
                storedWidth = juce::jlimit (0.0f, 2.0f, value);
            else if (parameterId == "space.width.bassMonoBelow")
                storedBassMonoBelowHz = juce::jlimit (20.0f, 500.0f, value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto width = std::isnan (inputs[2]) ? storedWidth : juce::jlimit (0.0f, 2.0f, inputs[2]);
            const auto crossoverHz = std::isnan (inputs[3]) ? storedBassMonoBelowHz : juce::jlimit (20.0f, 500.0f, inputs[3]);

            const auto coefficient = coefficientFor (crossoverHz);

            lowStateLeft = coefficient * inputs[0] + (1.0f - coefficient) * lowStateLeft;
            lowStateRight = coefficient * inputs[1] + (1.0f - coefficient) * lowStateRight;
            const auto highLeft = inputs[0] - lowStateLeft;
            const auto highRight = inputs[1] - lowStateRight;

            const auto monoLow = (lowStateLeft + lowStateRight) * 0.5f;
            const auto midHigh = (highLeft + highRight) * 0.5f;
            const auto sideHigh = (highLeft - highRight) * 0.5f;

            outputs[0] = monoLow + midHigh + sideHigh * width;
            outputs[1] = monoLow + midHigh - sideHigh * width;
        }

    private:
        float coefficientFor (float cutoffHz) noexcept
        {
            if (cutoffHz != cache.cutoffHz)
            {
                cache.cutoffHz = cutoffHz;
                cache.coefficient = sampleRate > 0.0
                                       ? (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * (double) cutoffHz / sampleRate))
                                       : 1.0f;
            }
            return cache.coefficient;
        }

        struct CoefficientCache
        {
            float cutoffHz = -1.0f;
            float coefficient = 1.0f;
        };

        double sampleRate = 44100.0;
        float storedWidth = 1.0f;
        float storedBassMonoBelowHz = defaultBassMonoBelowHz;
        float lowStateLeft = 0.0f;
        float lowStateRight = 0.0f;
        CoefficientCache cache;
    };
}
