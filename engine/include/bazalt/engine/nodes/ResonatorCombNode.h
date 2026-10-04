#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "resonator.comb" (wiki/NODES.md's `resonator.*` row,
        the PM Core batch). The cheapest resonator in the catalog, and —
        unlike plain `delay.line` (a generic building block with no feedback
        of its own, for hand-built feedback networks) — a real, self-
        contained resonant filter: feedback and damping are baked in here,
        the same way `filter.ladder` bakes in its own resonance rather than
        asking the patch to wire one up externally.

        **Two structural modes** (the catalog's own `type` enum):
        - `feedback` (the default, IIR): `y[n] = x[n] + g * damped(y[n-M])`
          — energy recirculates, builds a genuine resonance at `frequency`
          and its harmonics. This is the textbook Karplus-Strong-style
          feedback comb, `damping`'s one-pole lowpass sitting INSIDE the
          loop (classic "absorption" modelling — high frequencies die out
          faster on every round trip).
        - `feedforward` (FIR): `y[n] = x[n] + g * damped(x[n-M])` — taps the
          INPUT only, never its own output, so it can never build up energy
          or ring; one pass, pure comb-filtering (notches/peaks), always
          unconditionally stable regardless of `feedback`'s sign/magnitude.

        **`damping`** reuses `filter.onepole`'s own "Damping" port
        convention exactly (same formula, same meaning, for the same
        label to mean the same thing everywhere in this catalog): the
        in-loop one-pole's blend coefficient, `0` = fully damped/darkest,
        `1` = no damping/brightest. Applied in both modes, for a
        feedforward comb too, for the same tone-shaping reason, even though
        only the feedback mode actually needs it to tame runaway brightness.

        **`frequency [audio]`** sets the delay length (`delaySamples =
        sampleRate / frequency`), read and reclamped every sample — the
        established pattern this catalog already uses for every other
        audio-rate-modulatable filter parameter (`filter.ladder.cutoff`,
        `delay.line.samples`'s own cable). No fractional-delay interpolation
        (plain integer sample indexing, rounded) — the same simplification
        `DelayNode.h` itself already makes; a sub-sample-accurate version is
        real, separate future work if pitch-accuracy at this node's own
        (non-playable, utility-resonator) role ever needs it.

        **`feedback`** is hard-limited to `(-0.999, 0.999)` regardless of
        the raw input — the catalog's own "hard-limited below 1" spec — so
        the feedback-mode loop can never literally reach or exceed unity
        gain and diverge, matching `delay.line`'s own catalog note for the
        same reason.
    */
    class ResonatorCombNode : public Node
    {
    public:
        static constexpr float minFrequencyHz = 20.0f;
        static constexpr float maxFeedbackMagnitude = 0.999f;
        static constexpr int numInputs = 4;  // in, frequency, feedback, damping
        static constexpr int numOutputs = 1;

        enum class CombType { Feedforward, Feedback };

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            maxDelaySamples = juce::jmax (4, (int) std::ceil (sampleRate / minFrequencyHz) + 1);
            buffer.assign ((size_t) maxDelaySamples, 0.0f);
            writeIndex = 0;
            dampingState = 0.0f;
        }

        void reset() override
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writeIndex = 0;
            dampingState = 0.0f;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Comb"; }
        juce::String getCategory() const override { return "Resonators"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ "in", SignalType::Audio }),
                ValueTypes::frequencyPort ("resonator.comb.frequency", "Frequency", 220.0f),
                PortDescriptor { .id = "resonator.comb.feedback", .type = SignalType::Control, .label = "Feedback",
                                  .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.5f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Bipolar,
                                  .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "resonator.comb.damping", .type = SignalType::Control, .label = "Damping",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar,
                                  .polarity = Polarity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { perChannel ({ "out", SignalType::Audio }) };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "resonator.comb.type",
                                       .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, // feedback
                                       .displayName = "Type", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "feedforward", "Feedforward" }, { "feedback", "Feedback" } },
                                       .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "resonator.comb.frequency")
                storedFrequencyHz = value;
            else if (parameterId == "resonator.comb.feedback")
                storedFeedback = value;
            else if (parameterId == "resonator.comb.damping")
                storedDamping = value;
            else if (parameterId == "resonator.comb.type")
                type = value >= 0.5f ? CombType::Feedback : CombType::Feedforward;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto frequency = std::isnan (inputs[1]) ? storedFrequencyHz : inputs[1];
            const auto feedback = std::isnan (inputs[2]) ? storedFeedback : inputs[2];
            const auto damping = std::isnan (inputs[3]) ? storedDamping : inputs[3];

            const auto clampedFrequency = juce::jmax (minFrequencyHz, frequency);
            const auto delaySamples = juce::jlimit (1, maxDelaySamples - 1,
                                                     (int) std::lround (sampleRate / (double) clampedFrequency));
            const auto feedbackGain = juce::jlimit (-maxFeedbackMagnitude, maxFeedbackMagnitude, feedback);
            const auto dampingCoefficient = juce::jlimit (0.0f, 1.0f, damping);

            auto readIndex = writeIndex - delaySamples;
            if (readIndex < 0)
                readIndex += maxDelaySamples;

            const auto tapped = buffer[(size_t) readIndex];

            if (type == CombType::Feedback)
            {
                // Damping filter sits INSIDE the loop, on the fed-back
                // output — the classic "absorption" comb.
                dampingState = dampingCoefficient * tapped + (1.0f - dampingCoefficient) * dampingState;
                const auto y = inputs[0] + feedbackGain * dampingState;
                buffer[(size_t) writeIndex] = y;
                outputs[0] = y;
            }
            else
            {
                // Feedforward: damp the tapped INPUT sample, never our own
                // output — the buffer only ever stores raw input, so this
                // mode can never ring.
                dampingState = dampingCoefficient * tapped + (1.0f - dampingCoefficient) * dampingState;
                outputs[0] = inputs[0] + feedbackGain * dampingState;
                buffer[(size_t) writeIndex] = inputs[0];
            }

            writeIndex = (writeIndex + 1) % maxDelaySamples;
        }

    private:
        double sampleRate = 44100.0;
        int maxDelaySamples = 4;
        std::vector<float> buffer;
        int writeIndex = 0;
        float dampingState = 0.0f;

        float storedFrequencyHz = 220.0f;
        float storedFeedback = 0.5f;
        float storedDamping = 0.5f;
        CombType type = CombType::Feedback;
    };
}
