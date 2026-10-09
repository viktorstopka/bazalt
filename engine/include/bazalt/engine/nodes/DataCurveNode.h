#pragma once

#include "bazalt/engine/graph/CurveData.h"
#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** A curve document published as a `Data(curve)` buffer — what a curve
        factory node owns (data.curve, source.oscillator, source.envelope).
        Message thread only: republishing reclaims the buffers the audio
        thread has moved past first, so a live drag never runs out of slots
        (DataPublisher holds 8). */
    class CurvePublisher
    {
    public:
        explicit CurvePublisher (CurveDocument::TimeBase defaultTimeBaseIn) noexcept : defaultTimeBase (defaultTimeBaseIn) {}

        /** False if every slot is still held by the audio thread (a voice
            that has not played since several edits). */
        bool setContent (const juce::var& content)
        {
            document = CurveDocument::fromVar (content, defaultTimeBase);
            return publishDocument();
        }

        /** Publishes the default shape if nothing has been published yet. */
        void ensurePublished()
        {
            if (! published)
            {
                document = CurveDocument::fromVar ({}, defaultTimeBase);
                publishDocument();
            }
        }

        DataPublisher& getPublisher() noexcept { return publisher; }
        const CurveDocument& getDocument() const noexcept { return document; }

    private:
        bool publishDocument()
        {
            publisher.reclaim();
            const auto ok = publisher.publish (buildCurveBuffer (document));
            published = published || ok;
            return ok;
        }

        CurveDocument::TimeBase defaultTimeBase;
        CurveDocument document;
        DataPublisher publisher;
        bool published = false;
    };

    /** Stable type id: "data.curve" — Curve (wiki/plans/DataAndWavetable.md
        1b): one curve, drawn in the Factory window, that any number of
        consumers read — an oscillator's shape, an envelope, a Lookup. Pure
        content: no inputs. Defaults to a sine. Replaces data.table's fixed
        32-point bank (schema v16 migrates it). */
    class DataCurveNode : public Node
    {
    public:
        void prepare (const NodePrepareInfo&) override { curve.ensurePublished(); }

        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return "Curve"; }
        juce::String getCategory() const override { return "Data"; }

        bool supportsPerSample() const noexcept override { return false; }
        void processBlock (const float* const*, float* const*, int) noexcept override {}

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "curve", .type = SignalType::Data, .label = "Curve", .isPrimaryOutput = true,
                                      .dataTags = { DataTag::Curve } } };
        }

        bool setContent (const juce::var& content) override { return curve.setContent (content); }
        DataPublisher* getDataPublisher() noexcept override { return &curve.getPublisher(); }

    private:
        CurvePublisher curve { CurveDocument::TimeBase::Cycle };
    };
}
