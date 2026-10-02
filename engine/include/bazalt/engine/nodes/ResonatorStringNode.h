#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "resonator.string" (wiki/NODES.md's `resonator.*`
        row, the PM Core batch) — "a waveguide string, the playable version
        of Karplus-Strong." A real digital waveguide: a circular delay line
        of length `sampleRate/pitch` closed through a loop filter (damping
        one-pole + a stiffness allpass), continuously fed by `excite` —
        `resonator.comb`'s own feedback topology, generalized with a second
        filter stage and a real playable-instrument `pitch` input instead of
        a raw `frequency` knob.

        **The loop, per sample**: `tapped = buffer[readIndex]` →
        `damped = damping·tapped + (1-damping)·dampedPrev` (the SAME
        "Damping" convention `resonator.comb`/`filter.onepole` already
        establish: `0` = darkest, `1` = brightest) → a single-stage allpass
        (`stiffness`-controlled coefficient, a simplified one-stage version
        of the classic Jaffe-Smith dispersion allpass CHAIN — a real,
        documented reduction, not the full cascade) → scaled by `loopGain`
        (solved from `decay`, the nominal 60dB time, by the same per-round-
        trip decay-time formula `resonator.comb`'s own feedback mode uses,
        just parametrized by TIME instead of a raw gain — the "playable
        instrument" framing this node's catalog entry asks for) →
        `y[n] = excite[n] + loopSignal`, written back into the budget.
        No fractional-delay interpolation (plain integer sample indexing),
        the same simplification `DelayNode.h`/`resonator.comb` already make.

        **`position`** (pickup point): a plain difference tap against the
        SAME real delay line, `out = tapped - 0.5·buffer[readIndex -
        round(position·delaySamples)]` — a textbook pickup-position comb
        (an electric guitar pickup placed at a fractional point along the
        string nulls harmonics whose wavelength matches that offset); unlike
        `excite.pluck`'s own `position` (a FIXED reference window, since
        that node doesn't know the eventual pitch), this one is scaled to
        the string's REAL, live delay length.

        **`release` is a real, documented design call the catalog names but
        doesn't define — a `bool` gate, not a knob.** `true` (held): the
        string rings normally, decaying only at the rate `decay` sets.
        `false` (released): an extra damping multiplier smoothly ramps from
        `1.0` toward a floor of `0.4` over ~15ms, same the instrument
        behaviour of lifting a finger or palm-muting a real string —
        combined with the loop's own already-present decay, this makes the
        string die out convincingly fast rather than ringing out its full
        `decay` time regardless of whether it's still "held." Unconnected
        reads as held (`true`) — a string you never explicitly release just
        rings out on its own `decay`, matching every other resonator in this
        batch that has no release concept at all.

        **`motion`**: the freshly computed loop value at the injection point
        this very sample (`y[n]`, before it's written into the buffer) —
        what `excite.stickSlip`/`excite.mallet`'s own `feedback` input reads
        to model a coupled exciter feeling the string push back (a real,
        literal graph cycle once wired that way — `GraphCompiler.cpp`'s
        existing per-sample-region/SCC mechanism, not a new one, see this
        batch's own `wiki/MILESTONES.md` entry).
    */
    class ResonatorStringNode : public Node
    {
    public:
        static constexpr float minFrequencyHz = 20.0f;
        static constexpr float releaseSmoothingMs = 15.0f;
        static constexpr float releasedFloor = 0.4f;
        static constexpr int numInputs = 7;  // excite, pitch, decay, damping, stiffness, position, release
        static constexpr int numOutputs = 2; // out, motion

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            maxDelaySamples = juce::jmax (8, (int) std::ceil (sampleRate / minFrequencyHz) + 4);
            buffer.assign ((size_t) maxDelaySamples, 0.0f);
            writeIndex = 0;
            dampedState = 0.0f;
            allpassInPrev = 0.0f;
            allpassOutPrev = 0.0f;
            releaseEnvelope = 1.0f;
            releaseSmoothingCoeff = std::exp (-1.0f / ((releaseSmoothingMs * 0.001f) * (float) sampleRate));
        }

        void reset() override
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writeIndex = 0;
            dampedState = 0.0f;
            allpassInPrev = 0.0f;
            allpassOutPrev = 0.0f;
            releaseEnvelope = 1.0f;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "String"; }
        juce::String getCategory() const override { return "Resonators"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { "excite", SignalType::Audio },
                PortDescriptor { .id = "pitch", .type = SignalType::Control, .unit = "st",
                                  .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 60.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Pitch },
                PortDescriptor { .id = "resonator.string.decay", .type = SignalType::Control, .label = "Decay",
                                  .unit = "s", .minValue = 0.05f, .maxValue = 30.0f, .defaultValue = 3.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Time,
                                  .curve = Curve::Logarithmic },
                unipolarPort ("resonator.string.damping", "Damping", 0.5f),
                unipolarPort ("resonator.string.stiffness", "Stiffness", 0.1f),
                unipolarPort ("resonator.string.position", "Position", 0.15f),
                PortDescriptor { .id = "resonator.string.release", .type = SignalType::Boolean, .label = "Release",
                                  .defaultValue = 1.0f, .hasFallbackWhenUnconnected = true },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = SignalType::Audio, .label = "Out", .isPrimaryOutput = true },
                PortDescriptor { .id = "motion", .type = SignalType::Audio, .label = "Motion" },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "resonator.string.decay")
                storedDecay = value;
            else if (parameterId == "resonator.string.damping")
                storedDamping = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "resonator.string.stiffness")
                storedStiffness = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "resonator.string.position")
                storedPosition = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "resonator.string.release")
                storedRelease = value > 0.5f;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto pitchSemitones = std::isnan (inputs[1]) ? 60.0f : inputs[1];
            const auto frequency = juce::jmax (minFrequencyHz, 440.0f * std::pow (2.0f, (pitchSemitones - 69.0f) / 12.0f));
            const auto decaySeconds = juce::jmax (0.001f, std::isnan (inputs[2]) ? storedDecay : inputs[2]);
            const auto damping = std::isnan (inputs[3]) ? storedDamping : juce::jlimit (0.0f, 1.0f, inputs[3]);
            const auto stiffness = std::isnan (inputs[4]) ? storedStiffness : juce::jlimit (0.0f, 1.0f, inputs[4]);
            const auto position = std::isnan (inputs[5]) ? storedPosition : juce::jlimit (0.0f, 1.0f, inputs[5]);
            const auto held = std::isnan (inputs[6]) ? storedRelease : inputs[6] > 0.5f;

            const auto delaySamples = juce::jlimit (2, maxDelaySamples - 2, (int) std::lround (sampleRate / (double) frequency));
            const auto loopGain = juce::jlimit (0.0f, 0.999999f,
                                                 std::exp (std::log (0.001f) * (float) delaySamples / (decaySeconds * (float) sampleRate)));
            const auto allpassG = stiffness * 0.5f;

            const auto releaseTarget = held ? 1.0f : releasedFloor;
            releaseEnvelope += (releaseTarget - releaseEnvelope) * (1.0f - releaseSmoothingCoeff);

            auto readIndex = writeIndex - delaySamples;
            if (readIndex < 0)
                readIndex += maxDelaySamples;

            const auto tapped = buffer[(size_t) readIndex];

            dampedState = damping * tapped + (1.0f - damping) * dampedState;

            const auto allpassOut = -allpassG * dampedState + allpassInPrev + allpassG * allpassOutPrev;
            allpassInPrev = dampedState;
            allpassOutPrev = allpassOut;

            const auto loopSignal = allpassOut * loopGain * releaseEnvelope;
            const auto y = inputs[0] + loopSignal;

            buffer[(size_t) writeIndex] = y;

            const auto positionOffset = juce::jlimit (1, delaySamples - 1, (int) std::lround (position * (float) delaySamples));
            auto positionIndex = readIndex - positionOffset;
            if (positionIndex < 0)
                positionIndex += maxDelaySamples;
            const auto positionTapped = buffer[(size_t) positionIndex];

            outputs[0] = tapped - 0.5f * positionTapped;
            outputs[1] = y;

            writeIndex = (writeIndex + 1) % maxDelaySamples;
        }

    private:
        static PortDescriptor unipolarPort (juce::String id, juce::String label, float defaultValue)
        {
            return PortDescriptor { .id = std::move (id), .type = SignalType::Control, .label = std::move (label),
                                     .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultValue,
                                     .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar,
                                     .polarity = Polarity::Unipolar };
        }

        double sampleRate = 44100.0;
        int maxDelaySamples = 8;
        std::vector<float> buffer;
        int writeIndex = 0;
        float dampedState = 0.0f;
        float allpassInPrev = 0.0f, allpassOutPrev = 0.0f;
        float releaseEnvelope = 1.0f;
        float releaseSmoothingCoeff = 0.0f;

        float storedDecay = 3.0f;
        float storedDamping = 0.5f;
        float storedStiffness = 0.1f;
        float storedPosition = 0.15f;
        bool storedRelease = true;
    };
}
