#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/BandLimited.h"
#include "bazalt/engine/telemetry/PhaseSnapshot.h"
#include <vector>
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "osc.analog". One Audio output. Shape stays a plain
        ParameterDescriptor — it's a discrete waveform *selector*
        (isStructural: switching it swaps which PolyBLEP correction table
        renderNextSample() uses), not a continuous value, so modulating it
        at signal rate would just be jarring rather than something a
        listener would call "modulation" (the same reasoning that keeps
        instance.sum/instance.allocate.voice's own mode-style settings static).

        M18 (ADR-0024) added a real "pitch" input port — absolute
        semitones, continuous, so a live pitch-bend needs no special-cased
        path — using the `hasFallbackWhenUnconnected` NaN-sentinel
        `DelayNode.h` established: unconnected reads NaN and this node
        falls back to whatever setParameter("osc.analog.frequency") last
        set; connected, the port's value (converted from semitones to Hz)
        drives the oscillator every sample instead.

        M20 (direct feedback: "there is no reason why ... Oscillator
        Frequency wouldn't be modulatable") adds a second, parallel
        "osc.analog.frequency" *port* alongside pitch (same dotted id the
        old parameter used, so an existing saved patch's stored value still
        applies unchanged) — a direct Hz value rather than pitch's
        semitone-relative-to-a-note framing, for modulating frequency
        directly (an LFO into a drone oscillator with no note-tracking
        upstream, say) without needing a pitch-domain conversion first.
        Pitch wins whenever both are connected (see processSample) since it
        already unconditionally overwrote frequency every sample connected
        or not; neither connected leaves the oscillator at whatever
        setParameter() last configured, exactly like before this change.

        Completed 2026-10-04 (wiki/plans/SoundPalette.md Batch 2, closing
        the MVP): `fine` (cents, on top of pitch/frequency), `pulseWidth`
        (Square only), `phase` (through-zero phase modulation in cycles — it
        offsets the read point, never the running accumulator, the same rule
        as osc.sine), and `sync` (Event: resets the cycle). A real phase
        source, so its phase-locked preview draws what it plays. Every
        shape comes from bandLimited::evaluate, the definition the
        per-shape oscillators and the preview share.
    */
    class OscillatorNode : public Node
    {
    public:
        static constexpr float defaultFrequencyHz = 440.0f; // also osc.analog.frequency's descriptor default
        static constexpr int numInputs = 6; // pitch, frequency, fine, pulseWidth, phase, sync
        static constexpr int numOutputs = 1;

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

        bool isPhaseSource() const noexcept override { return true; }
        const float* getPhaseTrack() const noexcept override { return phaseTrack.data(); }
        void capturePhaseSnapshot (PhaseSnapshot& snapshot) const noexcept override
        {
            snapshot.render = &renderCycle;
            snapshot.frequencyHz = lastFrequency;
            snapshot.sampleRate = sampleRate;
            snapshot.playhead = (float) ((double) cycleCount + phase);
            snapshot.params[0] = (float) waveform;
            snapshot.params[1] = lastPhaseOffset;
            snapshot.params[2] = lastPulseWidth;
        }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            trackIndex = 0;
            Node::processBlock (inputs, outputs, numSamples);
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Oscillator"; }
        juce::String getCategory() const override { return "Generators"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "pitch", .type = SignalType::Control, .unit = "st",
                                  .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 60.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Pitch },
                ValueTypes::frequencyPort ("osc.analog.frequency", "Frequency", defaultFrequencyHz),
                PortDescriptor { .id = "osc.analog.fine", .type = SignalType::Control, .label = "Fine", .unit = "ct",
                                  .minValue = -100.0f, .maxValue = 100.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "osc.analog.pulseWidth", .type = SignalType::Control, .label = "Pulse Width",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "osc.analog.phase", .type = SignalType::Control, .label = "Phase",
                                  .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Bipolar,
                                  .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "sync", .type = SignalType::Event, .label = "Sync" },
            };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Audio, .label = "Out", .isPrimaryOutput = true } };
        }

        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::PhaseLocked, .portId = "out" } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "osc.analog.shape",
                                            .minValue = 0.0f,
                                            .maxValue = 3.0f,
                                            .defaultValue = 1.0f,
                                            .displayName = "Shape",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "sine", "Sine" },
                                                              { "saw", "Saw" },
                                                              { "square", "Square" },
                                                              { "triangle", "Triangle" } },
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "osc.analog.frequency")
                storedFrequency = value;
            else if (parameterId == "osc.analog.shape")
                setWaveform (waveformForShapeValue (value));
            else if (parameterId == "osc.analog.fine")
                storedFine = value;
            else if (parameterId == "osc.analog.pulseWidth")
                storedPulseWidth = value;
            else if (parameterId == "osc.analog.phase")
                storedPhase = value;
        }

        void setWaveform (OscillatorWaveform newWaveform) noexcept { waveform = newWaveform; }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            // NaN means the port is unconnected (the hasFallbackWhenUnconnected
            // sentinel). Pitch (semitones -> Hz) wins whenever connected;
            // frequency (direct Hz) drives it only when pitch isn't.
            auto frequency = ! std::isnan (inputs[0]) ? 440.0f * std::pow (2.0f, (inputs[0] - 69.0f) / 12.0f)
                                                       : (! std::isnan (inputs[1]) ? inputs[1] : storedFrequency);
            const auto fine = std::isnan (inputs[2]) ? storedFine : inputs[2];
            if (fine != 0.0f)
                frequency *= std::exp2 (fine / 1200.0f);
            const auto pulseWidth = std::isnan (inputs[3]) ? storedPulseWidth : inputs[3];
            const auto phaseOffset = std::isnan (inputs[4]) ? storedPhase : inputs[4];

            if (std::fabs (inputs[5]) > 0.0f)
                phase = 0.0;

            if (trackIndex < phaseTrack.size())
                phaseTrack[trackIndex++] = (float) ((double) cycleCount + phase);

            const auto dt = sampleRate > 0.0 ? std::fabs ((double) frequency) / sampleRate : 0.0;
            outputs[0] = (float) bandLimited::evaluate (waveform, bandLimited::wrap (phase + (double) phaseOffset), dt, pulseWidth);

            lastFrequency = frequency;
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
        // "shape" is a macro-automatable stand-in for waveform selection —
        // continuous input, quantized to one of the four band-limited
        // waveforms. Sine=0, Saw=1, Square=2, Triangle=3, matching
        // OscillatorWaveform's declaration order.
        static OscillatorWaveform waveformForShapeValue (float value) noexcept
        {
            const auto index = (int) std::round (std::clamp (value, 0.0f, 3.0f));

            switch (index)
            {
                case 0:  return OscillatorWaveform::Sine;
                case 2:  return OscillatorWaveform::Square;
                case 3:  return OscillatorWaveform::Triangle;
                default: return OscillatorWaveform::Saw;
            }
        }

        static float renderCycle (const PhaseSnapshot& snapshot, double cyclePosition)
        {
            const auto dt = snapshot.sampleRate > 0.0 ? std::fabs (snapshot.frequencyHz) / snapshot.sampleRate : 0.0;
            return (float) bandLimited::evaluate ((OscillatorWaveform) (int) snapshot.params[0],
                                                  bandLimited::wrap (cyclePosition + (double) snapshot.params[1]), dt, snapshot.params[2]);
        }

        OscillatorWaveform waveform = OscillatorWaveform::Saw;
        double sampleRate = 44100.0, phase = 0.0;
        int cycleCount = 0;
        std::vector<float> phaseTrack;
        size_t trackIndex = 0;
        float storedFrequency = defaultFrequencyHz, storedFine = 0.0f, storedPulseWidth = 0.5f, storedPhase = 0.0f;
        float lastFrequency = defaultFrequencyHz, lastPhaseOffset = 0.0f, lastPulseWidth = 0.5f;
    };
}
