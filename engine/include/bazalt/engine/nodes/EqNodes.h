#pragma once

#include "bazalt/engine/graph/EqCurve.h"
#include "bazalt/engine/graph/Node.h"
#include <atomic>

namespace bazalt::engine::nodes
{
    /** An EQ curve document published as a `Data(eq)` buffer — what an EQ
        factory node owns. Message thread only: republishing reclaims the
        buffers the audio thread has moved past first, so a live drag never
        runs out of slots. */
    class EqPublisher
    {
    public:
        bool setContent (const juce::var& content)
        {
            document = EqCurveDocument::fromVar (content);
            return publishDocument();
        }

        void ensurePublished()
        {
            if (! published)
                publishDocument();
        }

        DataPublisher& getPublisher() noexcept { return publisher; }
        const EqCurveDocument& getDocument() const noexcept { return document; }

    private:
        bool publishDocument()
        {
            publisher.reclaim();
            const auto ok = publisher.publish (buildEqBuffer (document));
            published = published || ok;
            return ok;
        }

        EqCurveDocument document;
        DataPublisher publisher;
        bool published = false;
    };

    /** Stable type id: "data.eqCurve" — EQ Curve (wiki/plans/
        DataAndWavetable.md 1d): one set of EQ bands, drawn in the Factory
        window, that any number of EQs play — the same carve on several
        voices or layers, edited once. Pure content: no inputs. */
    class DataEqCurveNode : public Node
    {
    public:
        void prepare (const NodePrepareInfo&) override { eq.ensurePublished(); }

        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return "EQ Curve"; }
        juce::String getCategory() const override { return "Data"; }

        bool supportsPerSample() const noexcept override { return false; }
        void processBlock (const float* const*, float* const*, int) noexcept override {}

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "eq", .type = SignalType::Data, .label = "EQ", .isPrimaryOutput = true,
                                      .dataTags = { DataTag::EqCurve } } };
        }

        bool setContent (const juce::var& content) override { return eq.setContent (content); }
        DataPublisher* getDataPublisher() noexcept override { return &eq.getPublisher(); }

    private:
        EqPublisher eq;
    };

    /** Stable type id: "filter.eq" — EQ (wiki/plans/DataAndWavetable.md 1d,
        Factories.md's first processor factory). Plays an EQ curve on its
        input: its own (content, edited in the Factory window over a live
        spectrum of what it outputs) or, when **Curve** is wired, the
        data.eqCurve plugged in — the same "I have a value, but you can
        connect me" rule the Oscillator's Shape follows.

        Bands run in series, each as the primitive filters it stands for
        (`EqCurve.h`): Unwrap replaces the node with exactly those Peak /
        Shelf / SVF nodes and the sound does not change. A curve edit glides
        (~10 ms) instead of jumping. Per channel (each lane its own state). */
    class EqFilterNode : public Node
    {
    public:
        void prepare (const NodePrepareInfo& info) override
        {
            processor.prepare (info.sampleRate);
            ownCurve.ensurePublished();
        }

        void reset() override { processor.reset(); }

        int getNumInputPorts() const noexcept override { return 2; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return "EQ"; }
        juce::String getCategory() const override { return "Filters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio }),
                PortDescriptor { .id = "curve", .type = SignalType::Data, .label = "Curve", .dataTags = { DataTag::EqCurve } },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { perChannel ({ .id = "out", .type = SignalType::Signal, .isPrimaryOutput = true, .quantity = Quantity::Audio }) };
        }

        bool setContent (const juce::var& content) override { return ownCurve.setContent (content); }

        void setDataInput (const juce::String& inputPortId, DataPublisher* publisher) noexcept override
        {
            if (inputPortId == "curve")
                curveInput.store (publisher, std::memory_order_release);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            processor.setBuffer (currentBuffer());
            outputs[0] = processor.processSample (inputs[0]);
        }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            processor.setBuffer (currentBuffer());
            for (int i = 0; i < numSamples; ++i)
                outputs[0][i] = processor.processSample (inputs[0][i]);
        }

        const EqCurveDocument& getOwnDocument() const noexcept { return ownCurve.getDocument(); }

    private:
        const DataBuffer* currentBuffer() noexcept
        {
            auto* wired = curveInput.load (std::memory_order_acquire);
            return (wired != nullptr ? wired : &ownCurve.getPublisher())->getCurrentForAudioThread();
        }

        EqPublisher ownCurve;
        EqProcessor processor;
        std::atomic<DataPublisher*> curveInput { nullptr };
    };
}
