#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/WavetableData.h"
#include "bazalt/engine/graph/WavetableExtraction.h"
#include <atomic>
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
        sample-accurately. One cable to the user.

        **Capture** (wiki/plans/Sampling.md §3.3): with a sample wired into
        `source` (typically a live Record), each `capture` Event takes the last
        quarter second of it, and the message thread turns that into a new
        table — four frames along it, by Cycles (Texture when it has no pitch)
        — "the synth takes on my guitar's timbre whenever I hit the
        footswitch". The audio thread only copies the snapshot; a captured
        table is playing state, not saved: editing the table or reopening the
        patch brings the drawn one back. */
    class DataWavetableNode : public Node
    {
    public:
        static constexpr const char* frameId = "data.wavetable.frame";

        static constexpr double captureSeconds = 0.25;

        void prepare (const NodePrepareInfo& info) override
        {
            snapshot.assign ((size_t) std::lround (captureSeconds * info.sampleRate), 0.0f); // the most a capture can hold
            frames.assign ((size_t) std::max (1, info.maxBlockSize), storedFrame);
            publisher.setCompanion (frames.data(), (int) frames.size());
            if (! published)
                setContent ({});
        }

        void reset() override { std::fill (frames.begin(), frames.end(), storedFrame); }

        int getNumInputPorts() const noexcept override { return 3; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return "Wavetable"; }
        juce::String getCategory() const override { return "Data"; }

        bool supportsPerSample() const noexcept override { return false; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = frameId, .type = SignalType::Signal, .label = "Frame",
                                      .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                      .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
                     PortDescriptor { .id = "source", .type = SignalType::Data, .label = "Source",
                                      .dataTags = { DataTag::Sample, DataTag::CycleCuts } },
                     PortDescriptor { .id = "capture", .type = SignalType::Event, .label = "Capture" } };
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

        void setDataInput (const juce::String& portId, DataPublisher* publisher) noexcept override
        {
            if (portId == "source")
                source = publisher;
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

            // A capture: copy the last moment of the source (the message thread
            // does the rest). One at a time — a capture asked for while the last
            // one is still being turned into a table is dropped.
            const auto* capture = inputs[2];
            if (source == nullptr || capture == nullptr || snapshotReady.load (std::memory_order_acquire))
                return;
            for (int s = 0; s < numSamples; ++s)
                if (capture[s] > 0.0f)
                {
                    takeSnapshot (s);
                    break;
                }
        }

        /** Message thread: a waiting snapshot becomes the table that plays. */
        void serviceOnMessageThread() override
        {
            if (! snapshotReady.load (std::memory_order_acquire))
                return;
            SampleSource taken;
            taken.sampleRate = snapshotRate;
            taken.frames = snapshotFrames;
            taken.data.assign (snapshot.begin(), snapshot.begin() + snapshotFrames);
            snapshotReady.store (false, std::memory_order_release);

            wavetableExtraction::Options options;
            options.frames = 4;
            options.method = wavetableExtraction::Method::Cycles;
            auto extracted = wavetableExtraction::fromSample (taken, options);
            if (! extracted.success)
            {
                options.method = wavetableExtraction::Method::Texture;
                extracted = wavetableExtraction::fromSample (taken, options);
            }
            if (extracted.success)
            {
                extracted.document.interpolation = document.interpolation;
                publisher.reclaim();
                publisher.publish (buildWavetableBuffer (extracted.document));
                ++capturesTaken;
            }
        }

        /** How many captures have become tables (tests, the body later). */
        int getCapturesTaken() const noexcept { return capturesTaken; }

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
        /** Audio thread: the newest captureSeconds of the source as of sample
            `s` of this block, oldest first, mono. */
        void takeSnapshot (int s) noexcept
        {
            const auto* buffer = source->getCurrentForAudioThread();
            if (buffer == nullptr || buffer->frames() < 1 || snapshot.empty())
                return;
            auto window = DataBuffer::Window { 0, buffer->frames(), 0 };
            if (buffer->live != nullptr)
                window = buffer->windowFor (source->readCompanion (s, 0.0f));
            const auto channels = buffer->channels();
            const auto take = juce::jmin ((int) snapshot.size(), window.length - window.firstReadable);
            if (take < 64)
                return;
            const auto* data = buffer->rawData();
            for (int i = 0; i < take; ++i)
            {
                auto p = window.origin + window.length - take + i; // the newest `take` frames
                if (p >= buffer->frames())
                    p -= buffer->frames();
                float sum = 0.0f;
                for (int c = 0; c < channels; ++c)
                    sum += data[(size_t) p * (size_t) channels + (size_t) c];
                snapshot[(size_t) i] = sum / (float) channels;
            }
            snapshotFrames = take;
            snapshotRate = buffer->sampleRate() > 0.0 ? buffer->sampleRate() : 48000.0;
            snapshotReady.store (true, std::memory_order_release);
        }

        DataPublisher* source = nullptr;
        std::vector<float> snapshot;
        int snapshotFrames = 0;
        double snapshotRate = 48000.0;
        std::atomic<bool> snapshotReady { false };
        int capturesTaken = 0;

        WavetableDocument document;
        DataPublisher publisher;
        std::vector<float> frames;
        float storedFrame = 0.0f;
        bool published = false;
    };
}
