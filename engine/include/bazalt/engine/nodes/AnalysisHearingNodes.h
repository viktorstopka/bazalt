#pragma once

#include "bazalt/engine/AudioAnalysis.h"
#include "bazalt/engine/graph/Node.h"
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Hearing a played instrument (wiki/plans/GuitarInput.md, step 1–2): an
        audio input becomes pitch, attacks, notes and phase — what a guitar
        needs to drive a synth. Shared DSP in AudioAnalysis.h. */

    namespace hearing
    {
        inline ParameterDescriptor noteParameter (const juce::String& id, const juce::String& name, float defaultNote)
        {
            return ParameterDescriptor { .id = id, .minValue = 12.0f, .maxValue = 120.0f, .defaultValue = defaultNote,
                                         .unit = "note", .displayName = name, .isInteger = true, .quantity = Quantity::Pitch };
        }

        inline ParameterDescriptor dbParameter (const juce::String& id, const juce::String& name, float minDb, float maxDb, float defaultDb)
        {
            return ParameterDescriptor { .id = id, .minValue = minDb, .maxValue = maxDb, .defaultValue = defaultDb,
                                         .unit = "dB", .displayName = name, .quantity = Quantity::Gain };
        }

        inline PortDescriptor audioIn()
        {
            return PortDescriptor { .id = "in", .type = SignalType::Signal, .label = "In", .quantity = Quantity::Audio };
        }
    }

    /** Stable type id: "analysis.pitch" — Pitch. What note is playing: YIN
        pitch tracking (PitchTracker), searching only Low..High (one string's
        range makes it fast and keeps it off the wrong octave). Outputs the
        pitch in semitones (MIDI note numbers, fractional — bends and vibrato
        show), the frequency, and a 0..1 confidence (noise and silence read
        low). Values are held between estimates. */
    class AnalysisPitchNode : public Node
    {
    public:
        void prepare (const NodePrepareInfo& info) override
        {
            tracker.prepare (info.sampleRate);
            tracker.setRange (lowNote, highNote);
            tracker.setThreshold (threshold);
        }
        void reset() override { tracker.reset(); }

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 3; }
        juce::String getTitle() const override { return "Pitch"; }
        juce::String getCategory() const override { return "Analysis"; }
        bool supportsPerSample() const noexcept override { return false; }

        std::vector<PortDescriptor> getInputPorts() const override { return { hearing::audioIn() }; }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "pitch", .type = SignalType::Signal, .label = "Pitch", .isPrimaryOutput = true,
                                  .unit = "st", .minValue = 0.0f, .maxValue = 127.0f, .quantity = Quantity::Pitch },
                PortDescriptor { .id = "frequency", .type = SignalType::Signal, .label = "Frequency", .unit = "Hz",
                                  .minValue = 0.0f, .maxValue = 20000.0f, .quantity = Quantity::Frequency },
                PortDescriptor { .id = "confidence", .type = SignalType::Signal, .label = "Confidence",
                                  .minValue = 0.0f, .maxValue = 1.0f, .quantity = Quantity::Unipolar },
            };
        }
        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                hearing::noteParameter ("analysis.pitch.low", "Low Note", 40.0f),   // E2, a guitar's low E
                hearing::noteParameter ("analysis.pitch.high", "High Note", 88.0f), // E6
                ParameterDescriptor { .id = "analysis.pitch.threshold", .minValue = 0.05f, .maxValue = 0.5f, .defaultValue = 0.15f,
                                      .displayName = "Strictness" },
            };
        }
        void setParameter (const juce::String& id, float value) override
        {
            if (id == "analysis.pitch.low")
                lowNote = value;
            else if (id == "analysis.pitch.high")
                highNote = value;
            else if (id == "analysis.pitch.threshold")
                threshold = value;
            tracker.setRange (lowNote, highNote);
            tracker.setThreshold (threshold);
        }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
            {
                tracker.push (inputs[0][i]);
                if (tracker.getConfidence() > 0.0f)
                {
                    heldNote = tracker.getNote();
                    heldFrequency = tracker.getFrequency();
                }
                outputs[0][i] = heldNote;
                outputs[1][i] = heldFrequency;
                outputs[2][i] = tracker.getConfidence();
            }
        }

    private:
        PitchTracker tracker;
        float lowNote = 40.0f, highNote = 88.0f, threshold = 0.15f;
        float heldNote = 60.0f, heldFrequency = 0.0f;
    };

    /** Stable type id: "analysis.onset" — Onset. A pluck, a hit, a new
        attack: an Event the moment one is heard, and its strength (0..1, a
        velocity). Sensitivity is how far the attack must jump above the
        sound so far; Gate ignores anything quieter; Hold is the shortest gap
        between two onsets. */
    class AnalysisOnsetNode : public Node
    {
    public:
        void prepare (const NodePrepareInfo& info) override
        {
            detector.prepare (info.sampleRate);
            apply();
        }
        void reset() override { detector.reset(); }

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 2; }
        juce::String getTitle() const override { return "Onset"; }
        juce::String getCategory() const override { return "Analysis"; }
        bool supportsPerSample() const noexcept override { return false; }

        std::vector<PortDescriptor> getInputPorts() const override { return { hearing::audioIn() }; }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "onset", .type = SignalType::Event, .label = "Onset", .isPrimaryOutput = true },
                PortDescriptor { .id = "strength", .type = SignalType::Signal, .label = "Strength",
                                  .minValue = 0.0f, .maxValue = 1.0f, .quantity = Quantity::Unipolar },
            };
        }
        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                hearing::dbParameter ("analysis.onset.sensitivity", "Sensitivity", 2.0f, 24.0f, 6.0f),
                hearing::dbParameter ("analysis.onset.gate", "Gate", -80.0f, -10.0f, -50.0f),
                ParameterDescriptor { .id = "analysis.onset.hold", .minValue = 10.0f, .maxValue = 500.0f, .defaultValue = 60.0f,
                                      .unit = "ms", .displayName = "Hold", .quantity = Quantity::Time },
            };
        }
        void setParameter (const juce::String& id, float value) override
        {
            if (id == "analysis.onset.sensitivity")
                sensitivityDb = value;
            else if (id == "analysis.onset.gate")
                gateDb = value;
            else if (id == "analysis.onset.hold")
                holdMs = value;
            apply();
        }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const auto onset = detector.push (inputs[0][i]);
                outputs[0][i] = onset ? 1.0f : 0.0f;
                outputs[1][i] = detector.getStrength();
            }
        }

    private:
        void apply() noexcept
        {
            detector.setSensitivityDb (sensitivityDb);
            detector.setGateDb (gateDb);
            detector.setHoldMs (holdMs);
        }

        OnsetDetector detector;
        float sensitivityDb = 6.0f, gateDb = -50.0f, holdMs = 60.0f;
    };

    /** Stable type id: "analysis.notes" — Audio to Notes. A played instrument
        as a Note stream: plug it into a Voice and every pluck plays a voice.
        A note starts at an onset, as soon as the pitch tracker is confident
        about the new pitch (its velocity is the attack's strength); it
        follows bends and vibrato while it rings; it ends when the sound
        fades under the gate or the pitch becomes unclear. A new pluck while
        a note rings ends it and starts the next one. */
    class AnalysisNotesNode : public Node
    {
    public:
        static constexpr double pendingMs = 120.0;  // how long after an onset a confident pitch may still start the note
        static constexpr double releaseMs = 40.0;   // how long the sound must stay faded/unclear to end the note

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            tracker.prepare (sampleRate);
            onset.prepare (sampleRate);
            scratch.assign ((size_t) std::max (1, info.maxBlockSize), NoteEvent {});
            apply();
            reset();
        }
        void reset() override
        {
            tracker.reset();
            onset.reset();
            held = pending = restart = false;
            pendingAge = quietFor = 0;
            heldPitch = 60.0f;
            heldVelocity = 0.0f;
        }

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 1; }
        juce::String getTitle() const override { return "Audio to Notes"; }
        juce::String getCategory() const override { return "Analysis"; }
        bool supportsPerSample() const noexcept override { return false; }

        std::vector<PortDescriptor> getInputPorts() const override { return { hearing::audioIn() }; }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "notes", .type = SignalType::Note, .label = "Notes", .isPrimaryOutput = true } };
        }
        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                hearing::noteParameter ("analysis.notes.low", "Low Note", 40.0f),
                hearing::noteParameter ("analysis.notes.high", "High Note", 88.0f),
                hearing::dbParameter ("analysis.notes.sensitivity", "Sensitivity", 2.0f, 24.0f, 6.0f),
                hearing::dbParameter ("analysis.notes.gate", "Gate", -80.0f, -10.0f, -50.0f),
            };
        }
        void setParameter (const juce::String& id, float value) override
        {
            if (id == "analysis.notes.low")
                lowNote = value;
            else if (id == "analysis.notes.high")
                highNote = value;
            else if (id == "analysis.notes.sensitivity")
                sensitivityDb = value;
            else if (id == "analysis.notes.gate")
                gateDb = value;
            apply();
        }

        void processBlock (const float* const* inputs, float* const*, int numSamples) noexcept override
        {
            const auto pendingLimit = (int) (pendingMs * 0.001 * sampleRate);
            const auto releaseLimit = (int) (releaseMs * 0.001 * sampleRate);
            const auto gate = std::pow (10.0f, gateDb / 20.0f);
            constexpr float confident = 0.7f;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto x = inputs[0][i];
                const auto newEstimate = tracker.push (x);
                if (onset.push (x))
                {
                    pending = true;
                    pendingAge = 0;
                    pendingVelocity = onset.getStrength();
                }

                auto& ev = scratch[(size_t) i];
                ev.startEvent = ev.stopEvent = false;

                if (restart)
                {
                    restart = false;
                    held = true;
                    ev.startEvent = true;
                }
                else if (pending)
                {
                    ++pendingAge;
                    pendingVelocity = std::max (pendingVelocity, std::min (1.0f, onset.getLevel() * 2.0f));
                    // Wait until the analysis window is mostly past the attack.
                    if (newEstimate && pendingAge >= tracker.getLatencySamples() / 2 && tracker.getConfidence() >= confident)
                    {
                        pending = false;
                        heldPitch = tracker.getNote();
                        heldVelocity = pendingVelocity;
                        quietFor = 0;
                        if (held)
                        {
                            ev.stopEvent = true; // end the ringing note now, start the new one next sample
                            restart = true;
                        }
                        else
                        {
                            held = true;
                            ev.startEvent = true;
                        }
                    }
                    else if (pendingAge > pendingLimit)
                        pending = false;
                }

                if (held && ! ev.startEvent && ! restart)
                {
                    const auto clear = tracker.getConfidence() >= confident;
                    if (clear && newEstimate)
                        heldPitch = tracker.getNote(); // bends, vibrato
                    const auto fading = onset.getLevel() < gate || ! clear;
                    quietFor = fading ? quietFor + 1 : 0;
                    if (quietFor > releaseLimit)
                    {
                        held = false;
                        ev.stopEvent = true;
                    }
                }

                ev.gate = held && ! ev.stopEvent;
                ev.pitch = heldPitch;
                ev.velocity = heldVelocity;
            }
        }

        void produceNoteBlock (NoteEvent* output, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
                output[i] = scratch[(size_t) i];
        }

    private:
        void apply() noexcept
        {
            tracker.setRange (lowNote, highNote);
            onset.setSensitivityDb (sensitivityDb);
            onset.setGateDb (gateDb);
            onset.setHoldMs (60.0f);
        }

        PitchTracker tracker;
        OnsetDetector onset;
        std::vector<NoteEvent> scratch;
        double sampleRate = 48000.0;
        float lowNote = 40.0f, highNote = 88.0f, sensitivityDb = 6.0f, gateDb = -50.0f;
        bool held = false, pending = false, restart = false;
        int pendingAge = 0, quietFor = 0;
        float heldPitch = 60.0f, heldVelocity = 0.0f, pendingVelocity = 0.0f;
    };

    /** Stable type id: "analysis.phase" — Track Phase. The phase (0..1 per
        cycle) of the note being played, locked to its zero crossings
        (PhaseTracker): plug it into an Oscillator's Phase with Frequency at 0
        and any drawn shape or wavetable plays in the instrument's own period,
        following every bend, with no note detection in between. Divide 2 is
        a clean octave down, Multiply 3 / Divide 2 a fifth up. Frequency is
        the tracked frequency after the ratio. */
    class AnalysisPhaseNode : public Node
    {
    public:
        void prepare (const NodePrepareInfo& info) override
        {
            tracker.prepare (info.sampleRate);
            apply();
        }
        void reset() override { tracker.reset(); }

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 2; }
        juce::String getTitle() const override { return "Track Phase"; }
        juce::String getCategory() const override { return "Analysis"; }
        bool supportsPerSample() const noexcept override { return false; }

        std::vector<PortDescriptor> getInputPorts() const override { return { hearing::audioIn() }; }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "phase", .type = SignalType::Signal, .label = "Phase", .isPrimaryOutput = true,
                                  .minValue = 0.0f, .maxValue = 1.0f, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "frequency", .type = SignalType::Signal, .label = "Frequency", .unit = "Hz",
                                  .minValue = 0.0f, .maxValue = 20000.0f, .quantity = Quantity::Frequency },
            };
        }
        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "analysis.phase.multiply", .minValue = 1.0f, .maxValue = 16.0f, .defaultValue = 1.0f,
                                      .displayName = "Multiply", .isInteger = true },
                ParameterDescriptor { .id = "analysis.phase.divide", .minValue = 1.0f, .maxValue = 16.0f, .defaultValue = 1.0f,
                                      .displayName = "Divide", .isInteger = true },
                hearing::dbParameter ("analysis.phase.gate", "Gate", -80.0f, -10.0f, -50.0f),
            };
        }
        void setParameter (const juce::String& id, float value) override
        {
            if (id == "analysis.phase.multiply")
                multiply = (int) std::lround (value);
            else if (id == "analysis.phase.divide")
                divide = (int) std::lround (value);
            else if (id == "analysis.phase.gate")
                gateDb = value;
            apply();
        }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
            {
                tracker.push (inputs[0][i]);
                outputs[0][i] = tracker.getPhase();
                outputs[1][i] = tracker.getFrequency();
            }
        }

    private:
        void apply() noexcept
        {
            tracker.setRatio (multiply, divide);
            tracker.setGateDb (gateDb);
        }

        PhaseTracker tracker;
        int multiply = 1, divide = 1;
        float gateDb = -50.0f;
    };
}
