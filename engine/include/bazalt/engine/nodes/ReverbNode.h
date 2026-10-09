#pragma once

#include "bazalt/engine/ReverbDsp.h"
#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "space.reverb" (wiki/plans/Reverb.md). Stereo in, stereo
        out. Input tone (low cut / high cut) -> predelay -> a multichannel
        diffuser (ReverbDsp.h's Diffuser — also its early sound) -> a feedback
        delay network with a Householder matrix, per-line low/high shelves and
        slowly wandering line lengths -> two decorrelated outputs -> width ->
        dry/wet.

        Controls are physical, so a scene, a material or a modulator can drive
        them meaningfully: `size` in metres sets the line lengths and the
        diffusion span; `decay` is the RT60 in seconds, with `decayLow`/
        `decayHigh` multiplying it below/above the network's crossovers
        (250 Hz / 3 kHz) — the measured decay per band is what these say
        (tests/ReverbTests.cpp holds it to that). `freeze` makes the network
        lossless and mutes its input: an infinite sustain on a gate.

        Loudness: the network's input is scaled by sqrt(1 - g²) (g = its mean
        per-pass gain), so a sustained signal sits at about the same level
        whatever the decay time — a 20 s tail doesn't arrive 15 dB louder than
        a 0.5 s one.

        `quality` picks 8 or 16 lines (structural; both are prepared, so the
        switch never allocates). 8 is cheap enough to place per voice.
        Controls are read every 32 samples counted from prepare(), never per
        host block — the output never depends on block size.
    */
    class ReverbNode : public Node
    {
    public:
        static constexpr int numInputs = 15;  // in (Stereo) + 14 controls
        static constexpr int numOutputs = 1;  // out (Stereo)
        static constexpr int controlInterval = 32;
        static constexpr float minSize = 1.0f, maxSize = 50.0f;
        static constexpr float maxPredelayMs = 500.0f;

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            const auto maxLineSeconds = 2.0 * maxSize / reverb::speedOfSound + 0.01;
            for (int q = 0; q < 2; ++q)
            {
                const auto lines = q == 0 ? 8 : 16;
                cores[(size_t) q].diffuser.prepare (sampleRate, lines, maxDiffusionSeconds, 0xd1ff + q);
                cores[(size_t) q].diffuser.setStages (4);
                cores[(size_t) q].fdn.prepare (sampleRate, lines, maxLineSeconds, 0xfd17 + q);
            }
            predelayLeft.prepare ((int) std::ceil (maxPredelayMs * 0.001 * sampleRate) + 2);
            predelayRight.prepare ((int) std::ceil (maxPredelayMs * 0.001 * sampleRate) + 2);
            controlCountdown = 0;
            inputGainInitialised = false;
        }

        void reset() override
        {
            for (auto& core : cores)
            {
                core.diffuser.reset();
                core.fdn.reset();
            }
            predelayLeft.reset();
            predelayRight.reset();
            for (auto* filter : { &lowCutLeft, &lowCutRight, &highCutLeft, &highCutRight })
                filter->reset();
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }
        int getNumInputChannels() const noexcept override { return numInputs + 1; }
        int getNumOutputChannels() const noexcept override { return 2; }

        juce::String getTitle() const override { return "Reverb"; }
        juce::String getCategory() const override { return "Space"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            const auto unit = [] (const char* id, const char* label, float defaultValue)
            {
                return PortDescriptor { .id = id, .type = SignalType::Signal, .label = label, .minValue = 0.0f, .maxValue = 1.0f,
                                        .defaultValue = defaultValue, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar };
            };
            const auto multiplier = [] (const char* id, const char* label, float minValue, float maxValue, float defaultValue)
            {
                return PortDescriptor { .id = id, .type = SignalType::Signal, .label = label, .unit = "x",
                                        .minValue = minValue, .maxValue = maxValue, .defaultValue = defaultValue,
                                        .isLogScale = true, .hasFallbackWhenUnconnected = true, .curve = Curve::Logarithmic };
            };
            auto lowCut = ValueTypes::frequencyPort ("space.reverb.lowCut", "Low Cut", 20.0f);
            lowCut.maxValue = 1000.0f;
            auto highCut = ValueTypes::frequencyPort ("space.reverb.highCut", "High Cut", 12000.0f);
            highCut.minValue = 1000.0f;

            return {
                PortDescriptor { .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio, .channels = Channels::Stereo },
                PortDescriptor { .id = "space.reverb.size", .type = SignalType::Signal, .label = "Size", .unit = "m",
                                  .minValue = minSize, .maxValue = maxSize, .defaultValue = 12.0f, .isLogScale = true,
                                  .hasFallbackWhenUnconnected = true, .curve = Curve::Logarithmic },
                ValueTypes::timeSecondsPort ("space.reverb.decay", "Decay", 2.5f, 60.0f),
                multiplier ("space.reverb.decayLow", "Decay Low", 0.25f, 4.0f, 1.2f),
                multiplier ("space.reverb.decayHigh", "Decay High", 0.1f, 2.0f, 0.5f),
                ValueTypes::timeMsPort ("space.reverb.predelay", "Predelay", 10.0f, maxPredelayMs),
                unit ("space.reverb.diffusion", "Diffusion", 0.85f),
                unit ("space.reverb.modulation", "Modulation", 0.3f),
                PortDescriptor { .id = "space.reverb.modRate", .type = SignalType::Signal, .label = "Mod Rate", .unit = "Hz",
                                  .minValue = 0.05f, .maxValue = 5.0f, .defaultValue = 0.6f, .isLogScale = true,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency, .curve = Curve::Logarithmic },
                unit ("space.reverb.early", "Early", 0.3f),
                lowCut,
                highCut,
                unit ("space.reverb.width", "Width", 1.0f),
                unit ("space.reverb.mix", "Mix", 0.3f),
                PortDescriptor { .id = "space.reverb.freeze", .type = SignalType::Signal, .label = "Freeze",
                                  .defaultValue = 0.0f, .hasFallbackWhenUnconnected = true, .kind = ValueKind::Bool, .quantity = Quantity::Boolean },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true, .quantity = Quantity::Audio, .channels = Channels::Stereo } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "space.reverb.quality", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                            .displayName = "Quality", .isInteger = true, .kind = ValueKind::Enum,
                                            .enumOptions = { { "eight", "8 lines" }, { "sixteen", "16 lines" } },
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "space.reverb.quality")
            {
                activeCore = value > 0.5f ? 1 : 0;
                return;
            }
            for (int i = 0; i < numControls; ++i)
                if (parameterId == controlIds()[(size_t) i])
                    stored[(size_t) i] = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            auto& core = cores[(size_t) activeCore];
            const auto lines = core.fdn.getNumLines();

            if (--controlCountdown < 0)
            {
                controlCountdown = controlInterval - 1;
                updateControls (inputs + 2, core);
            }

            // Input tone, then predelay.
            const auto left = highCutLeft.process (lowCutLeft.process (inputs[0]));
            const auto right = highCutRight.process (lowCutRight.process (inputs[1]));
            predelayLeft.write (left);
            predelayRight.write (right);
            const auto delayedLeft = predelayLeft.readInteger (predelaySamples);
            const auto delayedRight = predelayRight.readInteger (predelaySamples);

            // Diffusion: the early sound, and what feeds the network.
            std::array<float, reverb::maxChannels> diffused {}, late {};
            reverb::upmix (delayedLeft, delayedRight, diffused.data(), lines);
            core.diffuser.process (diffused.data());

            float earlyLeft, earlyRight;
            reverb::downmix (diffused.data(), lines, earlyLeft, earlyRight);

            inputGain += (targetInputGain - inputGain) * 0.002f; // freeze in/out without a click
            for (int i = 0; i < lines; ++i)
                diffused[(size_t) i] *= inputGain;

            core.fdn.process (diffused.data(), late.data());
            float lateLeft, lateRight;
            reverb::downmix (late.data(), lines, lateLeft, lateRight);

            auto wetLeft = lateLeft + earlyLeft * early;
            auto wetRight = lateRight + earlyRight * early;
            const auto mid = (wetLeft + wetRight) * 0.5f;
            const auto side = (wetLeft - wetRight) * 0.5f * width;
            wetLeft = mid + side;
            wetRight = mid - side;

            outputs[0] = inputs[0] * (1.0f - mix) + wetLeft * mix;
            outputs[1] = inputs[1] * (1.0f - mix) + wetRight * mix;
        }

    private:
        static constexpr int numControls = 14;
        static constexpr double maxDiffusionSeconds = 0.08;

        static const std::array<juce::String, numControls>& controlIds()
        {
            static const std::array<juce::String, numControls> ids {
                "space.reverb.size", "space.reverb.decay", "space.reverb.decayLow", "space.reverb.decayHigh",
                "space.reverb.predelay", "space.reverb.diffusion", "space.reverb.modulation", "space.reverb.modRate",
                "space.reverb.early", "space.reverb.lowCut", "space.reverb.highCut", "space.reverb.width",
                "space.reverb.mix", "space.reverb.freeze"
            };
            return ids;
        }

        struct Core
        {
            reverb::Diffuser diffuser;
            reverb::FeedbackDelayNetwork fdn;
        };

        // `controls` = inputs[2..15], in controlIds() order; NaN = unconnected.
        void updateControls (const float* controls, Core& core) noexcept
        {
            std::array<float, numControls> v {};
            for (int i = 0; i < numControls; ++i)
                v[(size_t) i] = std::isnan (controls[i]) ? stored[(size_t) i] : controls[i];

            const auto size = juce::jlimit (minSize, maxSize, v[0]);
            const auto decay = juce::jlimit (0.1f, 60.0f, v[1]);
            const auto decayLow = juce::jlimit (0.25f, 4.0f, v[2]);
            const auto decayHigh = juce::jlimit (0.1f, 2.0f, v[3]);
            const auto frozen = v[13] > 0.5f;

            predelaySamples = (int) std::lround (juce::jlimit (0.0f, maxPredelayMs, v[4]) * 0.001 * sampleRate);

            const auto roomSeconds = (double) size / reverb::speedOfSound;
            core.fdn.setBaseLength ((float) (roomSeconds * sampleRate));
            core.fdn.setDecay (decay * decayLow, decay, decay * decayHigh, frozen);

            const auto diffusion = juce::jlimit (0.0f, 1.0f, v[5]);
            const auto spanSeconds = juce::jmin (maxDiffusionSeconds, 0.004 + roomSeconds * 0.4);
            core.diffuser.setSpan ((float) (spanSeconds * sampleRate) * diffusion);

            const auto depth = juce::jlimit (0.0f, 1.0f, v[6]) * 12.0f * (float) (sampleRate / 48000.0);
            const auto rate = juce::jlimit (0.05f, 5.0f, v[7]);
            core.fdn.setModulation (depth, rate);
            core.diffuser.setModulation (depth * 0.25f, rate * 1.3f);

            early = juce::jlimit (0.0f, 1.0f, v[8]);
            const auto nyquistGuard = (float) (sampleRate * 0.45);
            const auto lowCutHz = juce::jlimit (20.0f, 1000.0f, v[9]);
            const auto highCutHz = juce::jlimit (1000.0f, juce::jmin (20000.0f, nyquistGuard), v[10]);
            lowCutLeft.setHighpass (sampleRate, lowCutHz);
            lowCutRight.setHighpass (sampleRate, lowCutHz);
            highCutLeft.setLowpass (sampleRate, highCutHz);
            highCutRight.setLowpass (sampleRate, highCutHz);
            width = juce::jlimit (0.0f, 1.0f, v[11]);
            mix = juce::jlimit (0.0f, 1.0f, v[12]);

            const auto g = juce::jlimit (0.0f, 0.9999f, core.fdn.getMeanMidGain());
            targetInputGain = frozen ? 0.0f : std::sqrt (1.0f - g * g);
            if (! inputGainInitialised)
            {
                inputGain = targetInputGain;
                inputGainInitialised = true;
            }
        }

        double sampleRate = 48000.0;
        int activeCore = 0;
        int controlCountdown = 0;
        int predelaySamples = 0;
        float early = 0.3f, width = 1.0f, mix = 0.3f;
        float inputGain = 0.0f, targetInputGain = 0.0f;
        bool inputGainInitialised = false;
        std::array<float, numControls> stored { 12.0f, 2.5f, 1.2f, 0.5f, 10.0f, 0.85f, 0.3f, 0.6f, 0.3f, 20.0f, 12000.0f, 1.0f, 0.3f, 0.0f };
        std::array<Core, 2> cores;
        reverb::DelayLine predelayLeft, predelayRight;
        reverb::OnePole lowCutLeft, lowCutRight, highCutLeft, highCutRight;
    };
}
