#pragma once

#include "bazalt/engine/graph/Node.h"
#include <array>
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "data.table" (wiki/NODES.md's `data.*` row, the Data
        Foundations batch). Publishes a `Data(curve)` buffer — the shared,
        hand-drawn curve source the catalog leans on repeatedly (an
        envelope's shape, an LFO's waveform, a waveshaper's transfer
        function, a remap curve): edit it once here, every place it's wired
        in updates.

        **Content shape, an interim simplification**: the catalog's own
        "structural curve content is really the `NodeContent` third category
        once that lands" note (`wiki/NODES.System.md` §3) is exactly the
        situation here — `NodeContent` doesn't exist as real code yet, so
        this node's curve is a fixed bank of up to `maxPoints` (32) plain
        `ParameterDescriptor` points, `data.table.point.0`..`.31`, the same
        pattern `seq.steps`' own step bank already established. `resolution`
        (structural, 2–32) picks how many of those 32 are actually
        published, truncating from the front — raising the cap later is
        trivial, these are individually-numbered parameters, not a
        wire-format array size.

        No RT-safety tension here unlike `data.scale`: this node has **no
        input ports at all** (the catalog's own spec — `data.table` is pure
        content, nothing to modulate), so every rebuild is already and only
        ever triggered by `setParameter()` (ordinary content edits, message-
        thread only) — never something that needs to happen from the audio
        thread in the first place.

        **`loop`** (structural bool) is carried as real metadata on this
        node (matching the catalog) but isn't consumed by anything yet — no
        node reads a `Data(curve)` buffer's "should this wrap" intent today
        beyond `data.lookup`'s own `edgeMode`, which the *reader* controls
        independently. Documented as inert-for-now rather than silently
        assumed to do something.
    */
    class DataTableNode : public Node
    {
    public:
        static constexpr int maxPoints = 32;
        static constexpr int numInputs = 0;
        static constexpr int numOutputs = 1; // data (wasted float slot, same as data.scale)

        void prepare (const NodePrepareInfo&) override { rebuildAndPublish(); }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Table"; }
        juce::String getCategory() const override { return "Data"; }

        bool supportsPerSample() const noexcept override { return false; }
        void processBlock (const float* const*, float* const*, int) noexcept override {}

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "data", .type = SignalType::Data, .label = "Data", .isPrimaryOutput = true,
                                  .dataTags = { DataTag::Curve } },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            std::vector<ParameterDescriptor> parameters = {
                ParameterDescriptor { .id = "data.table.resolution",
                                       .minValue = 2.0f, .maxValue = (float) maxPoints, .defaultValue = 8.0f,
                                       .displayName = "Resolution", .isInteger = true, .quantity = Quantity::Count,
                                       .step = 1.0f, .isStructural = true },
                ParameterDescriptor { .id = "data.table.loop",
                                       .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                       .displayName = "Loop", .kind = ValueKind::Bool, .isStructural = true },
            };

            for (int i = 0; i < maxPoints; ++i)
            {
                parameters.push_back (ParameterDescriptor {
                    .id = "data.table.point." + juce::String (i),
                    .minValue = -1.0f,
                    .maxValue = 1.0f,
                    .defaultValue = 0.0f,
                    .displayName = "Point " + juce::String (i + 1),
                    .quantity = Quantity::Bipolar,
                    .polarity = Polarity::Bipolar,
                    .isStructural = true });
            }

            return parameters;
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "data.table.resolution")
                storedResolution = juce::jlimit (2, maxPoints, (int) std::lround (value));
            else if (parameterId == "data.table.loop")
                loop = value > 0.5f;
            else if (parameterId.startsWith ("data.table.point."))
            {
                const auto index = parameterId.fromLastOccurrenceOf (".", false, false).getIntValue();
                if (index >= 0 && index < maxPoints)
                    points[(size_t) index] = juce::jlimit (-1.0f, 1.0f, value);
            }
            else
                return;

            rebuildAndPublish();
        }

        DataPublisher* getDataPublisher() noexcept override { return &dataPublisher; }

    private:
        void rebuildAndPublish()
        {
            std::vector<float> values (points.begin(), points.begin() + storedResolution);
            dataPublisher.publish (std::make_unique<DataBuffer> (DataTag::Curve, std::move (values), 1));
        }

        DataPublisher dataPublisher;
        std::array<float, maxPoints> points {};
        int storedResolution = 8;
        bool loop = false;
    };
}
