#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>
#include <limits>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "note.quantize" (wiki/NODES.md's `note.*` row, the
        Note Stream batch). Snaps a `Note` stream's pitch onto whatever
        `data.scale` publishes — the flagship consumer the Data Foundations
        batch was built for: `io.noteIn -> note.quantize <- data.scale`
        `-> instance.allocate.voice` is the catalog's own reference patch
        #2 ("MIDI remapped to a scale"), now genuinely buildable.

        **`root` here is a *second*, independent knob from `data.scale`'s
        own `root`** — a real, deliberate design choice, not a redundant
        duplicate: `data.scale` already rotates its published pitch-classes
        by its own root (so the buffer is a self-contained, nameable scale
        like "F major"), while this node's `root` is a plain post-
        quantization semitone offset — the same "same scale shape, movable
        key centre without touching the scale's own definition" knob real
        hardware/software quantizer modules commonly have on the quantizer
        itself, independent of the scale table it reads from.

        **Assumes a standard 12-semitone octave** for pitch reconstruction
        (`octaveBase = floor(pitch/12)`), matching `Pitch`'s own canonical
        unit throughout this engine ("60 = middle C"). A `data.scale` wired
        in with `octaveSize != 12` publishes degree values outside the
        0-11 range this node's octave math expects — not a crash, just not
        meaningfully quantizable against absolute pitch this way. A real
        fix (generalizing this node's own octave assumption) is future
        work, not attempted here.

        **`applyTo`**: `continuous` re-quantizes every sample (so pitch
        bend/glide tracks the scale live); `onNoteOnOnly` computes the
        target once, at the note's own `startEvent`, and holds it for the
        note's whole duration regardless of later pitch changes — both are
        real, common quantizer behaviours, not a spec detail this node
        guesses at silently.
    */
    class NoteQuantizeNode : public Node
    {
    public:
        static constexpr int numInputs = 4;  // notes, scale, root, strength
        static constexpr int numOutputs = 1; // notes

        enum class Direction { Nearest, Up, Down };
        enum class ApplyTo { Continuous, OnNoteOnOnly };

        void prepare (const NodePrepareInfo& info) override
        {
            rootScratch.resize ((size_t) info.maxBlockSize, 0.0f);
            strengthScratch.resize ((size_t) info.maxBlockSize, 1.0f);
        }

        void reset() override { heldQuantizedPitch = 60.0f; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Scale Quantize"; }
        juce::String getCategory() const override { return "Note"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "notes", .type = SignalType::Note },
                PortDescriptor { .id = "scale", .type = SignalType::Data, .label = "Scale", .dataTags = { DataTag::Scale } },
                PortDescriptor { .id = "note.quantize.root", .type = SignalType::Control, .label = "Root",
                                  .unit = "st", .minValue = -60.0f, .maxValue = 60.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Pitch, .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "note.quantize.strength", .type = SignalType::Control, .label = "Strength",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f,
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
                ParameterDescriptor { .id = "note.quantize.direction",
                                       .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 0.0f,
                                       .displayName = "Direction", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "nearest", "Nearest" }, { "up", "Up" }, { "down", "Down" } },
                                       .isStructural = true },
                ParameterDescriptor { .id = "note.quantize.applyTo",
                                       .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                       .displayName = "Apply To", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "continuous", "Continuous" }, { "onNoteOnOnly", "On Note-On Only" } },
                                       .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "note.quantize.root")
                storedRoot = value;
            else if (parameterId == "note.quantize.strength")
                storedStrength = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "note.quantize.direction")
                direction = (Direction) juce::jlimit (0, 2, (int) std::lround (value));
            else if (parameterId == "note.quantize.applyTo")
                applyTo = std::lround (value) == 1 ? ApplyTo::OnNoteOnOnly : ApplyTo::Continuous;
        }

        void setDataInput (const juce::String& inputPortId, DataPublisher* publisher) noexcept override
        {
            if (inputPortId == "scale")
                scalePublisher = publisher;
        }

        void consumeNoteBlock (const NoteEvent* input, int numSamples) noexcept override
        {
            pendingNoteBlock = input;
            pendingLength = numSamples;
        }

        void processBlock (const float* const* inputs, float* const*, int numSamples) noexcept override
        {
            currentScale = scalePublisher != nullptr ? scalePublisher->getCurrentForAudioThread() : nullptr;

            for (int i = 0; i < numSamples; ++i)
            {
                rootScratch[(size_t) i] = std::isnan (inputs[2][i]) ? storedRoot : inputs[2][i];
                strengthScratch[(size_t) i] = std::isnan (inputs[3][i]) ? storedStrength : juce::jlimit (0.0f, 1.0f, inputs[3][i]);
            }
        }

        void produceNoteBlock (NoteEvent* output, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
            {
                auto note = (pendingNoteBlock != nullptr && i < pendingLength) ? pendingNoteBlock[i] : NoteEvent {};

                if (applyTo == ApplyTo::OnNoteOnOnly)
                {
                    if (note.startEvent)
                        heldQuantizedPitch = quantizeToScale (note.pitch);
                    note.pitch = note.pitch + (heldQuantizedPitch - note.pitch) * strengthScratch[(size_t) i];
                }
                else
                {
                    const auto target = quantizeToScale (note.pitch);
                    note.pitch = note.pitch + (target - note.pitch) * strengthScratch[(size_t) i];
                }

                note.pitch += rootScratch[(size_t) i];
                output[i] = note;
            }

            pendingNoteBlock = nullptr;
        }

    private:
        float quantizeToScale (float pitch) const noexcept
        {
            if (currentScale == nullptr || currentScale->length() <= 0)
                return pitch; // nothing published to quantize against - passthrough

            const auto length = currentScale->length();
            const auto octaveBase = (int) std::floor (pitch / 12.0f);

            auto best = pitch;
            auto bestDist = std::numeric_limits<float>::max();

            for (int octaveOffset = -1; octaveOffset <= 1; ++octaveOffset)
            {
                for (int d = 0; d < length; ++d)
                {
                    const auto candidate = (float) ((octaveBase + octaveOffset) * 12) + currentScale->at (d);
                    const auto diff = candidate - pitch;

                    if (direction == Direction::Up && diff < 0.0f)
                        continue;
                    if (direction == Direction::Down && diff > 0.0f)
                        continue;

                    const auto dist = std::fabs (diff);
                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        best = candidate;
                    }
                }
            }

            return best;
        }

        const NoteEvent* pendingNoteBlock = nullptr;
        int pendingLength = 0;
        DataPublisher* scalePublisher = nullptr;
        const DataBuffer* currentScale = nullptr;

        float storedRoot = 0.0f;
        float storedStrength = 1.0f;
        Direction direction = Direction::Nearest;
        ApplyTo applyTo = ApplyTo::Continuous;
        float heldQuantizedPitch = 60.0f;

        std::vector<float> rootScratch, strengthScratch;
    };
}
