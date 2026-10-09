#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "note.filter" (wiki/NODES.md's `note.*` row, the
        Note Stream batch). Gates a `Note` stream by pitch/velocity range.

        **A real, deliberate redesign from the catalog's literal port
        shape, not a silent narrowing**: the catalog specifies two `Note`
        OUTPUTS, `pass`/`reject`. `ExecutionPlan::BlockStep` has exactly one
        `noteOutputBufferIndex` field (singular) — a node can produce at
        most one `Note` output today, a real, previously-unexercised engine
        limit this batch is the first to actually hit (see `wiki/
        MILESTONES.md`'s own Note Stream batch entry for the full
        reasoning, including why `note.hold`/`note.select`/`note.chord`
        were deferred entirely rather than forced through the same wall).

        This node's own concrete resolution: **one** `Note` output (the
        note, verbatim, when in range; fully suppressed — gate held false,
        no start/stop, per-sample pitch/velocity frozen — when out of
        range) plus a plain `inRange` Boolean carrying the pass/reject
        *decision* as an ordinary signal. This captures the real, useful
        behaviour (a keyboard split, a velocity gate) a two-`Note`-output
        design would have offered, without pretending the engine can carry
        two simultaneous note streams off one node today.
    */
    class NoteFilterNode : public Node
    {
    public:
        static constexpr int numInputs = 5;  // notes, lowPitch, highPitch, lowVelocity, highVelocity
        static constexpr int numOutputs = 2; // notes, inRange

        void prepare (const NodePrepareInfo& info) override
        {
            // Plain float, not std::vector<bool> (whose bitset specialization's
            // proxy-reference semantics are an unnecessary surprise here) -
            // matches this codebase's usual scratch-buffer idiom elsewhere.
            inRangeScratch.resize ((size_t) info.maxBlockSize, 1.0f);
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Note Filter"; }
        juce::String getCategory() const override { return "Notes"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "notes", .type = SignalType::Note },
                ValueTypes::midiNotePort ("note.filter.lowPitch", "Low Pitch", 0.0f),
                ValueTypes::midiNotePort ("note.filter.highPitch", "High Pitch", 127.0f),
                PortDescriptor { .id = "note.filter.lowVelocity", .type = SignalType::Signal, .label = "Low Velocity",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "note.filter.highVelocity", .type = SignalType::Signal, .label = "High Velocity",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                // "notesOut", not "notes" - a real engine invariant (no node
                // may reuse a port id across its own inputs/outputs, same as
                // clock.divide's own "tickOut") forces this; label stays "Notes".
                PortDescriptor { .id = "notesOut", .type = SignalType::Note, .label = "Notes", .isPrimaryOutput = true },
                PortDescriptor { .id = "inRange", .type = SignalType::Signal, .label = "In Range", .kind = ValueKind::Bool, .quantity = Quantity::Boolean },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "note.filter.lowPitch")
                storedLowPitch = value;
            else if (parameterId == "note.filter.highPitch")
                storedHighPitch = value;
            else if (parameterId == "note.filter.lowVelocity")
                storedLowVelocity = value;
            else if (parameterId == "note.filter.highVelocity")
                storedHighVelocity = value;
        }

        void consumeNoteBlock (const NoteEvent* input, int numSamples) noexcept override
        {
            pendingNoteBlock = input;
            pendingLength = numSamples;
        }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const auto lowPitch = std::isnan (inputs[1][i]) ? storedLowPitch : inputs[1][i];
                const auto highPitch = std::isnan (inputs[2][i]) ? storedHighPitch : inputs[2][i];
                const auto lowVelocity = std::isnan (inputs[3][i]) ? storedLowVelocity : inputs[3][i];
                const auto highVelocity = std::isnan (inputs[4][i]) ? storedHighVelocity : inputs[4][i];

                const auto note = (pendingNoteBlock != nullptr && i < pendingLength) ? pendingNoteBlock[i] : NoteEvent {};
                const auto inRange = note.pitch >= lowPitch && note.pitch <= highPitch
                                      && note.velocity >= lowVelocity && note.velocity <= highVelocity;

                inRangeScratch[(size_t) i] = inRange ? 1.0f : 0.0f;
                outputs[1][i] = inRangeScratch[(size_t) i];
            }
        }

        void produceNoteBlock (NoteEvent* output, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const auto note = (pendingNoteBlock != nullptr && i < pendingLength) ? pendingNoteBlock[i] : NoteEvent {};
                output[i] = inRangeScratch[(size_t) i] > 0.5f ? note : NoteEvent {}; // suppressed: gate false, no start/stop
            }

            pendingNoteBlock = nullptr;
        }

    private:
        const NoteEvent* pendingNoteBlock = nullptr;
        int pendingLength = 0;
        float storedLowPitch = 0.0f;
        float storedHighPitch = 127.0f;
        float storedLowVelocity = 0.0f;
        float storedHighVelocity = 1.0f;
        std::vector<float> inRangeScratch;
    };
}
