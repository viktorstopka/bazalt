#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "space.pan" (NODE_CATALOG.md's `space.*` row). Mono
        in, stereo out — `left`/`right` named ports, not
        `PortDescriptor::channels = Channels::Stereo` (see ADR-0023's
        Amendment (M22), which settles this exact question for the whole
        catalog, not just this node: that field exists and is wired into
        `canConnect`, but no real node produces actual multi-sample-per-frame
        audio through it, and every stereo entry in NODE_CATALOG.md — this
        one, `space.width`, `space.reverb`, `resonator.plate`,
        `sampler.granular` — is already written with `left`/`right` pairs).

        **`law`** (structural, default `constantPower` — the catalog names
        four options but gives no default): at pan=0 (centre),
        `linear` sends each channel at half gain (-6.02dB, the classic
        "constant voltage" convention: the two channels sum back to unity at
        the centre); `-3dB`/`constantPower` are the SAME real law under two
        names (`cos(θ)`/`sin(θ)`, θ = `(pan+1)·π/4` — the defining property
        of true constant-power panning is `cos²+sin²=1`, i.e. total power is
        the same at every pan position, and that identity lands exactly at
        -3.01dB at centre, which is what both names refer to); `-4.5dB` is a
        deeper-centre-dip law between the two, `cos(θ)^k`/`sin(θ)^k` with `k`
        solved so `cos(π/4)^k` equals -4.5dB — this node's own documented
        design call for a law the catalog names but doesn't define.

        **`width`** (0-2, the catalog's own port, no further catalog
        description) is this node's own documented design call too: an
        equal-power mid/side cross-mix of the ALREADY-panned stereo pair —
        `width=1` (default) is a true no-op (`mid+side == L`,
        `mid-side == R`, exactly the plain panned signal); `0` collapses to
        mono; `2` exaggerates the difference beyond what the pan law alone
        produces. The same mid/side formula `space.width` uses on its own
        (already-stereo) input.
    */
    class PanNode : public Node
    {
    public:
        static constexpr int numInputs = 3; // in, pan, width
        static constexpr int numOutputs = 2; // left, right

        enum class Law { Linear, Minus3dB, Minus4_5dB, ConstantPower };

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Pan"; }
        juce::String getCategory() const override { return "Space"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { "in", SignalType::Audio },
                PortDescriptor { .id = "space.pan.pan", .type = SignalType::Control, .label = "Pan",
                                  .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Bipolar,
                                  .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "space.pan.width", .type = SignalType::Control, .label = "Width",
                                  .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 1.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            // Milestone 0.2 (wiki/NODES.System.md §9): `channels = Stereo`
            // on both, in this declared order, is what GraphCompiler.cpp's
            // "Final output" resolution and the UI's stereo-pair grouping
            // read to treat "left" immediately followed by "right" as one
            // paired signal — a metadata-only addition, the ids/behaviour
            // are exactly what shipped before this milestone.
            return {
                PortDescriptor { .id = "left", .type = SignalType::Audio, .label = "Left", .isPrimaryOutput = true, .channels = Channels::Stereo },
                PortDescriptor { .id = "right", .type = SignalType::Audio, .label = "Right", .channels = Channels::Stereo },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "space.pan.law",
                                            .minValue = 0.0f,
                                            .maxValue = 3.0f,
                                            .defaultValue = 3.0f, // constantPower
                                            .displayName = "Law",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "linear", "Linear" }, { "minus3dB", "-3 dB" },
                                                              { "minus4_5dB", "-4.5 dB" }, { "constantPower", "Constant Power" } },
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "space.pan.pan")
                storedPan = juce::jlimit (-1.0f, 1.0f, value);
            else if (parameterId == "space.pan.width")
                storedWidth = juce::jlimit (0.0f, 2.0f, value);
            else if (parameterId == "space.pan.law")
                law = (Law) juce::jlimit (0, 3, (int) std::lround (value));
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto pan = std::isnan (inputs[1]) ? storedPan : juce::jlimit (-1.0f, 1.0f, inputs[1]);
            const auto width = std::isnan (inputs[2]) ? storedWidth : juce::jlimit (0.0f, 2.0f, inputs[2]);

            const auto theta = (pan + 1.0f) * juce::MathConstants<float>::halfPi * 0.5f; // (pan+1)*pi/4

            float leftGain, rightGain;
            if (law == Law::Linear)
            {
                leftGain = (1.0f - pan) * 0.5f;
                rightGain = (1.0f + pan) * 0.5f;
            }
            else
            {
                // std::max(0, ...) before pow(): cos/sin land a hair below 0 at
                // the extremes from float rounding (cos(pi/2) != exactly 0), and
                // pow() of a negative base to this fractional exponent is NaN -
                // a real bug this exact scenario (hard left/right pan) caught.
                const auto k = law == Law::Minus4_5dB ? minus4_5dBExponent : 1.0f;
                leftGain = std::pow (std::max (0.0f, std::cos (theta)), k);
                rightGain = std::pow (std::max (0.0f, std::sin (theta)), k);
            }

            const auto left = inputs[0] * leftGain;
            const auto right = inputs[0] * rightGain;

            const auto mid = (left + right) * 0.5f;
            const auto side = (left - right) * 0.5f;
            outputs[0] = mid + side * width;
            outputs[1] = mid - side * width;
        }

    private:
        // Solved so cos(pi/4)^k == 10^(-4.5/20): k = ln(gain) / ln(cos(pi/4)).
        static constexpr float minus4_5dBExponent = 1.4949f;

        float storedPan = 0.0f;
        float storedWidth = 1.0f;
        Law law = Law::ConstantPower;
    };
}
