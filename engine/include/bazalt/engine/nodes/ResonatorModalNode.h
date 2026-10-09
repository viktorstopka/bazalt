#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "resonator.modal" (wiki/NODES.md's `resonator.*` row,
        the PM Core batch) — "the centre of the physical-modelling set."
        A bank of up to `maxModes` independent two-pole resonators, one per
        mode in the required `modes` input (a `Data(modal-set)` buffer, the
        exact shape `data.material` publishes — `(ratio, amplitudeWeight,
        decayWeight)` per mode, `stride = 3`), each driven by the same
        `excite` signal and summed into one real stereo output.

        **The resonator itself**, per mode `k` (0-based): the textbook
        undamped-then-damped coupled-form recursion used throughout the
        modal-synthesis literature (e.g. Cook, "Physically Informed Sonic
        Modeling") —
        `y[n] = 2·r·cos(w)·y[n-1] - r²·y[n-2] + excite[n]·gain_k`, where `w =
        2π·frequency_k/sampleRate` and `r` is solved from this mode's own
        effective 60dB decay time so `r^(T60·sampleRate) = 0.001`. Two
        state floats per mode (`y[n-1]`, `y[n-2]`), preallocated to
        `maxModes` in `prepare()` — never reallocated on the audio thread.

        **`frequency_k`** = `pitch (Hz) · ratio_k · extraStretch_k`, `pitch`
        converted from the `pitch` port's absolute semitones the exact same
        way `osc.analog` already does (`440·2^((pitch-69)/12)`) — the
        established convention for "this port carries an absolute note,"
        not a relative offset. `extraStretch_k = sqrt(1 + B·(k+1)²)`, `B =
        inharmonicity · 0.001` — this node's OWN `inharmonicity` knob,
        applied ON TOP of whatever `data.material` already baked into
        `ratio_k`; two different knobs for two different moments (the
        material's own character vs. a live, patchable extra stretch),
        not a duplicate. Clamped to `[1, 0.45·sampleRate]` — a real safety
        margin below Nyquist, not just the catalog's own number.

        **`effectiveT60_k`** = `max(0.001, decay · decayWeight_k ·
        brightnessDamping_k)`. `decay` is the nominal fundamental's own 60dB
        time; `decayWeight_k` is `data.material`'s own per-mode relative
        decay; `brightnessDamping_k = exp(-(1-brightness)·k^1.2·0.1)` is
        this node's OWN extra per-mode damping curve (`k=0`, the
        fundamental, is always exponent `0` — `brightness` only ever
        reshapes the balance of the OVERTONES, never the fundamental's own
        decay) — `brightness=1` (the default) applies none of this extra
        damping at all, relying purely on what `data.material` already
        encoded; dialing it down mutes the top of the spectrum progressively
        faster, independent of `data.material`'s own character.

        **`position`** (pickup/excitation point, the catalog's own
        parenthetical): `gain_k = ampWeight_k · |sin((k+1)·π·position)| /
        sqrt(activeModeCount)`. The `sin` factor is the textbook-exact mode-
        shape weighting for a string excited at a fractional point along its
        length (a real, not invented, physical formula) — applied here as a
        documented GENERALIZATION to every geometry, not a literal per-
        geometry mode-shape derivation (the same honest-approximation spirit
        `data.material`'s own plate/membrane relationship already uses). The
        `1/sqrt(activeModeCount)` factor is this node's own loudness
        normalization, so the overall level doesn't balloon as `modeCount`
        grows — a common, defensible modal-synthesis convention (summing
        `N` roughly-decorrelated resonators), not a measured calibration.

        **`spread`** (stereo distribution): `pan_k = spread · sin((k+1) ·
        goldenAngle)`, `goldenAngle ≈ 2.39996` rad (~137.5°) — a classic,
        deterministic decorrelation constant (no `seed` needed, unlike the
        instance-allocator family's own randoms: the SAME mode should always
        land at the SAME stereo position on every run of the SAME patch).
        `spread=0` collapses to mono-centre; `1` spreads modes across the
        full stereo field. Linear (not equal-power) per-mode panning — cheap,
        and summing many already-decorrelated modes makes the difference
        inaudible in practice.

        **Real stereo cable, not the catalog's stale `left`/`right` pair**:
        the catalog text for this not-yet-built node still says
        `**Out:** left, right — Audio` — written before `wiki/NODES.System.md`
        §9's stereo redesign made "one real `Channels::Stereo` port" the
        catalog-wide rule. This node declares the real, current shape (one
        `out` port, `Channels::Stereo`) instead of literally matching stale
        prose — the same correction this file's own class comment would ask
        for if the catalog text were being read strictly; `wiki/NODES.md`'s
        own entry is updated to match, not left to contradict the code.

        **Cost, named up front, not discovered later**: the catalog's own
        note on this node ("inner loop scales with mode count; delicate
        resonant filters") already anticipates recomputing `cos`/`exp`/`pow`
        once per mode per sample — up to `maxModes` (64) of each, every
        sample, the single most expensive node in this catalog by
        construction. Accepted cost, not an oversight to optimize away
        before this batch ships.
    */
    class ResonatorModalNode : public Node
    {
    public:
        static constexpr int defaultMaxModes = 64;
        static constexpr int numInputs = 8;  // excite, modes, pitch, decay, brightness, inharmonicity, position, spread
        static constexpr int numOutputs = 1; // out (Stereo — 2 flat channels)
        static constexpr float goldenAngleRad = 2.399963f;

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            // Always allocate to the CEILING (defaultMaxModes), never the
            // current `maxModesParam` member — GraphCompiler.cpp calls
            // prepare() BEFORE applying a freshly-constructed node's own
            // stored parameters, so `maxModesParam` is still at its
            // just-constructed default here, not whatever the graph's own
            // saved "resonator.modal.maxModes" value will shortly set it
            // to. Harmless today only because that default already equals
            // the ceiling (sizing from the live member would be genuinely
            // safe only by coincidence) — hardened explicitly after the
            // IDENTICAL bug class was caught live in `resonator.plate`'s
            // own `quality` (whose default is NOT its ceiling), see that
            // node's own comment for the full mechanism.
            state1.assign ((size_t) defaultMaxModes, 0.0f);
            state2.assign ((size_t) defaultMaxModes, 0.0f);
        }

        void reset() override
        {
            std::fill (state1.begin(), state1.end(), 0.0f);
            std::fill (state2.begin(), state2.end(), 0.0f);
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }
        int getNumOutputChannels() const noexcept override { return 2; }

        juce::String getTitle() const override { return "Modal Bank"; }
        juce::String getCategory() const override { return "Resonators"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { .id = "excite", .type = SignalType::Signal, .quantity = Quantity::Audio },
                PortDescriptor { .id = "modes", .type = SignalType::Data, .label = "Modes",
                                  .dataTags = { DataTag::ModalSet } },
                PortDescriptor { .id = "pitch", .type = SignalType::Signal, .unit = "st",
                                  .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 60.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Pitch },
                ValueTypes::timeSecondsPort ("resonator.modal.decay", "Decay", 1.5f, 10.0f),
                unipolarPort ("resonator.modal.brightness", "Brightness", 1.0f),
                unipolarPort ("resonator.modal.inharmonicity", "Inharmonicity", 0.0f),
                unipolarPort ("resonator.modal.position", "Position", 0.33f),
                unipolarPort ("resonator.modal.spread", "Spread", 0.5f),
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true, .quantity = Quantity::Audio, .channels = Channels::Stereo },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "resonator.modal.maxModes",
                                       .minValue = 1.0f, .maxValue = (float) defaultMaxModes, .defaultValue = (float) defaultMaxModes,
                                       .displayName = "Max Modes", .isInteger = true, .quantity = Quantity::Count,
                                       .step = 1.0f, .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "resonator.modal.decay")
                storedDecay = value;
            else if (parameterId == "resonator.modal.brightness")
                storedBrightness = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "resonator.modal.inharmonicity")
                storedInharmonicity = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "resonator.modal.position")
                storedPosition = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "resonator.modal.spread")
                storedSpread = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "resonator.modal.maxModes")
                maxModesParam = juce::jlimit (1, defaultMaxModes, (int) std::lround (value));
        }

        void setDataInput (const juce::String& inputPortId, DataPublisher* publisher) noexcept override
        {
            if (inputPortId == "modes")
                modesPublisher = publisher;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = 0.0f;
            outputs[1] = 0.0f;

            if (modesPublisher == nullptr)
                return;

            const auto* buffer = modesPublisher->getCurrentForAudioThread();
            if (buffer == nullptr || buffer->tag() != DataTag::ModalSet || buffer->stride() != 3)
                return;

            const auto activeModeCount = juce::jmin (buffer->length(), maxModesParam, (int) state1.size());
            if (activeModeCount <= 0)
                return;

            const auto pitchSemitones = std::isnan (inputs[2]) ? 60.0f : inputs[2];
            const auto pitchHz = 440.0f * std::pow (2.0f, (pitchSemitones - 69.0f) / 12.0f);
            const auto nominalT60 = juce::jmax (0.001f, std::isnan (inputs[3]) ? storedDecay : inputs[3]);
            const auto brightness = std::isnan (inputs[4]) ? storedBrightness : juce::jlimit (0.0f, 1.0f, inputs[4]);
            const auto inharmonicity = std::isnan (inputs[5]) ? storedInharmonicity : juce::jlimit (0.0f, 1.0f, inputs[5]);
            const auto position = std::isnan (inputs[6]) ? storedPosition : juce::jlimit (0.0f, 1.0f, inputs[6]);
            const auto spread = std::isnan (inputs[7]) ? storedSpread : juce::jlimit (0.0f, 1.0f, inputs[7]);

            const auto inharmonicityB = inharmonicity * 0.001f;
            const auto excite = inputs[0];
            const auto invSqrtActive = 1.0f / std::sqrt ((float) activeModeCount);
            const auto maxFrequency = (float) (sampleRate * 0.45);

            float left = 0.0f, right = 0.0f;

            for (int k = 0; k < activeModeCount; ++k)
            {
                const auto ratio = buffer->at (k, 0);
                const auto ampWeight = buffer->at (k, 1);
                const auto decayWeight = buffer->at (k, 2);

                const auto extraStretch = std::sqrt (1.0f + inharmonicityB * (float) (k + 1) * (float) (k + 1));
                const auto frequency = juce::jlimit (1.0f, maxFrequency, pitchHz * ratio * extraStretch);
                const auto w = juce::MathConstants<float>::twoPi * frequency / (float) sampleRate;
                const auto cosw = std::cos (w);

                const auto brightnessDamping = std::exp (-(1.0f - brightness) * std::pow ((float) k, 1.2f) * 0.1f);
                const auto effectiveT60 = juce::jmax (0.001f, nominalT60 * decayWeight * brightnessDamping);
                const auto r = juce::jlimit (0.0f, 0.999999f, std::exp (std::log (0.001f) / (effectiveT60 * (float) sampleRate)));

                const auto positionWeight = std::fabs (std::sin ((float) (k + 1) * juce::MathConstants<float>::pi * position));
                const auto gain = ampWeight * positionWeight * invSqrtActive;

                const auto y = 2.0f * r * cosw * state1[(size_t) k] - r * r * state2[(size_t) k] + excite * gain;
                state2[(size_t) k] = state1[(size_t) k];
                state1[(size_t) k] = y;

                const auto pan = spread * std::sin ((float) (k + 1) * goldenAngleRad);
                const auto leftGain = (1.0f - pan) * 0.5f;
                const auto rightGain = (1.0f + pan) * 0.5f;

                left += y * leftGain;
                right += y * rightGain;
            }

            outputs[0] = left;
            outputs[1] = right;
        }

    private:
        static PortDescriptor unipolarPort (juce::String id, juce::String label, float defaultValue)
        {
            return PortDescriptor { .id = std::move (id), .type = SignalType::Signal, .label = std::move (label),
                                     .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultValue,
                                     .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar,
                                     .polarity = Polarity::Unipolar };
        }

        double sampleRate = 44100.0;
        int maxModesParam = defaultMaxModes;
        std::vector<float> state1, state2;
        DataPublisher* modesPublisher = nullptr;

        float storedDecay = 1.5f;
        float storedBrightness = 1.0f;
        float storedInharmonicity = 0.0f;
        float storedPosition = 0.33f;
        float storedSpread = 0.5f;
    };
}
