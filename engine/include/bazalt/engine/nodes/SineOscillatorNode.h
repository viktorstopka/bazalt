#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/BandLimited.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** The four classic shapes — the same enum osc.analog's
        PolyBlepOscillator uses, so every oscillator shares one definition. */
    using BasicWaveform = OscillatorWaveform;

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

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            phaseTrack.assign ((size_t) std::max (1, info.maxBlockSize), 0.0f);
        }

        void reset() override
        {
            phase = 0.0;
            cycleCount = 0;
        }

        // ---- Phase source (PreviewKind::PhaseLocked) ------------------------
        bool isPhaseSource() const noexcept override { return true; }
        const float* getPhaseTrack() const noexcept override { return phaseTrack.data(); }

        void capturePhaseSnapshot (PhaseSnapshot& snapshot) const noexcept override
        {
            snapshot.render = &renderCycle;
            snapshot.frequencyHz = lastFrequency;
            snapshot.sampleRate = sampleRate;
            snapshot.playhead = (float) ((double) cycleCount + phase);
            snapshot.params[0] = (float) waveform;
            snapshot.params[1] = lastAmplitude;
            snapshot.params[2] = lastPhaseOffset;
            snapshot.params[3] = lastPulseWidth;
        }

        /** The oscillator's output as a function of its read phase — the ONE
            definition both processSample() and the preview use, so the
            preview is what the node actually produces, band-limiting
            included. */
        static double evaluate (BasicWaveform shape, double t, double dt, double pulseWidth) noexcept
        {
            return bandLimited::evaluate (shape, t, dt, pulseWidth);
        }

        /** Rebuilds each block's per-sample phase track around the default
            per-sample loop. */
        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            trackIndex = 0;
            Node::processBlock (inputs, outputs, numSamples);
        }

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

        juce::String getCategory() const override { return "Sources"; }

        /** "osc.sine", "osc.saw", ... — the prefix every port/parameter id of
            this node uses. */
        juce::String typeId() const { return "osc." + getTitle().toLowerCase(); }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            std::vector<PortDescriptor> ports {
                PortDescriptor { .id = frequencyId, .type = SignalType::Signal, .label = "Frequency",
                                 .unit = "Hz", .minValue = 0.01f, .maxValue = 20000.0f, .defaultValue = defaultFrequencyHz,
                                 .isLogScale = true, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency,
                                 .curve = Curve::Logarithmic },
                PortDescriptor { .id = amplitudeId, .type = SignalType::Signal, .label = "Amplitude",
                                 .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f,
                                 .hasFallbackWhenUnconnected = true },
                PortDescriptor { .id = phaseId, .type = SignalType::Signal, .label = "Phase",
                                 .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                 .hasFallbackWhenUnconnected = true, .quantity = Quantity::Bipolar,
                                 .polarity = Polarity::Bipolar },
            };
            if (hasPulseWidth())
                ports.push_back (PortDescriptor { .id = pulseWidthId, .type = SignalType::Signal, .label = "Pulse Width",
                                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultPulseWidth,
                                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar });
            ports.push_back (PortDescriptor { .id = "sync", .type = SignalType::Event, .label = "Sync" });
            return ports;
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true, .quantity = Quantity::Audio } };
        }

        /** Phase-locked: its own waveform at its current parameters,
            evaluated from capturePhaseSnapshot() (PreviewKind::PhaseLocked). */
        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::PhaseLocked, .portId = "out" } };
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

            if (trackIndex < phaseTrack.size())
                phaseTrack[trackIndex++] = (float) ((double) cycleCount + phase);

            const auto dt = sampleRate > 0.0 ? std::fabs ((double) frequency) / sampleRate : 0.0;
            const auto t = bandLimited::wrap (phase + (double) phaseOffset);
            outputs[0] = (float) (evaluate (waveform, t, dt, pulseWidth) * (double) amplitude);

            lastFrequency = frequency;
            lastAmplitude = amplitude;
            lastPhaseOffset = phaseOffset;
            lastPulseWidth = pulseWidth;

            if (sampleRate > 0.0)
            {
                phase += (double) frequency / sampleRate;
                const auto wraps = std::floor (phase);
                if (wraps != 0.0)
                {
                    phase -= wraps;
                    cycleCount = (int) (((long long) cycleCount + (long long) wraps) % phaseLockedCycles + phaseLockedCycles) % phaseLockedCycles;
                }
            }
        }

    private:
        bool hasPulseWidth() const noexcept { return waveform == BasicWaveform::Square; }

        static float renderCycle (const PhaseSnapshot& snapshot, double cyclePosition)
        {
            const auto dt = snapshot.sampleRate > 0.0 ? std::fabs (snapshot.frequencyHz) / snapshot.sampleRate : 0.0;
            const auto t = bandLimited::wrap (cyclePosition + (double) snapshot.params[2]);
            return (float) (evaluate ((BasicWaveform) (int) snapshot.params[0], t, dt, snapshot.params[3]) * (double) snapshot.params[1]);
        }

        BasicWaveform waveform;
        juce::String frequencyId, amplitudeId, phaseId, pulseWidthId;
        double sampleRate = 44100.0;
        double phase = 0.0;
        int cycleCount = 0; // completed cycles mod phaseLockedCycles — the preview's cycle index
        std::vector<float> phaseTrack;
        size_t trackIndex = 0;
        float lastFrequency = defaultFrequencyHz, lastAmplitude = 1.0f, lastPhaseOffset = 0.0f, lastPulseWidth = defaultPulseWidth;
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
