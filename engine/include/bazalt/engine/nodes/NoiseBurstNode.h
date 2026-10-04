#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "excite.burst". One Audio output: a decaying burst
        of white noise on trigger, silence otherwise. The Karplus-Strong
        proof graph's "pluck" excitation (ARCHITECTURE.md §3.4) — fed into
        the feedback loop's Mix node from outside the per-sample region.

        wiki/NODES_Gaps.md's `hardcoded-trigger` finding (confirmed): this
        node used to have ZERO input ports — the only way to start a burst
        was a direct C++ poke (`trigger(int)`) from whoever compiled it into
        a graph, with no Event-typed port a patch could ever wire into. Now
        has a real `trigger : Event` input (the catalog's own spec), so a
        clock, a threshold detector, or anything else that produces Events
        can start it. `trigger(int)` stays as a direct poke for
        tests/tools/ProofGraphs.h that still want exact sample-accurate
        control (render-cli's Karplus-Strong path pokes it this way, and
        still can) — a real `trigger` Event connection calls the SAME
        internal start logic from processSample() below, never a second
        implementation.

        `duration` is a real, modulatable port (catalog: 0.1–2000ms, default
        30ms) rather than only the poke's own explicit argument — sampled
        once at the moment `trigger` fires (a burst's length not changing
        mid-flight matches every other one-shot excitation node in this
        catalog).

        `tone` (-1..1, wiki/plans/SoundPalette.md Batch 2) tilts the noise:
        0 is white, negative blends toward an 800 Hz low-passed noise
        (level-matched) for soft thumps, positive toward the high-passed
        remainder for bright ticks. `shape` (-1..1) bends the decay: 0 is
        the linear ramp, negative a fast exponential-like drop (env^4 at
        -1), positive a held body that falls off at the end (env^0.25).
        The generator is seeded, so a patch renders the same burst every
        time.
    */
    class NoiseBurstNode : public Node
    {
    public:
        static constexpr float defaultDurationMs = 30.0f;
        static constexpr int numInputs = 4; // trigger, duration, tone, shape
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            lowpassCoefficient = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * toneCornerHz / sampleRate));
            lowpassGain = (float) std::sqrt (sampleRate / (juce::MathConstants<double>::pi * toneCornerHz));
        }

        void reset() override
        {
            remainingSamples = 0;
            lowpass = 0.0f;
            random.setSeed (seed);
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Noise Burst"; }
        juce::String getCategory() const override { return "Excite"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "trigger", .type = SignalType::Event, .label = "Trigger" },
                ValueTypes::timeMsPort ("excite.burst.duration", "Duration", defaultDurationMs, 2000.0f),
                PortDescriptor { .id = "excite.burst.tone", .type = SignalType::Control, .label = "Tone",
                                  .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .hasFallbackWhenUnconnected = true,
                                  .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "excite.burst.shape", .type = SignalType::Control, .label = "Shape",
                                  .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .hasFallbackWhenUnconnected = true,
                                  .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "excite.burst.duration")
                storedDurationMs = juce::jmax (0.0f, value);
            else if (parameterId == "excite.burst.tone")
                storedTone = value;
            else if (parameterId == "excite.burst.shape")
                storedShape = value;
        }

        /** Starts a burst lasting durationSamples, amplitude decaying
            linearly to zero over that span. Direct C++ poke — see class
            comment; a real `trigger` Event connection calls this
            internally via processSample() below, never a second
            implementation.
        */
        void trigger (int durationSamples) noexcept
        {
            remainingSamples = durationSamples;
            totalSamples = juce::jmax (1, durationSamples);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            // "non-zero this sample = fired" (LogicToggleNode.h's
            // documented Event convention, reused verbatim) — an
            // unconnected trigger input reads a plain 0, never firing.
            if (std::fabs (inputs[0]) > 0.0f)
            {
                const auto durationMs = std::isnan (inputs[1]) ? storedDurationMs : juce::jmax (0.0f, inputs[1]);
                trigger ((int) std::lround ((double) durationMs * 0.001 * sampleRate));
            }

            if (remainingSamples <= 0)
            {
                outputs[0] = 0.0f;
                return;
            }

            const auto tone = std::isnan (inputs[2]) ? storedTone : juce::jlimit (-1.0f, 1.0f, inputs[2]);
            const auto shape = std::isnan (inputs[3]) ? storedShape : juce::jlimit (-1.0f, 1.0f, inputs[3]);

            const auto white = random.nextFloat() * 2.0f - 1.0f;
            lowpass += lowpassCoefficient * (white - lowpass);
            const auto coloured = tone < 0.0f ? white + (lowpass * lowpassGain - white) * -tone
                                              : white + ((white - lowpass) - white) * tone;

            const auto ramp = (float) remainingSamples / (float) totalSamples;
            const auto envelope = std::pow (ramp, std::exp2 (-2.0f * shape)); // shape -1 -> ramp^4, +1 -> ramp^0.25
            outputs[0] = coloured * envelope;
            --remainingSamples;
        }

    private:
        static constexpr double toneCornerHz = 800.0;
        static constexpr juce::int64 seed = 0x8badf00d;
        juce::Random random { seed };
        double sampleRate = 44100.0;
        float storedDurationMs = defaultDurationMs, storedTone = 0.0f, storedShape = 0.0f;
        float lowpass = 0.0f, lowpassCoefficient = 1.0f, lowpassGain = 1.0f;
        int remainingSamples = 0;
        int totalSamples = 1;
    };
}
