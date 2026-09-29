#pragma once

#include "bazalt/engine/graph/Node.h"
#include <array>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "note.value" (wiki/NODES.md's `note.*` row, the Note
        Stream batch). The catalog's own framing: "the mono-domain way to
        read a note stream. Inside an instanced region, the allocator's own
        outputs are used instead."

        **`pressure`/`slide` are not built** — `NoteEvent` (M18, ADR-0024)
        is "deliberately narrower than the full catalog... add a field once
        something can actually drive it," and nothing upstream produces
        pressure/slide today (the same reasoning `InstanceVoiceNode.h`
        already gives for omitting them from its own ports). Only
        `pitch`/`velocity` are real.

        **`select` (last/lowest/highest/first) has genuine meaning here**,
        even though the engine's real note sources (`io.noteIn`) are
        currently strictly monophonic one voice at a time: this node
        maintains its own small internal memory (up to `maxHeld` = 8
        concurrently-held notes, tracked by watching the incoming stream's
        own start/stop edges over time) rather than only ever looking at
        "whatever's live this sample" — a mono-domain patch feeding several
        overlapping notes into one `notes` stream (the catalog's own use
        case for this node) gets real, correct last/lowest/highest/first
        behaviour once such a source exists, not just when `io.noteIn`
        itself eventually supports it.
    */
    class NoteValueNode : public Node
    {
    public:
        static constexpr int maxHeld = 8;
        static constexpr int numInputs = 1;  // notes
        static constexpr int numOutputs = 2; // pitch, velocity

        enum class Select { Last, Lowest, Highest, First };

        void reset() override
        {
            heldCount = 0;
            lastPitch = 60.0f;
            lastVelocity = 0.0f;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Note Value"; }
        juce::String getCategory() const override { return "Note"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "notes", .type = SignalType::Note } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "pitch", .type = SignalType::Control, .isPrimaryOutput = true,
                                  .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 60.0f, .quantity = Quantity::Pitch },
                PortDescriptor { .id = "velocity", .type = SignalType::Control,
                                  .quantity = Quantity::Unipolar, .polarity = Polarity::Unipolar },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "note.value.select",
                                       .minValue = 0.0f, .maxValue = 3.0f, .defaultValue = 0.0f,
                                       .displayName = "Select", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "last", "Last" }, { "lowest", "Lowest" },
                                                         { "highest", "Highest" }, { "first", "First" } },
                                       .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "note.value.select")
                select = (Select) juce::jlimit (0, 3, (int) std::lround (value));
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
                {
                    lastPitch = note.pitch;
                    lastVelocity = note.velocity;

                    if (heldCount < maxHeld)
                        ++heldCount;
                    else // full: drop the oldest to make room, FIFO
                        for (int h = 0; h < maxHeld - 1; ++h)
                        {
                            heldPitches[(size_t) h] = heldPitches[(size_t) h + 1];
                            heldVelocities[(size_t) h] = heldVelocities[(size_t) h + 1];
                        }

                    heldPitches[(size_t) (heldCount - 1)] = note.pitch;
                    heldVelocities[(size_t) (heldCount - 1)] = note.velocity;
                }

                if (note.stopEvent)
                    removeHeld (note.pitch);

                outputs[0][i] = selectedPitch();
                outputs[1][i] = selectedVelocity();
            }

            pendingNoteBlock = nullptr;
        }

    private:
        void removeHeld (float pitch) noexcept
        {
            for (int h = 0; h < heldCount; ++h)
            {
                if (std::fabs (heldPitches[(size_t) h] - pitch) < 1.0e-3f)
                {
                    for (int k = h; k < heldCount - 1; ++k)
                    {
                        heldPitches[(size_t) k] = heldPitches[(size_t) k + 1];
                        heldVelocities[(size_t) k] = heldVelocities[(size_t) k + 1];
                    }
                    --heldCount;
                    return;
                }
            }
        }

        int indexForSelect() const noexcept
        {
            if (heldCount <= 0)
                return -1;

            switch (select)
            {
                case Select::First:
                    return 0;
                case Select::Lowest:
                case Select::Highest:
                {
                    auto best = 0;
                    for (int h = 1; h < heldCount; ++h)
                    {
                        const auto better = select == Select::Lowest ? heldPitches[(size_t) h] < heldPitches[(size_t) best]
                                                                      : heldPitches[(size_t) h] > heldPitches[(size_t) best];
                        if (better)
                            best = h;
                    }
                    return best;
                }
                case Select::Last:
                default:
                    return heldCount - 1;
            }
        }

        float selectedPitch() const noexcept
        {
            const auto index = indexForSelect();
            return index >= 0 ? heldPitches[(size_t) index] : lastPitch;
        }

        float selectedVelocity() const noexcept
        {
            const auto index = indexForSelect();
            return index >= 0 ? heldVelocities[(size_t) index] : lastVelocity;
        }

        const NoteEvent* pendingNoteBlock = nullptr;
        int pendingLength = 0;
        Select select = Select::Last;

        std::array<float, maxHeld> heldPitches {};
        std::array<float, maxHeld> heldVelocities {};
        int heldCount = 0;
        float lastPitch = 60.0f;
        float lastVelocity = 0.0f;
    };
}
