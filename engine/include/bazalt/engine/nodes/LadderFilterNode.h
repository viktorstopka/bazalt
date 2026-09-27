#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/LadderFilter.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "filter.ladder" (NODE_CATALOG.md's `filter.*` row,
        the one entry the catalog itself flags "Native: numerically
        delicate"). Wraps `LadderFilter.h` — see that file's own header
        comment for the full ZDF derivation and the documented, deliberate
        simplifications (resonance clamped just under literal
        self-oscillation rather than a nonlinear feedback loop; `drive` is
        a post-loop `tanh`, not per-stage saturation).

        `keyTrack`/`keyPitch` (both real here, unlike `filter.svf`, which
        the catalog also lists them on but which doesn't implement them
        yet — CLAUDE.md's own known-gaps note): `effectiveCutoff = cutoff *
        2^((keyPitch-60)/12 * keyTrack)` — standard 1V/octave-style keyboard
        tracking, `keyTrack=0` (the default) meaning no tracking at all
        (exactly today's fixed-cutoff behaviour), `keyTrack=1` meaning the
        cutoff moves a full octave per octave of `keyPitch`, referenced
        against MIDI note 60 (middle C) the same way `osc.analog`'s own
        `pitch` port is.

        Coefficients are recomputed only when the live cutoff/resonance
        actually changed (`LadderFilter`'s own internal cache for cutoff;
        `setResonance` is cheap enough — one multiply — to call unconditionally).
    */
    class LadderFilterNode : public Node
    {
    public:
        static constexpr float defaultCutoffHz = 1000.0f;
        static constexpr float defaultResonance = 0.2f;
        static constexpr float defaultDrive = 1.0f;
        static constexpr float defaultKeyTrack = 0.0f;
        static constexpr float defaultKeyPitch = 60.0f;
        static constexpr int numInputs = 6; // in, cutoff, resonance, drive, keyTrack, keyPitch
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override { ladder.prepare (info.sampleRate); }
        void reset() override { ladder.reset(); }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Ladder Filter"; }
        juce::String getCategory() const override { return "Filters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { "in", SignalType::Audio },
                ValueTypes::frequencyPort ("filter.ladder.cutoff", "Cutoff", defaultCutoffHz),
                PortDescriptor { .id = "filter.ladder.resonance", .type = SignalType::Control, .label = "Resonance",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultResonance,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar,
                                  .polarity = Polarity::Unipolar },
                PortDescriptor { .id = "filter.ladder.drive", .type = SignalType::Control, .label = "Drive",
                                  .minValue = 1.0f, .maxValue = 30.0f, .defaultValue = defaultDrive,
                                  .isLogScale = true, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Gain,
                                  .curve = Curve::Logarithmic },
                PortDescriptor { .id = "filter.ladder.keyTrack", .type = SignalType::Control, .label = "Key Track",
                                  .minValue = -1.0f, .maxValue = 2.0f, .defaultValue = defaultKeyTrack,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Ratio,
                                  .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "filter.ladder.keyPitch", .type = SignalType::Control, .label = "Key Pitch",
                                  .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = defaultKeyPitch,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Pitch },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "filter.ladder.poles",
                                       .minValue = 1.0f,
                                       .maxValue = 4.0f,
                                       .defaultValue = 4.0f,
                                       .displayName = "Poles",
                                       .isInteger = true,
                                       .quantity = Quantity::Count,
                                       .step = 1.0f,
                                       .isStructural = true },
                ParameterDescriptor { .id = "filter.ladder.mode",
                                       .minValue = 0.0f,
                                       .maxValue = 2.0f,
                                       .defaultValue = 0.0f,
                                       .displayName = "Mode",
                                       .isInteger = true,
                                       .kind = ValueKind::Enum,
                                       .enumOptions = { { "lowpass", "Lowpass" }, { "highpass", "Highpass" }, { "bandpass", "Bandpass" } },
                                       .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "filter.ladder.cutoff")
                storedCutoffHz = value;
            else if (parameterId == "filter.ladder.resonance")
                storedResonance = value;
            else if (parameterId == "filter.ladder.drive")
                storedDrive = value;
            else if (parameterId == "filter.ladder.keyTrack")
                storedKeyTrack = value;
            else if (parameterId == "filter.ladder.keyPitch")
                storedKeyPitch = value;
            else if (parameterId == "filter.ladder.poles")
                ladder.setPoles ((int) std::lround (value));
            else if (parameterId == "filter.ladder.mode")
            {
                switch ((int) std::lround (value))
                {
                    case 1:  ladder.setMode (LadderFilter::Mode::Highpass); break;
                    case 2:  ladder.setMode (LadderFilter::Mode::Bandpass); break;
                    default: ladder.setMode (LadderFilter::Mode::Lowpass); break;
                }
            }
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto cutoff = std::isnan (inputs[1]) ? storedCutoffHz : inputs[1];
            const auto resonance = std::isnan (inputs[2]) ? storedResonance : inputs[2];
            const auto drive = std::isnan (inputs[3]) ? storedDrive : inputs[3];
            const auto keyTrack = std::isnan (inputs[4]) ? storedKeyTrack : inputs[4];
            const auto keyPitch = std::isnan (inputs[5]) ? storedKeyPitch : inputs[5];

            const auto effectiveCutoff = keyTrack == 0.0f
                                            ? cutoff
                                            : cutoff * std::pow (2.0f, (keyPitch - 60.0f) / 12.0f * keyTrack);

            ladder.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f, effectiveCutoff));
            ladder.setResonance (resonance);
            outputs[0] = ladder.processSample (inputs[0], drive);
        }

    private:
        float storedCutoffHz = defaultCutoffHz;
        float storedResonance = defaultResonance;
        float storedDrive = defaultDrive;
        float storedKeyTrack = defaultKeyTrack;
        float storedKeyPitch = defaultKeyPitch;
        LadderFilter ladder;
    };
}
