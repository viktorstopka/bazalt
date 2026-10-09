#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "data.lookup" (wiki/NODES.md's `data.*` row, the
        Data Foundations batch). The reader half of this batch: turns
        whatever `data.scale`/`data.table` publish back into an ordinary,
        live `Control` value — `data.scale`/`data.table` **produce**, this
        node **consumes**, closing the round trip.

        **Mode semantics, this node's own concrete design** (the catalog
        names the four modes but not their exact contract):
        - `nearest`/`interpolate`: `in` is a normalised bipolar **position**
          (-1..1 → 0..1, wiki/plans/PropsAndMacroRedesign.md Batch D — "mod
          values are always bipolar," replacing the old per-node Unipolar/
          Bipolar selector) then scaled across the buffer's own length.
        - `index`/`wrapIndex`: `in` is a **literal element index** — read
          directly, no position remap applies (an index has no natural
          "normalised" meaning). `wrapIndex` always wraps regardless of
          `edgeMode`; plain `index` respects it (clamp or wrap).
        - `edgeMode` (clamp/wrap) governs what happens at the buffer's own
          edges for every mode except `wrapIndex`, which is unconditional
          wrapping by definition.

        **`dataB`/`morph`**: if `dataB` is wired and its tag matches
        `data`'s, the output is a linear blend between the two buffers'
        looked-up values; a tag mismatch (or nothing wired) silently falls
        back to `data` alone — a deliberate, documented graceful
        degradation, not a compile-time requirement (nothing in this engine
        enforces "required" ports today; see `data`'s own field comment).

        **Only `stride() == 1` buffers are meaningfully supported** — the
        two real producers this batch ships (`Curve`, `Scale`) are both
        stride 1. A future stride>1 tag (e.g. `ModalSet`) would read raw
        interleaved floats through this node today, not a per-element
        value — not crash-unsafe, just not meaningfully generalized yet;
        `PortDescriptor::dataTags`' own doc comment already anticipates a
        proper generic Data-preview/reader node as future work.
    */
    class DataLookupNode : public Node
    {
    public:
        static constexpr int numInputs = 4;  // in, data, dataB, morph
        static constexpr int numOutputs = 1; // out

        enum class Mode { Nearest, Interpolate, Index, WrapIndex };
        enum class EdgeMode { Clamp, Wrap };

        void reset() override { currentA = nullptr; currentB = nullptr; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Lookup"; }
        juce::String getCategory() const override { return "Data"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "in", .type = SignalType::Signal, .label = "In" },
                PortDescriptor { .id = "data", .type = SignalType::Data, .label = "Data",
                                  .dataTags = { DataTag::Curve, DataTag::Scale } },
                PortDescriptor { .id = "dataB", .type = SignalType::Data, .label = "Data B",
                                  .dataTags = { DataTag::Curve, DataTag::Scale } },
                PortDescriptor { .id = "data.lookup.morph", .type = SignalType::Signal, .label = "Morph",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { .id = "out", .type = SignalType::Signal } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "data.lookup.mode",
                                       .minValue = 0.0f, .maxValue = 3.0f, .defaultValue = 1.0f, // "interpolate"
                                       .displayName = "Mode", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "nearest", "Nearest" }, { "interpolate", "Interpolate" },
                                                         { "index", "Index" }, { "wrapIndex", "Wrap Index" } },
                                       .isStructural = true },
                ParameterDescriptor { .id = "data.lookup.edgeMode",
                                       .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                       .displayName = "Edge Mode", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "clamp", "Clamp" }, { "wrap", "Wrap" } },
                                       .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "data.lookup.morph")
                storedMorph = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "data.lookup.mode")
                mode = (Mode) juce::jlimit (0, 3, (int) std::lround (value));
            else if (parameterId == "data.lookup.edgeMode")
                edgeMode = std::lround (value) == 1 ? EdgeMode::Wrap : EdgeMode::Clamp;
        }

        void setDataInput (const juce::String& inputPortId, DataPublisher* publisher) noexcept override
        {
            if (inputPortId == "data")
                dataPublisherA = publisher;
            else if (inputPortId == "dataB")
                dataPublisherB = publisher;
        }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            // Fetched once per block, not per sample — a Data buffer never
            // changes mid-block (only ever swapped between blocks by a
            // discrete edit), and getCurrentForAudioThread() is cheap but
            // there's still no reason to call it more than once here.
            currentA = dataPublisherA != nullptr ? dataPublisherA->getCurrentForAudioThread() : nullptr;
            currentB = dataPublisherB != nullptr ? dataPublisherB->getCurrentForAudioThread() : nullptr;
            Node::processBlock (inputs, outputs, numSamples);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            if (currentA == nullptr)
            {
                outputs[0] = 0.0f;
                return;
            }

            const auto in = inputs[0];
            const auto morph = std::isnan (inputs[3]) ? storedMorph : juce::jlimit (0.0f, 1.0f, inputs[3]);

            const auto valueA = sampleAt (*currentA, in);

            if (currentB != nullptr && currentB->tag() == currentA->tag())
            {
                const auto valueB = sampleAt (*currentB, in);
                outputs[0] = valueA + (valueB - valueA) * morph;
            }
            else
            {
                outputs[0] = valueA;
            }
        }

    private:
        static int wrapOrClampIndex (int idx, int length, EdgeMode edge) noexcept
        {
            if (length <= 0)
                return 0;
            if (edge == EdgeMode::Wrap)
                return ((idx % length) + length) % length;
            return juce::jlimit (0, length - 1, idx);
        }

        float sampleAt (const DataBuffer& buffer, float in) const noexcept
        {
            const auto length = buffer.length();
            if (length <= 0)
                return 0.0f;

            switch (mode)
            {
                case Mode::Index:
                {
                    const auto idx = wrapOrClampIndex ((int) std::lround (in), length, edgeMode);
                    return buffer.at (idx);
                }
                case Mode::WrapIndex:
                {
                    const auto idx = ((((int) std::lround (in)) % length) + length) % length;
                    return buffer.at (idx);
                }
                case Mode::Nearest:
                {
                    // Batch D: position is always bipolar now (-1..1 -> 0..1).
                    const auto pos01 = in * 0.5f + 0.5f;
                    const auto raw = pos01 * (float) (length - 1);
                    const auto idx = wrapOrClampIndex ((int) std::lround (raw), length, edgeMode);
                    return buffer.at (idx);
                }
                case Mode::Interpolate:
                default:
                {
                    const auto pos01 = in * 0.5f + 0.5f;
                    const auto raw = pos01 * (float) (length - 1);
                    const auto lower = wrapOrClampIndex ((int) std::floor (raw), length, edgeMode);
                    const auto upper = wrapOrClampIndex (lower + 1, length, edgeMode);
                    const auto frac = raw - std::floor (raw);
                    return buffer.at (lower) + (buffer.at (upper) - buffer.at (lower)) * frac;
                }
            }
        }

        DataPublisher* dataPublisherA = nullptr;
        DataPublisher* dataPublisherB = nullptr;
        const DataBuffer* currentA = nullptr;
        const DataBuffer* currentB = nullptr;
        float storedMorph = 0.0f;
        Mode mode = Mode::Interpolate;
        EdgeMode edgeMode = EdgeMode::Clamp;
    };
}
