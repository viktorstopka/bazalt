#pragma once

#include "bazalt/engine/ReverbDsp.h"
#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "space.diffuser" (wiki/plans/Reverb.md §3). Stereo in,
        stereo out: smears a signal into dense, uncoloured noise-like texture
        without adding a tail — the first half of a reverb on its own, useful
        for softening transients or thickening a delay. Eight internal channels
        through `stages` Hadamard diffusion stages (ReverbDsp.h's Diffuser,
        the same code space.reverb runs). Lossless: energy in = energy out.

        `size` is the total smear span in ms; `diffusion` scales it (0 = no
        smear); `modulation` gently moves the internal delays, softening any
        residual pattern. Controls are read every 32 samples counted from
        prepare(), never per host block, so output never depends on block size.
    */
    class DiffuserNode : public Node
    {
    public:
        static constexpr int numInputs = 4;  // in (Stereo), size, diffusion, modulation
        static constexpr int numOutputs = 1; // out (Stereo)
        static constexpr int controlInterval = 32;
        static constexpr float maxSizeMs = 100.0f;

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            diffuser.prepare (sampleRate, channels, maxSizeMs / 1000.0, 0x5d1ff);
            diffuser.setStages (stages);
            controlCountdown = 0;
        }

        void reset() override { diffuser.reset(); }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }
        int getNumInputChannels() const noexcept override { return 5; }
        int getNumOutputChannels() const noexcept override { return 2; }

        juce::String getTitle() const override { return "Diffuser"; }
        juce::String getCategory() const override { return "Space"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "in", .type = SignalType::Audio, .channels = Channels::Stereo },
                ValueTypes::timeMsPort ("space.diffuser.size", "Size", 30.0f, maxSizeMs),
                PortDescriptor { .id = "space.diffuser.diffusion", .type = SignalType::Control, .label = "Diffusion",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "space.diffuser.modulation", .type = SignalType::Control, .label = "Modulation",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Audio, .label = "Out", .isPrimaryOutput = true, .channels = Channels::Stereo } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "space.diffuser.stages", .minValue = 1.0f, .maxValue = (float) reverb::Diffuser::maxStages,
                                            .defaultValue = 4.0f, .displayName = "Stages", .isInteger = true,
                                            .kind = ValueKind::Int, .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "space.diffuser.stages")
            {
                stages = juce::jlimit (1, reverb::Diffuser::maxStages, (int) std::lround (value));
                diffuser.setStages (stages);
            }
            else if (parameterId == "space.diffuser.size")
                storedSizeMs = juce::jlimit (0.0f, maxSizeMs, value);
            else if (parameterId == "space.diffuser.diffusion")
                storedDiffusion = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "space.diffuser.modulation")
                storedModulation = juce::jlimit (0.0f, 1.0f, value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            if (--controlCountdown < 0)
            {
                controlCountdown = controlInterval - 1;
                const auto sizeMs = std::isnan (inputs[2]) ? storedSizeMs : juce::jlimit (0.0f, maxSizeMs, inputs[2]);
                const auto diffusion = std::isnan (inputs[3]) ? storedDiffusion : juce::jlimit (0.0f, 1.0f, inputs[3]);
                const auto modulation = std::isnan (inputs[4]) ? storedModulation : juce::jlimit (0.0f, 1.0f, inputs[4]);
                diffuser.setSpan ((float) (sizeMs * 0.001 * sampleRate) * diffusion);
                diffuser.setModulation (modulation * 6.0f * (float) (sampleRate / 48000.0), 0.8f);
            }

            std::array<float, reverb::maxChannels> x {};
            reverb::upmix (inputs[0], inputs[1], x.data(), channels);
            diffuser.process (x.data());
            reverb::downmix (x.data(), channels, outputs[0], outputs[1]);
        }

    private:
        static constexpr int channels = 8;
        double sampleRate = 48000.0;
        int stages = 4;
        int controlCountdown = 0;
        float storedSizeMs = 30.0f, storedDiffusion = 1.0f, storedModulation = 0.0f;
        reverb::Diffuser diffuser;
    };
}
