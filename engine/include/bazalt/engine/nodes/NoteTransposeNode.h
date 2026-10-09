#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "note.transpose" (wiki/NODES.md's `note.*` row, the
        Note Stream batch). Shifts a `Note` stream's pitch by
        `semitones + octaves*12`, otherwise passing every field through
        unchanged (gate/velocity/start/stop untouched).

        **The consume-then-produce shape every Note-transforming node in
        this batch shares**: `consumeNoteBlock()` stores the incoming
        per-sample array; `processBlock()` runs next (per `ExecutionPlan::
        process()`'s own fixed ordering: consume -> processBlock -> produce)
        and captures this block's `semitones`/`octaves` into a small
        `prepare()`-sized scratch buffer, since `produceNoteBlock()` (which
        runs last) has no access to the ordinary float `inputs[]` array
        itself — only `NoteEvent*`. This is what makes `semitones`/`octaves`
        genuinely audio-rate-modulatable rather than only settable via
        `setParameter()` (contrast `data.scale`'s `root`, which — for a real,
        different, RT-safety reason documented on that node — can't be).
    */
    class NoteTransposeNode : public Node
    {
    public:
        static constexpr int numInputs = 3;  // notes, semitones, octaves
        static constexpr int numOutputs = 1; // notes

        void prepare (const NodePrepareInfo& info) override
        {
            semitonesScratch.resize ((size_t) info.maxBlockSize, 0.0f);
            octavesScratch.resize ((size_t) info.maxBlockSize, 0.0f);
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Transpose"; }
        juce::String getCategory() const override { return "Note"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "notes", .type = SignalType::Note },
                ValueTypes::pitchPort ("note.transpose.semitones", "Semitones"),
                PortDescriptor { .id = "note.transpose.octaves", .type = SignalType::Signal, .label = "Octaves",
                                  .minValue = -10.0f, .maxValue = 10.0f, .defaultValue = 0.0f, .isInteger = true,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Count, .step = 1.0f },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            // "notesOut", not "notes" - a real engine invariant (no node may
            // reuse a port id across its own inputs/outputs, same as
            // clock.divide's own "tickOut") forces this; label stays "Notes".
            return { PortDescriptor { .id = "notesOut", .type = SignalType::Note, .label = "Notes", .isPrimaryOutput = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "note.transpose.semitones")
                storedSemitones = value;
            else if (parameterId == "note.transpose.octaves")
                storedOctaves = value;
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
                semitonesScratch[(size_t) i] = std::isnan (inputs[1][i]) ? storedSemitones : inputs[1][i];
                octavesScratch[(size_t) i] = std::isnan (inputs[2][i]) ? storedOctaves : inputs[2][i];
            }
        }

        void produceNoteBlock (NoteEvent* output, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
            {
                auto note = (pendingNoteBlock != nullptr && i < pendingLength) ? pendingNoteBlock[i] : NoteEvent {};
                note.pitch += semitonesScratch[(size_t) i] + octavesScratch[(size_t) i] * 12.0f;
                output[i] = note;
            }

            pendingNoteBlock = nullptr;
        }

    private:
        const NoteEvent* pendingNoteBlock = nullptr;
        int pendingLength = 0;
        float storedSemitones = 0.0f;
        float storedOctaves = 0.0f;
        std::vector<float> semitonesScratch;
        std::vector<float> octavesScratch;
    };
}
