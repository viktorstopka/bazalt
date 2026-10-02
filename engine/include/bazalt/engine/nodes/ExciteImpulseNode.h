#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "excite.impulse" (wiki/NODES.md's `excite.*` row, the
        PM Core batch). The simplest possible physical-modelling excitation:
        a single sharp transient on trigger — knocks a resonator (comb,
        modal, string, plate) into motion with no tonal character of its own,
        the "dry" complement to `excite.burst`'s noisy one.

        **`width`**: 0 (the catalog's own stated default meaning, "a true
        single-sample delta") fires exactly one sample at `amplitude`, the
        purest possible excitation — an ideal unit impulse, spectrally flat.
        Any positive `width` widens it into a short raised-cosine (Hann) bump
        instead of a hard rectangular pulse, so a widened impulse stays
        smooth/click-free rather than introducing its own harsh edges — a
        real, documented design choice this node had to make (the catalog
        names `width` but not its shape). Capped at `maxWidthMs` (50ms):
        anything longer is what `excite.burst`/`excite.pluck` are for, not
        this node's job.

        Same Event-trigger convention as `excite.burst`/every other
        Event-consuming node in this catalog: "non-zero this sample = fired"
        (`LogicToggleNode.h`'s documented convention). No legacy direct-poke
        constructor method, unlike `NoiseBurstNode.h` — this is a new node
        with no pre-Note-era callers to stay compatible with, so it only
        ever needs the one, real, graph-driven path.
    */
    class ExciteImpulseNode : public Node
    {
    public:
        static constexpr float maxWidthMs = 50.0f;
        static constexpr int numInputs = 3;  // trigger, amplitude, width
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }
        void reset() override { remainingSamples = 0; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Impulse"; }
        juce::String getCategory() const override { return "Excite"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "trigger", .type = SignalType::Event, .label = "Trigger" },
                PortDescriptor { .id = "excite.impulse.amplitude", .type = SignalType::Control, .label = "Amplitude",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar,
                                  .polarity = Polarity::Unipolar },
                ValueTypes::timeMsPort ("excite.impulse.width", "Width", 0.0f, maxWidthMs),
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "excite.impulse.amplitude")
                storedAmplitude = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "excite.impulse.width")
                storedWidthMs = juce::jlimit (0.0f, maxWidthMs, value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            if (std::fabs (inputs[0]) > 0.0f)
            {
                const auto amplitude = std::isnan (inputs[1]) ? storedAmplitude : juce::jlimit (0.0f, 1.0f, inputs[1]);
                const auto widthMs = std::isnan (inputs[2]) ? storedWidthMs : juce::jlimit (0.0f, maxWidthMs, inputs[2]);

                firedAmplitude = amplitude;
                totalSamples = juce::jmax (1, (int) std::lround ((double) widthMs * 0.001 * sampleRate));
                remainingSamples = totalSamples;
            }

            if (remainingSamples <= 0)
            {
                outputs[0] = 0.0f;
                return;
            }

            if (totalSamples == 1)
            {
                // The true single-sample delta case (width == 0) — no
                // envelope shaping needed or wanted, just the raw amplitude.
                outputs[0] = firedAmplitude;
            }
            else
            {
                // Raised-cosine (Hann) bump over [0, totalSamples), peaking
                // at amplitude in the middle — smooth, click-free widening.
                const auto elapsed = totalSamples - remainingSamples;
                const auto phase = (float) elapsed / (float) (totalSamples - 1);
                const auto window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * phase);
                outputs[0] = firedAmplitude * window;
            }

            --remainingSamples;
        }

    private:
        double sampleRate = 44100.0;
        float storedAmplitude = 1.0f;
        float storedWidthMs = 0.0f;
        float firedAmplitude = 0.0f;
        int totalSamples = 1;
        int remainingSamples = 0;
    };
}
