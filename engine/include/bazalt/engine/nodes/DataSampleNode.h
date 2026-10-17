#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/SampleData.h"
#include <atomic>

namespace bazalt::engine::nodes
{
    /** Stable type id: "data.sample" — Sample (wiki/plans/Sampling.md 3a.1).
        One sound from the sample library, with a non-destructive region
        (start / end / loop / fades / gain / reverse / normalise) as its content,
        published as `Data(sample)` for players (the Sampler, the Convolver,
        Cycle Cuts) to read. The file itself never changes.

        The content names the sample by id; the samples themselves live in the
        process-wide SampleStore, filled by the plugin's library. A sample that
        isn't loaded (yet) publishes one silent frame and reports itself
        missing — the patch keeps working, and the node is republished when the
        sample arrives (GraphEditController::refreshSamples). */
    class DataSampleNode : public Node
    {
    public:
        void prepare (const NodePrepareInfo&) override
        {
            if (! published)
                publishRegion();
        }

        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 2; }
        juce::String getTitle() const override { return "Sample"; }
        juce::String getCategory() const override { return "Data"; }

        bool supportsPerSample() const noexcept override { return false; }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "sample", .type = SignalType::Data, .label = "Sample", .isPrimaryOutput = true,
                                      .dataTags = { DataTag::Sample } },
                     PortDescriptor { .id = "length", .type = SignalType::Signal, .label = "Length", .unit = "s",
                                      .quantity = Quantity::Time } };
        }

        bool setContent (const juce::var& content) override
        {
            region = SampleRegion::fromVar (content);
            return publishRegion();
        }

        DataPublisher* getDataPublisher() noexcept override { return &publisher; }

        void processBlock (const float* const*, float* const* outputs, int numSamples) noexcept override
        {
            std::fill (outputs[1], outputs[1] + numSamples, lengthSeconds.load (std::memory_order_relaxed));
        }

        /** Message thread: the region as last set, and whether its sample was
            found in the store when it was published. */
        const SampleRegion& getRegion() const noexcept { return region; }
        bool isMissing() const noexcept { return missing; }

    private:
        bool publishRegion()
        {
            const auto source = region.sampleId.isNotEmpty() ? SampleStore::instance().find (region.sampleId) : nullptr;
            missing = region.sampleId.isNotEmpty() && source == nullptr;
            auto buffer = buildSampleBuffer (source.get(), region, (uint8_t) Quantity::Audio);
            lengthSeconds.store (source != nullptr && buffer->sampleRate() > 0.0 ? (float) ((double) buffer->frames() / buffer->sampleRate()) : 0.0f,
                                 std::memory_order_relaxed);
            publisher.reclaim();
            const auto ok = publisher.publish (std::move (buffer));
            published = published || ok;
            return ok;
        }

        SampleRegion region;
        DataPublisher publisher;
        std::atomic<float> lengthSeconds { 0.0f }; // read by the audio thread; set by a (re)publish on the message thread
        bool published = false;
        bool missing = false;
    };
}
