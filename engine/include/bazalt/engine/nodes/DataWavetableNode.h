#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/WavetableData.h"
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "data.wavetable" — Wavetable (wiki/plans/
        DataAndWavetable.md 1c, D7): a table of keyframes, drawn or built from
        harmonics in the Factory window, morphing (or stepping) between them.
        Plug it into an Oscillator's Shape: the Oscillator plays the table,
        band-limited, at the frame this node's **Frame** input says — a
        per-sample signal travelling beside the table on the same cable
        (DataPublisher's companion), so modulating Frame sweeps the timbre
        sample-accurately. One cable to the user. */
    class DataWavetableNode : public Node
    {
    public:
        static constexpr const char* frameId = "data.wavetable.frame";

        void prepare (const NodePrepareInfo& info) override
        {
            frames.assign ((size_t) std::max (1, info.maxBlockSize), storedFrame);
            publisher.setCompanion (frames.data(), (int) frames.size());
            if (! published)
                setContent ({});
        }

        void reset() override { std::fill (frames.begin(), frames.end(), storedFrame); }

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return "Wavetable"; }
        juce::String getCategory() const override { return "Data"; }

        bool supportsPerSample() const noexcept override { return false; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = frameId, .type = SignalType::Signal, .label = "Frame",
                                      .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                      .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "table", .type = SignalType::Data, .label = "Table", .isPrimaryOutput = true,
                                      .dataTags = { DataTag::Wavetable } } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == frameId)
                storedFrame = value;
        }

        void processBlock (const float* const* inputs, float* const*, int numSamples) noexcept override
        {
            const auto count = juce::jmin (numSamples, (int) frames.size());
            const auto* in = inputs[0];
            for (int i = 0; i < count; ++i)
            {
                const auto value = in != nullptr && ! std::isnan (in[i]) ? in[i] : storedFrame;
                frames[(size_t) i] = juce::jlimit (0.0f, 1.0f, value);
            }
        }

        /** Message thread. False if every publisher slot is still held. */
        bool setContent (const juce::var& content) override
        {
            document = WavetableDocument::fromVar (content);
            publisher.reclaim();
            const auto ok = publisher.publish (buildWavetableBuffer (document));
            published = published || ok;
            return ok;
        }

        DataPublisher* getDataPublisher() noexcept override { return &publisher; }
        const WavetableDocument& getDocument() const noexcept { return document; }

    private:
        WavetableDocument document;
        DataPublisher publisher;
        std::vector<float> frames;
        float storedFrame = 0.0f;
        bool published = false;
    };
}
