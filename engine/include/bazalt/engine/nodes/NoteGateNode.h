#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "note.gate" (wiki/NODES.md's `note.*` row, the Note
        Stream batch). Turns a `Note` stream's start/stop edges and gate
        level into ordinary Event/Boolean/Control signals every other
        family in this catalog already knows how to use.

        `count` (not elaborated by the catalog): a plain running tally of
        note-on events seen since the last `reset()` — this node's own
        concrete design, useful for anything that wants to react every Nth
        note (feed it into `time.divide`, or `math.modulo` for a repeating
        pattern).
    */
    class NoteGateNode : public Node
    {
    public:
        static constexpr int numInputs = 1;  // notes
        static constexpr int numOutputs = 4; // noteOn, noteOff, gate, count

        void reset() override { count = 0; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Note Gate"; }
        juce::String getCategory() const override { return "Notes"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "notes", .type = SignalType::Note } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "noteOn", .type = SignalType::Event, .label = "Note On", .isPrimaryOutput = true },
                PortDescriptor { .id = "noteOff", .type = SignalType::Event, .label = "Note Off" },
                PortDescriptor { .id = "gate", .type = SignalType::Signal, .label = "Gate", .kind = ValueKind::Bool, .quantity = Quantity::Boolean },
                PortDescriptor { .id = "count", .type = SignalType::Signal, .label = "Count",
                                  .minValue = 0.0f, .isInteger = true, .quantity = Quantity::Count },
            };
        }

        void consumeNoteBlock (const NoteEvent* input, int numSamples) noexcept override
        {
            pendingNoteBlock = input;
            pendingLength = numSamples;
        }

        void processBlock (const float* const*, float* const* outputs, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const auto note = (pendingNoteBlock != nullptr && i < pendingLength) ? pendingNoteBlock[i] : NoteEvent {};

                if (note.startEvent)
                    ++count;

                outputs[0][i] = note.startEvent ? 1.0f : 0.0f;
                outputs[1][i] = note.stopEvent ? 1.0f : 0.0f;
                outputs[2][i] = note.gate ? 1.0f : 0.0f;
                outputs[3][i] = (float) count;
            }

            pendingNoteBlock = nullptr;
        }

    private:
        const NoteEvent* pendingNoteBlock = nullptr;
        int pendingLength = 0;
        int count = 0;
    };
}
