#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "osc.sine" (NODE_CATALOG.md's `osc.*` row: "Cheap,
        alias-free sine for FM, test tones, and modal excitation... exact
        sine from a phase accumulator... must be as cheap as possible
        because FM stacks and modal excitation use many"). Unlike
        `osc.analog`, this is Pattern B (inline DSP, no standalone
        primitive) — a direct `std::sin` call has no band-limiting to get
        wrong, so there's nothing here worth factoring out behind its own
        interface the way `PolyBlepOscillator`'s discontinuity-correction
        math is.

        **In:** `frequency` (Hz, `hasFallbackWhenUnconnected`, default 440,
        range 0.01-20000 — NOT `ValueTypes::frequencyPort`, whose floor is
        20 Hz: an FM carrier or a modal exciter genuinely wants sub-audio
        rates too, this node's whole reason to exist over `osc.analog` is
        being the cheap inner-loop primitive those stacks are built from).
        `phaseMod` (bipolar, default/unconnected = 0 = no modulation, so it
        does NOT use the NaN-fallback pattern — 0 is already the correct
        silent default, unlike `frequency` where 0 would mean "stopped").
        `sync` (Event, "non-zero this sample = fired" — `LogicToggleNode.h`'s
        own documented convention, reused verbatim: resets phase to 0;
        a sync source held non-zero for several samples resets every one of
        them, same accepted behaviour `LogicToggleNode` already documents
        for its own Event inputs, not a new special case here).

        Phase modulation is through-zero and non-destructive: `phaseMod`
        offsets the SAMPLE READ, never the running phase accumulator, so a
        modulation source can't permanently detune the oscillator by
        leaving a nonzero value connected — matching `osc.analog`'s own
        "through-zero phase modulation" framing for its own `phaseMod` port.

        Sample-rate handling (CLAUDE.md rule 6): `phaseIncrement` is
        `frequency / sampleRate`, both members set fresh in `prepare()` —
        no block-size dependence anywhere in `processSample`.
    */
    class SineOscillatorNode : public Node
    {
    public:
        static constexpr float defaultFrequencyHz = 440.0f;
        static constexpr int numInputs = 3; // frequency, phaseMod, sync
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            // Same reasoning as OscillatorNode's own M21 fix: start at the
            // frequency the card DISPLAYS (440 Hz) rather than the silent
            // "frequency unset" state, so a freshly-placed osc.sine with
            // nothing wired to it is audible, not silent DC — matters once a
            // mono (no-allocator) graph can run this node on its own.
            storedFrequency = defaultFrequencyHz;
        }

        void reset() override { phase = 0.0; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Sine"; }
        juce::String getCategory() const override { return "Generators"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "osc.sine.frequency", .type = SignalType::Control, .label = "Frequency",
                                  .unit = "Hz", .minValue = 0.01f, .maxValue = 20000.0f, .defaultValue = defaultFrequencyHz,
                                  .isLogScale = true, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency,
                                  .curve = Curve::Logarithmic },
                PortDescriptor { .id = "phaseMod", .type = SignalType::Control, .label = "Phase Mod",
                                  .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .quantity = Quantity::Bipolar,
                                  .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "sync", .type = SignalType::Event, .label = "Sync" },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::Waveform, .portId = "out" } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "osc.sine.frequency")
                storedFrequency = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto frequency = std::isnan (inputs[0]) ? storedFrequency : inputs[0];
            const auto phaseModAmount = inputs[1]; // no fallback: unconnected already reads 0 (no modulation)

            // sync: "non-zero this sample = fired" (LogicToggleNode.h's convention).
            // Checked BEFORE the increment, so the triggering sample itself renders
            // exactly what a freshly-reset oscillator's first sample would - not
            // one sample later - matching conventional hard-sync behaviour.
            if (std::fabs (inputs[2]) > 0.0f)
                phase = 0.0;

            if (sampleRate > 0.0)
                phase += (double) frequency / sampleRate;
            phase -= std::floor (phase); // wrap to [0, 1)

            const auto readPhase = phase + (double) phaseModAmount;
            outputs[0] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * readPhase);
        }

    private:
        double sampleRate = 44100.0;
        double phase = 0.0;
        float storedFrequency = defaultFrequencyHz;
    };
}
