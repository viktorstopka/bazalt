#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "adapt.audioToControl" (wiki/plans/AudioControlBridge.md).
        The mechanical half of the Audio -> Control bridge: reads a raw
        waveform's instantaneous per-sample value and hands it out as an
        ordinary Bipolar Control signal, scaled by `depth`. Deliberately NOT
        `env.follower` — that node rectifies and smooths, throwing away the
        waveform on purpose; this node keeps it, on purpose, because the
        whole point is audio-rate FM/ring-mod/physical-parameter modulation,
        where the instant-by-instant value of the source IS the modulator
        (plan §3). Same spirit as `adapt.map`/`adapt.normalise`: mechanical,
        opinion-free, allocation-free — not a creative DSP effect.

        `canConnect` (`CanConnect.cpp`) auto-inserts this for any
        `Audio -> Control` connection whose source is mono (stereo is a hard
        reject in v1, plan §4.2/§6 — `mix.downmix` first, by hand). When the
        destination is a real-quantity port, this node is step one of a
        2-step chain, followed by `adapt.map` (seeded from the destination's
        own range) — this node's own job stays exactly "cross the type wall
        into Bipolar," never "also guess the destination's range."

        `depth` has `hasFallbackWhenUnconnected` with a real unconnected
        default of 1.0 (full-strength passthrough) — the same "unpatched is
        just as loud/present as before" precedent `mix.gain.gain` established
        (wiki/NODES_Gaps.md's `modulation-only-port` fix), not a silent
        NaN-multiply trap.
    */
    class AudioToControlNode : public Node
    {
    public:
        static constexpr float defaultDepth = 1.0f;
        static constexpr int numInputs = 2;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "To Modulation"; }
        juce::String getCategory() const override { return "Adapters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { "in", SignalType::Audio },
                PortDescriptor { .id = "depth", .type = SignalType::Control, .label = "Depth",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultDepth,
                                  .hasFallbackWhenUnconnected = true,
                                  .quantity = Quantity::Unipolar, .curve = Curve::Linear,
                                  .polarity = Polarity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out",
                                       .type = SignalType::Control,
                                       .isPrimaryOutput = true,
                                       .quantity = Quantity::Bipolar,
                                       .polarity = Polarity::Bipolar } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { { "depth", 0.0f, 1.0f, defaultDepth, 1.0f, "", "Depth" } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "depth")
                storedDepth = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto depth = std::isnan (inputs[1]) ? storedDepth : inputs[1];
            outputs[0] = std::clamp (inputs[0] * depth, -1.0f, 1.0f);
        }

    private:
        float storedDepth = defaultDepth;
    };
}
