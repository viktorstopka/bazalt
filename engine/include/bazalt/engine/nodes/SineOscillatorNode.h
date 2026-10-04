#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Band-limited single-cycle shapes, evaluated statelessly from a read
        phase `t` in [0, 1) and the per-sample phase increment `dt`. Stateless
        on purpose: the Phase port offsets the READ point, so anything that
        integrated the waveform over time (PolyBlepOscillator's leaky-
        integrated triangle) would drift whenever phase is modulated.

        Saw and square correct their jumps with PolyBLEP; triangle has no
        jumps, only corners, so it corrects them with PolyBLAMP (the
        integrated BLEP). Same 2-sample polynomial residuals as
        PolyBlepOscillator.cpp — the project's existing band-limiting floor.
    */
    namespace bandLimited
    {
        /** Residual for a unit step at t = 0 (PolyBlepOscillator::polyBlep). */
        inline double blep (double t, double dt) noexcept
        {
            if (dt <= 0.0)
                return 0.0;
            if (t < dt)
            {
                t /= dt;
                return t + t - t * t - 1.0;
            }
            if (t > 1.0 - dt)
            {
                t = (t - 1.0) / dt;
                return t * t + t + t + 1.0;
            }
            return 0.0;
        }

        /** Residual for a slope change at t = 0 (the integral of blep);
            `0.5 * slopeChange * dt * blamp` corrects a corner whose slope
            changes by `slopeChange` per unit phase. */
        inline double blamp (double t, double dt) noexcept
        {
            if (dt <= 0.0)
                return 0.0;
            if (t < dt)
            {
                t = t / dt - 1.0;
                return -t * t * t / 3.0;
            }
            if (t > 1.0 - dt)
            {
                t = (t - 1.0) / dt + 1.0;
                return t * t * t / 3.0;
            }
            return 0.0;
        }

        inline double wrap (double t) noexcept { return t - std::floor (t); }

        inline double saw (double t, double dt) noexcept
        {
            return 2.0 * t - 1.0 - blep (t, dt); // rises -1 -> 1, drops by 2 at t = 0
        }

        /** High for t < width, low after; rising edge at 0, falling at width. */
        inline double square (double t, double dt, double width) noexcept
        {
            const auto naive = t < width ? 1.0 : -1.0;
            return naive + blep (t, dt) - blep (wrap (t - width), dt);
        }

        /** 1 at t = 0, -1 at t = 0.5: slope -4 then +4, so the corner at 0
            changes slope by -8 and the one at 0.5 by +8. This blamp's
            polynomial already carries a factor of 2, hence half of each
            slope change (verified numerically against the Nyquist-truncated
            Fourier series — SineOscillatorNodeTests.cpp). */
        inline double triangle (double t, double dt) noexcept
        {
            const auto naive = 4.0 * std::fabs (t - 0.5) - 1.0;
            return naive - 4.0 * dt * blamp (t, dt) + 4.0 * dt * blamp (wrap (t - 0.5), dt);
        }
    }

    enum class BasicWaveform
    {
        Sine,
        Saw,
        Square,
        Triangle
    };

    /** Stable type ids: "osc.sine", "osc.saw", "osc.square", "osc.triangle"
        — one node per classic waveform, all sharing one port set and layout:

        - Frequency (Hz, value row, default 440, 0.01-20000 — sub-audio on
          purpose: FM carriers and modal exciters are built from these).
        - Amplitude (value row, default 1.00) — scales the output.
        - Phase (Modulation/Bipolar row, default 0.00): an ordinary
          modulatable port with its own inline value, in cycles. Through-zero
          and non-destructive — it offsets the sample READ, never the running
          accumulator, so a value left connected can't detune the oscillator.
          Replaces osc.sine's old bare "phaseMod" input (2026-10-04).
        - Pulse Width (Square only, Unipolar, default 0.5, clamped 0.01-0.99).
        - Sync (Event, "!"): non-zero this sample resets the phase to 0
          (LogicToggleNode.h's own Event convention).
        - Out (Audio).

        All four are band-limited (`bandLimited` above); Sine is exact
        `std::sin`, which has nothing to alias. Every value port uses the
        NaN-fallback pattern (`hasFallbackWhenUnconnected`), so an unwired
        port reads its own inline value. Sample-rate handling (CLAUDE.md rule
        6): the increment is derived from `sampleRate`, set in prepare(); no
        block-size dependence anywhere.

        `osc.analog` (one node, switchable shape, pitch input, voice-ready)
        stays as it is; these are the minimal per-shape primitives.
    */
    class BasicOscillatorNode : public Node
    {
    public:
        static constexpr float defaultFrequencyHz = 440.0f;
        static constexpr float defaultPulseWidth = 0.5f;

        explicit BasicOscillatorNode (BasicWaveform waveformToUse)
            : waveform (waveformToUse),
              frequencyId (typeId() + ".frequency"),
              amplitudeId (typeId() + ".amplitude"),
              phaseId (typeId() + ".phase"),
              pulseWidthId (typeId() + ".pulseWidth")
        {
        }

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }
        void reset() override { phase = 0.0; }

        int getNumInputPorts() const noexcept override { return hasPulseWidth() ? 5 : 4; } // frequency, amplitude, phase, [pulseWidth], sync
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override
        {
            switch (waveform)
            {
                case BasicWaveform::Sine:     return "Sine";
                case BasicWaveform::Saw:      return "Saw";
                case BasicWaveform::Square:   return "Square";
                case BasicWaveform::Triangle: return "Triangle";
            }
            return {};
        }

        juce::String getCategory() const override { return "Generators"; }

        /** "osc.sine", "osc.saw", ... — the prefix every port/parameter id of
            this node uses. */
        juce::String typeId() const { return "osc." + getTitle().toLowerCase(); }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            std::vector<PortDescriptor> ports {
                PortDescriptor { .id = frequencyId, .type = SignalType::Control, .label = "Frequency",
                                 .unit = "Hz", .minValue = 0.01f, .maxValue = 20000.0f, .defaultValue = defaultFrequencyHz,
                                 .isLogScale = true, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency,
                                 .curve = Curve::Logarithmic },
                PortDescriptor { .id = amplitudeId, .type = SignalType::Control, .label = "Amplitude",
                                 .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f,
                                 .hasFallbackWhenUnconnected = true },
                PortDescriptor { .id = phaseId, .type = SignalType::Control, .label = "Phase",
                                 .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                 .hasFallbackWhenUnconnected = true, .quantity = Quantity::Bipolar,
                                 .polarity = Polarity::Bipolar },
            };
            if (hasPulseWidth())
                ports.push_back (PortDescriptor { .id = pulseWidthId, .type = SignalType::Control, .label = "Pulse Width",
                                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultPulseWidth,
                                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar });
            ports.push_back (PortDescriptor { .id = "sync", .type = SignalType::Event, .label = "Sync" });
            return ports;
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Audio, .label = "Out", .isPrimaryOutput = true } };
        }

        // Placeholder preview — rebuilt in the oscillator-preview task.
        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::Waveform, .portId = "out", .timeWindowSeconds = 0.015f } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            // Compared against ids built once in the constructor: this may run
            // on the audio thread (macro automation), which must not allocate.
            if (parameterId == frequencyId)
                storedFrequency = value;
            else if (parameterId == amplitudeId)
                storedAmplitude = value;
            else if (parameterId == phaseId)
                storedPhase = value;
            else if (parameterId == pulseWidthId)
                storedPulseWidth = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto frequency = std::isnan (inputs[0]) ? storedFrequency : inputs[0];
            const auto amplitude = std::isnan (inputs[1]) ? storedAmplitude : inputs[1];
            const auto phaseOffset = std::isnan (inputs[2]) ? storedPhase : inputs[2];
            const auto pulseWidth = hasPulseWidth() ? (std::isnan (inputs[3]) ? storedPulseWidth : inputs[3]) : defaultPulseWidth;
            const auto syncIndex = hasPulseWidth() ? 4 : 3;

            if (std::fabs (inputs[syncIndex]) > 0.0f)
                phase = 0.0;

            const auto dt = sampleRate > 0.0 ? std::fabs ((double) frequency) / sampleRate : 0.0;
            const auto t = bandLimited::wrap (phase + (double) phaseOffset);

            double value = 0.0;
            switch (waveform)
            {
                case BasicWaveform::Sine:     value = std::sin (juce::MathConstants<double>::twoPi * t); break;
                case BasicWaveform::Saw:      value = bandLimited::saw (t, dt); break;
                case BasicWaveform::Square:   value = bandLimited::square (t, dt, std::clamp ((double) pulseWidth, 0.01, 0.99)); break;
                case BasicWaveform::Triangle: value = bandLimited::triangle (t, dt); break;
            }
            outputs[0] = (float) (value * (double) amplitude);

            if (sampleRate > 0.0)
                phase = bandLimited::wrap (phase + (double) frequency / sampleRate);
        }

    private:
        bool hasPulseWidth() const noexcept { return waveform == BasicWaveform::Square; }

        BasicWaveform waveform;
        juce::String frequencyId, amplitudeId, phaseId, pulseWidthId;
        double sampleRate = 44100.0;
        double phase = 0.0;
        float storedFrequency = defaultFrequencyHz;
        float storedAmplitude = 1.0f;
        float storedPhase = 0.0f;
        float storedPulseWidth = defaultPulseWidth;
    };

    struct SineOscillatorNode : BasicOscillatorNode { SineOscillatorNode() : BasicOscillatorNode (BasicWaveform::Sine) {} };
    struct SawOscillatorNode : BasicOscillatorNode { SawOscillatorNode() : BasicOscillatorNode (BasicWaveform::Saw) {} };
    struct SquareOscillatorNode : BasicOscillatorNode { SquareOscillatorNode() : BasicOscillatorNode (BasicWaveform::Square) {} };
    struct TriangleOscillatorNode : BasicOscillatorNode { TriangleOscillatorNode() : BasicOscillatorNode (BasicWaveform::Triangle) {} };
}
