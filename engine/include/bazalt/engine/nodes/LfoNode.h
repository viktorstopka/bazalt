#pragma once

#include "bazalt/engine/graph/HostInputs.h"
#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/telemetry/PhaseSnapshot.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "lfo.shape" (wiki/plans/SoundPalette.md, Batch 1 — v1,
        built-in shapes; the Data(curve) input joins with factory.curve).
        A Control-rate modulation source: sine, triangle, saw (rising), ramp
        (falling), square, sample & hold (a new random value every cycle) and
        smooth random (cosine-glided between random values).

        - `rate` in Hz when free; with `sync` on, `division` (in bars/beats)
          against the host tempo, and while the host is playing the phase is
          locked to the host's position — the LFO lands on the same point of
          the cycle at the same bar every time you press play.
        - `phase` offsets the read position in cycles; `reset` (Event)
          restarts the cycle.
        - `polarity` (structural): bipolar -1..1 or unipolar 0..1 output.

        A phase source (Node::isPhaseSource), so it gets the same phase-locked
        preview as the oscillators — for the random shapes the preview shows
        the most recent values. Random values come from a seeded generator:
        the same patch plays the same "random" sequence.
    */
    class LfoNode : public Node
    {
    public:
        enum class Shape { Sine, Triangle, Saw, Ramp, Square, SampleHold, Smooth };

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            phaseTrack.assign ((size_t) std::max (1, info.maxBlockSize), 0.0f);
            reset();
        }

        void reset() override
        {
            phase = 0.0;
            cycleCount = 0;
            rng = 0x2545F491u;
            for (auto& v : history)
                v = nextRandom();
            current = history[0];
            next = nextRandom();
        }

        bool wantsHostInputs() const noexcept override { return true; }
        void setHostInputs (const HostInputs& inputs) noexcept override
        {
            tempoBpm = inputs.tempoBpm > 0.0 ? inputs.tempoBpm : 120.0;
            hostPlaying = inputs.transportPlaying;
            hostPpq = inputs.ppqPosition;
            hostPositionFresh = true;
        }

        bool isPhaseSource() const noexcept override { return true; }
        const float* getPhaseTrack() const noexcept override { return phaseTrack.data(); }
        void capturePhaseSnapshot (PhaseSnapshot& snapshot) const noexcept override
        {
            snapshot.render = &renderCycle;
            snapshot.frequencyHz = lastFrequency;
            snapshot.sampleRate = sampleRate;
            snapshot.playhead = (float) ((double) cycleCount + phase);
            snapshot.params[0] = (float) shape + (unipolar ? 16.0f : 0.0f);
            snapshot.params[1] = lastPhaseOffset;
            // The four most recent random values, by cycle index mod 4.
            for (int i = 0; i < 4; ++i)
                snapshot.params[2 + i] = history[(size_t) i];
        }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            trackIndex = 0;
            Node::processBlock (inputs, outputs, numSamples);
        }

        int getNumInputPorts() const noexcept override { return 3; }
        int getNumOutputPorts() const noexcept override { return 1; }
        juce::String getTitle() const override { return "LFO"; }
        juce::String getCategory() const override { return "Sources"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "lfo.shape.rate", .type = SignalType::Signal, .label = "Rate", .unit = "Hz",
                                  .minValue = 0.01f, .maxValue = 50.0f, .defaultValue = 1.0f, .isLogScale = true,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency, .curve = Curve::Logarithmic },
                PortDescriptor { .id = "lfo.shape.phase", .type = SignalType::Signal, .label = "Phase",
                                  .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .hasFallbackWhenUnconnected = true,
                                  .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "reset", .type = SignalType::Event, .label = "Reset" },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true,
                                      .minValue = unipolar ? 0.0f : -1.0f, .maxValue = 1.0f,
                                      .quantity = unipolar ? Quantity::Unipolar : Quantity::Bipolar,
                                      .polarity = unipolar ? Polarity::Unipolar : Polarity::Bipolar } };
        }

        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::PhaseLocked, .portId = "out" } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "lfo.shape.shape", .minValue = 0.0f, .maxValue = 6.0f, .defaultValue = 0.0f,
                                       .displayName = "Shape", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "sine", "Sine" }, { "triangle", "Triangle" }, { "saw", "Saw" },
                                                         { "ramp", "Ramp" }, { "square", "Square" },
                                                         { "sampleHold", "S&H" }, { "smooth", "Smooth" } } },
                ParameterDescriptor { .id = "lfo.shape.polarity", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                       .displayName = "Polarity", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "bipolar", "Bipolar" }, { "unipolar", "Unipolar" } }, .isStructural = true },
                ParameterDescriptor { .id = "lfo.shape.sync", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                       .displayName = "Sync", .isInteger = true, .kind = ValueKind::Bool },
                ParameterDescriptor { .id = "lfo.shape.division", .minValue = 0.0f, .maxValue = 8.0f, .defaultValue = 4.0f,
                                       .displayName = "Division", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "8bars", "8 bars" }, { "4bars", "4 bars" }, { "2bars", "2 bars" },
                                                         { "1bar", "1 bar" }, { "1/2", "1/2" }, { "1/4", "1/4" },
                                                         { "1/8", "1/8" }, { "1/16", "1/16" }, { "1/32", "1/32" } } },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "lfo.shape.shape")
                shape = (Shape) juce::jlimit (0, 6, (int) std::lround (value));
            else if (parameterId == "lfo.shape.polarity")
                unipolar = value > 0.5f;
            else if (parameterId == "lfo.shape.sync")
                synced = value > 0.5f;
            else if (parameterId == "lfo.shape.division")
                beatsPerCycle = beatsForDivision (juce::jlimit (0, 8, (int) std::lround (value)));
            else if (parameterId == "lfo.shape.rate")
                storedRate = value;
            else if (parameterId == "lfo.shape.phase")
                storedPhase = value;
        }

        /** The waveform at read position t (0..1), bipolar. */
        static double evaluate (Shape s, double t, float held, float glideFrom, float glideTo) noexcept
        {
            switch (s)
            {
                case Shape::Sine:       return std::sin (juce::MathConstants<double>::twoPi * t);
                case Shape::Triangle:   return t < 0.25 ? 4.0 * t : (t < 0.75 ? 2.0 - 4.0 * t : 4.0 * t - 4.0);
                case Shape::Saw:        return 2.0 * t - 1.0;
                case Shape::Ramp:       return 1.0 - 2.0 * t;
                case Shape::Square:     return t < 0.5 ? 1.0 : -1.0;
                case Shape::SampleHold: return held;
                case Shape::Smooth:     return glideFrom + (glideTo - glideFrom) * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * t));
            }
            return 0.0;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto phaseOffset = std::isnan (inputs[1]) ? storedPhase : inputs[1];
            const auto frequency = synced ? (float) (tempoBpm / 60.0 / beatsPerCycle)
                                          : (std::isnan (inputs[0]) ? storedRate : juce::jlimit (0.001f, 1000.0f, inputs[0]));

            if (std::fabs (inputs[2]) > 0.0f)
                phase = 0.0;
            else if (synced && hostPlaying && hostPositionFresh)
            {
                // Locked to the timeline: the cycle position IS the host position.
                const auto cycles = hostPpq / beatsPerCycle;
                const auto locked = cycles - std::floor (cycles);
                if (locked < phase - 0.5) // the host position wrapped past a cycle
                    advanceCycle();
                phase = locked;
            }
            hostPositionFresh = false;

            if (trackIndex < phaseTrack.size())
                phaseTrack[trackIndex++] = (float) ((double) cycleCount + phase);

            const auto t = phase + (double) phaseOffset - std::floor (phase + (double) phaseOffset);
            const auto bipolar = evaluate (shape, t, current, current, next);
            outputs[0] = (float) (unipolar ? 0.5 * (bipolar + 1.0) : bipolar);

            lastFrequency = frequency;
            lastPhaseOffset = phaseOffset;

            phase += sampleRate > 0.0 ? (double) frequency / sampleRate : 0.0;
            if (phase >= 1.0)
            {
                phase -= std::floor (phase);
                advanceCycle();
            }
        }

    private:
        static double beatsForDivision (int index) noexcept
        {
            constexpr double beats[] = { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
            return beats[index];
        }

        float nextRandom() noexcept
        {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return (float) (int32_t) rng * (1.0f / 2147483648.0f);
        }

        void advanceCycle() noexcept
        {
            cycleCount = (cycleCount + 1) % phaseLockedCycles;
            current = next;
            next = nextRandom();
            history[(size_t) cycleCount] = current;
        }

        static float renderCycle (const PhaseSnapshot& snapshot, double cyclePosition)
        {
            const auto encoded = (int) snapshot.params[0];
            const auto s = (Shape) (encoded & 15);
            const auto isUnipolar = encoded >= 16;
            const auto t = cyclePosition + (double) snapshot.params[1];
            const auto cycle = ((int) std::floor (cyclePosition) % 4 + 4) % 4;
            const auto held = snapshot.params[2 + cycle];
            const auto following = snapshot.params[2 + (cycle + 1) % 4];
            const auto value = evaluate (s, t - std::floor (t), held, held, following);
            return (float) (isUnipolar ? 0.5 * (value + 1.0) : value);
        }

        Shape shape = Shape::Sine;
        bool unipolar = false, synced = false;
        double beatsPerCycle = 2.0;
        double sampleRate = 48000.0, phase = 0.0;
        int cycleCount = 0;
        double tempoBpm = 120.0, hostPpq = 0.0;
        bool hostPlaying = false, hostPositionFresh = false;
        uint32_t rng = 0x2545F491u;
        float current = 0.0f, next = 0.0f;
        std::array<float, 4> history {};
        std::vector<float> phaseTrack;
        size_t trackIndex = 0;
        float lastFrequency = 1.0f, lastPhaseOffset = 0.0f;
        float storedRate = 1.0f, storedPhase = 0.0f;
    };
}
