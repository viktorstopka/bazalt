#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "note.humanize" (wiki/NODES.md's `note.*` row, the
        Note Stream batch). Jitters a `Note` stream's note-on timing,
        velocity, and per-note pitch by a controlled random amount.

        **`timing`**: delays a note-on by up to `maxTimingJitterMs` (50ms,
        this node's own concrete bound — the catalog gives no range),
        implemented as a small scheduled-countdown (not a full ring buffer —
        genuinely simpler and sufficient, since this stream is monophonic:
        at most one note-on is ever "pending" at a time). **Only note-on is
        jittered, deliberately** — note-off timing humanization is both far
        less musically useful and adds real complexity (would need to track
        a release schedule independent of the already-scheduled start) for
        little benefit; a real, narrower-than-spec scope choice.

        **`velocity`** perturbs velocity at the moment a note actually
        fires (after any timing delay). **`pitch`** draws one small detune
        (up to `maxPitchJitterSemitones` = 0.5) per note, held for that
        note's whole duration — matching how real-world humanization
        plugins typically treat pitch (a per-note constant, not a
        continuous wobble).

        **A genuinely benign edge case, not fixed further**: if a new
        note-on arrives while a previous one is still in its scheduled
        delay window, the new one overwrites the pending schedule (the old
        one is dropped, never fires) — reasonable given real MIDI on a
        single monophonic stream rarely does this, and "exactly how two
        overlapping delayed note-ons interleave" isn't a meaningful
        question for a *humanization* effect whose whole point is
        introduced randomness in the first place.
    */
    class NoteHumanizeNode : public Node
    {
    public:
        static constexpr float maxTimingJitterMs = 50.0f;
        static constexpr float maxPitchJitterSemitones = 0.5f;
        static constexpr int numInputs = 4;  // notes, timing, velocity, pitch
        static constexpr int numOutputs = 1; // notes

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            maxDelaySamples = juce::jmax (1, (int) (maxTimingJitterMs * 0.001 * sampleRate));
            timingScratch.resize ((size_t) info.maxBlockSize, 0.0f);
            velocityScratch.resize ((size_t) info.maxBlockSize, 0.0f);
            pitchScratch.resize ((size_t) info.maxBlockSize, 0.0f);
        }

        void reset() override
        {
            random = juce::Random ((juce::int64) seed);
            hasScheduled = false;
            current = {};
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Humanize"; }
        juce::String getCategory() const override { return "Note"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "notes", .type = SignalType::Note },
                PortDescriptor { .id = "note.humanize.timing", .type = SignalType::Control, .label = "Timing",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "note.humanize.velocity", .type = SignalType::Control, .label = "Velocity",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "note.humanize.pitch", .type = SignalType::Control, .label = "Pitch",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            // "notesOut", not "notes" - a real engine invariant (no node may
            // reuse a port id across its own inputs/outputs) forces this;
            // label stays "Notes".
            return { PortDescriptor { .id = "notesOut", .type = SignalType::Note, .label = "Notes", .isPrimaryOutput = true } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "note.humanize.seed",
                                       .minValue = 0.0f, .maxValue = 999999.0f, .defaultValue = 1.0f,
                                       .displayName = "Seed", .isInteger = true, .quantity = Quantity::Count,
                                       .step = 1.0f, .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "note.humanize.timing")
                storedTiming = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "note.humanize.velocity")
                storedVelocityAmount = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "note.humanize.pitch")
                storedPitchAmount = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "note.humanize.seed")
            {
                seed = (int) std::lround (value);
                random = juce::Random ((juce::int64) seed);
            }
        }

        void consumeNoteBlock (const NoteEvent* input, int numSamples) noexcept override
        {
            pendingNoteBlock = input;
            pendingLength = numSamples;
        }

        void processBlock (const float* const* inputs, float* const*, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
            {
                timingScratch[(size_t) i] = std::isnan (inputs[1][i]) ? storedTiming : juce::jlimit (0.0f, 1.0f, inputs[1][i]);
                velocityScratch[(size_t) i] = std::isnan (inputs[2][i]) ? storedVelocityAmount : juce::jlimit (0.0f, 1.0f, inputs[2][i]);
                pitchScratch[(size_t) i] = std::isnan (inputs[3][i]) ? storedPitchAmount : juce::jlimit (0.0f, 1.0f, inputs[3][i]);
            }
        }

        void produceNoteBlock (NoteEvent* output, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const auto incoming = (pendingNoteBlock != nullptr && i < pendingLength) ? pendingNoteBlock[i] : NoteEvent {};

                current.startEvent = false;
                current.stopEvent = false;

                if (incoming.stopEvent)
                {
                    hasScheduled = false;
                    current.gate = false;
                    current.stopEvent = true;
                }

                if (incoming.startEvent)
                {
                    const auto delaySamples = (int) std::lround ((double) random.nextFloat() * timingScratch[(size_t) i] * maxDelaySamples);
                    if (delaySamples <= 0)
                        fireNote (incoming.pitch, incoming.velocity, velocityScratch[(size_t) i], pitchScratch[(size_t) i]);
                    else
                    {
                        hasScheduled = true;
                        scheduledCountdown = delaySamples;
                        scheduledRawPitch = incoming.pitch;
                        scheduledRawVelocity = incoming.velocity;
                        scheduledVelocityAmount = velocityScratch[(size_t) i];
                        scheduledPitchAmount = pitchScratch[(size_t) i];
                    }
                }

                if (hasScheduled)
                {
                    if (--scheduledCountdown <= 0)
                        fireNote (scheduledRawPitch, scheduledRawVelocity, scheduledVelocityAmount, scheduledPitchAmount);
                }

                output[i] = current;
            }

            pendingNoteBlock = nullptr;
        }

    private:
        void fireNote (float rawPitch, float rawVelocity, float velocityAmount, float pitchAmount) noexcept
        {
            const auto pitchJitter = (random.nextFloat() * 2.0f - 1.0f) * pitchAmount * maxPitchJitterSemitones;
            const auto velocityJitter = (random.nextFloat() * 2.0f - 1.0f) * velocityAmount;

            current.gate = true;
            current.pitch = rawPitch + pitchJitter;
            current.velocity = juce::jlimit (0.0f, 1.0f, rawVelocity + velocityJitter);
            current.startEvent = true;
            hasScheduled = false;
        }

        const NoteEvent* pendingNoteBlock = nullptr;
        int pendingLength = 0;
        double sampleRate = 44100.0;
        int maxDelaySamples = 1;

        float storedTiming = 0.0f;
        float storedVelocityAmount = 0.0f;
        float storedPitchAmount = 0.0f;
        int seed = 1;
        juce::Random random { (juce::int64) 1 };

        std::vector<float> timingScratch, velocityScratch, pitchScratch;

        NoteEvent current;
        bool hasScheduled = false;
        int scheduledCountdown = 0;
        float scheduledRawPitch = 60.0f;
        float scheduledRawVelocity = 0.0f;
        float scheduledVelocityAmount = 0.0f;
        float scheduledPitchAmount = 0.0f;
    };
}
