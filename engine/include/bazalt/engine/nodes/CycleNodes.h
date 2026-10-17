#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/nodes/GrowableGroupNode.h"
#include <array>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "cycle.cuts" — Cycle Cuts (was Wavesets) (wiki/plans/Wavesets.md W1).
        Cuts a sound at its zero crossings: a **waveset** is one positive and one
        negative half, from one crossing to the next in the same direction —
        on anything but a pure tone, pseudo-cycles of very uneven lengths.

        **Out:** `wavesets` — a live Data(wavesets): the last second of the
        input plus where each waveset starts (DataBuffer::Live's marks), for
        Waveset Play (or a Sampler: it is a live sample too); `boundary` (Event
        at each new waveset), `length` (s, the last waveset's; 1/length is a
        crude pitch), `peak` (the last waveset's), `count`.
        **Settings:** Min length (shorter wavesets merge into the next, so noise
        doesn't become fizz), Group (N wavesets count as one), Direction (up /
        down crossings). A waveset longer than a quarter second (silence, a very
        low sound) is cut there — a player never waits forever. */
    class CycleCutsNode : public Node
    {
    public:
        static constexpr double ringSeconds = 1.0;
        static constexpr double maxWavesetSeconds = 0.25;
        static constexpr int markCapacity = 4096;

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            capacity = juce::jmax (64, (int) std::lround (ringSeconds * sampleRate));
            maxLength = juce::jmax (2, (int) std::lround (maxWavesetSeconds * sampleRate));
            track.assign ((size_t) juce::jmax (1, info.maxBlockSize), 0.0f);
            publisher.setCompanion (track.data(), (int) track.size());

            auto buffer = std::make_unique<DataBuffer> (DataTag::CycleCuts, std::vector<float> ((size_t) capacity, 0.0f), 1, sampleRate);
            buffer->sampleMeta.channelQuantities = { (uint8_t) Quantity::Audio };
            buffer->live = std::make_unique<DataBuffer::Live>();
            buffer->live->guard = info.maxBlockSize + 8;
            buffer->live->marks.assign ((size_t) markCapacity, 0);
            publisher.reclaim();
            publisher.publish (std::move (buffer));
            reset();
        }

        void reset() override
        {
            written = 0;
            lastMark = -1;
            crossings = 0;
            previous = 0.0f;
            peak = lastPeak = lastLength = 0.0f;
            resetPending = true; // the shared marks/written live in the buffer: cleared on the audio thread
        }

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 5; }
        juce::String getTitle() const override { return "Cycle Cuts"; }
        juce::String getCategory() const override { return "Cycles"; }
        bool supportsPerSample() const noexcept override { return false; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Signal, .label = "In", .quantity = Quantity::Audio } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "cuts", .type = SignalType::Data, .label = "Cuts", .isPrimaryOutput = true,
                                  .dataTags = { DataTag::CycleCuts } },
                PortDescriptor { .id = "boundary", .type = SignalType::Event, .label = "Boundary" },
                PortDescriptor { .id = "length", .type = SignalType::Signal, .label = "Length", .unit = "s", .quantity = Quantity::Time },
                PortDescriptor { .id = "peak", .type = SignalType::Signal, .label = "Peak", .minValue = 0.0f, .maxValue = 1.0f,
                                  .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "count", .type = SignalType::Signal, .label = "Count", .isInteger = true, .quantity = Quantity::Count },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "cycle.cuts.minLength", .minValue = 0.0f, .maxValue = 20.0f, .defaultValue = 0.0f,
                                      .unit = "ms", .displayName = "Min length", .quantity = Quantity::Time },
                ParameterDescriptor { .id = "cycle.cuts.group", .minValue = 1.0f, .maxValue = 16.0f, .defaultValue = 1.0f,
                                      .displayName = "Group", .isInteger = true, .kind = ValueKind::Int, .quantity = Quantity::Count },
                ParameterDescriptor { .id = "cycle.cuts.direction", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                      .displayName = "Direction", .isInteger = true, .kind = ValueKind::Enum,
                                      .enumOptions = { { "up", "Upward" }, { "down", "Downward" } } },
            };
        }

        void setParameter (const juce::String& id, float value) override
        {
            if (id == "cycle.cuts.minLength")
                minLengthMs = juce::jlimit (0.0f, 20.0f, value);
            else if (id == "cycle.cuts.group")
                group = juce::jlimit (1, 16, (int) std::lround (value));
            else if (id == "cycle.cuts.direction")
                downward = value > 0.5f;
        }

        DataPublisher* getDataPublisher() noexcept override { return &publisher; }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            auto* buffer = publisher.getCurrentForAudioThread();
            auto* data = buffer != nullptr ? buffer->liveData() : nullptr;
            if (data == nullptr)
            {
                for (int o = 1; o < 5; ++o)
                    std::fill (outputs[o], outputs[o] + numSamples, 0.0f);
                return;
            }
            auto& live = *buffer->live;
            if (resetPending)
            {
                resetPending = false;
                live.written = 0;
                live.markCount = 0;
            }
            const auto minLength = (int64_t) std::lround ((double) minLengthMs * 0.001 * sampleRate);

            for (int s = 0; s < numSamples; ++s)
            {
                const auto x = inputs[0][s];
                const auto at = written; // this sample's absolute frame
                data[(size_t) (at % capacity)] = x;
                ++written;

                const auto crossed = downward ? (previous > 0.0f && x <= 0.0f) : (previous < 0.0f && x >= 0.0f);
                previous = x;
                if (crossed)
                    ++crossings;

                auto boundary = false;
                if (lastMark < 0)
                {
                    if (crossed)
                        boundary = true; // the first crossing starts the first waveset
                }
                else if ((crossings >= group && at - lastMark >= minLength) || at - lastMark >= maxLength)
                    boundary = true;

                if (boundary)
                {
                    if (lastMark >= 0)
                    {
                        lastLength = (float) ((double) (at - lastMark) / sampleRate);
                        lastPeak = peak;
                    }
                    live.marks[(size_t) (live.markCount % markCapacity)] = at;
                    ++live.markCount;
                    lastMark = at;
                    crossings = 0;
                    peak = 0.0f;
                }
                peak = juce::jmax (peak, std::abs (x));

                outputs[1][s] = boundary ? 1.0f : 0.0f;
                outputs[2][s] = lastLength;
                outputs[3][s] = lastPeak;
                outputs[4][s] = (float) live.markCount;
                track[(size_t) juce::jmin (s, (int) track.size() - 1)] =
                    DataBuffer::encodeWindow ((int) juce::jmin<int64_t> (written, capacity), (int) (written % capacity), capacity, written >= capacity);
            }
            live.written = written;
            if (numSamples > 0)
                live.lastWindow.store (track[(size_t) juce::jmin (numSamples, (int) track.size()) - 1], std::memory_order_release);
        }

    private:
        DataPublisher publisher;
        std::vector<float> track;
        double sampleRate = 48000.0;
        int capacity = 48000, maxLength = 12000;
        float minLengthMs = 0.0f;
        int group = 1;
        bool downward = false;

        int64_t written = 0, lastMark = -1;
        int crossings = 0;
        float previous = 0.0f, peak = 0.0f, lastPeak = 0.0f, lastLength = 0.0f;
        bool resetPending = true;
    };

    /** Stable type id: "cycle.weave" — Weave (was Waveset Play) (wiki/plans/Wavesets.md W1,
        Live mode). Plays whole wavesets from its inputs in turn — Count of A,
        Count of B, … — copied sample for sample, so every join is at a zero
        crossing and nothing clicks. Live: each source keeps running in real
        time, and a turn starts on that source's **newest complete** waveset
        (within a turn the next ones follow in order); each source loses what
        the others play instead of slowing down. Latency: at most one waveset.
        **Polarity match** turns over a waveset that starts on the other side
        (a cut forced by silence can). Two Cycle Cuts into one Weave at Count 1 is
        Composers Desktop Project's Distort: Interleave, live. */
    class WeaveNode : public GrowableGroupNode
    {
    public:
        static constexpr int minInputs = 2;
        static constexpr int maxInputs = 8;

        WeaveNode() noexcept : GrowableGroupNode (minInputs, maxInputs) {}

        void reset() override
        {
            playing = false;
            current = -1;
            left = 0;
            position = end = 0;
            for (auto& j : lastIndex)
                j = -1;
        }

        int getNumInputPorts() const noexcept override { return groupCount + 1; }
        int getNumOutputPorts() const noexcept override { return 3; }
        juce::String getTitle() const override { return "Weave"; }
        juce::String getCategory() const override { return "Cycles"; }
        bool supportsPerSample() const noexcept override { return false; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            std::vector<PortDescriptor> ports;
            const auto group = groupFor ("in.");
            for (int i = 0; i < groupCount; ++i)
                ports.push_back (PortDescriptor { .id = "in." + juce::String (i), .type = SignalType::Data,
                                                  .label = juce::String::charToString ((juce::juce_wchar) ('A' + i)),
                                                  .group = group, .dataTags = { DataTag::CycleCuts } });
            ports.push_back (PortDescriptor { .id = "cycle.weave.count", .type = SignalType::Signal, .label = "Count", .minValue = 1.0f,
                                              .maxValue = 16.0f, .defaultValue = 1.0f, .isInteger = true,
                                              .hasFallbackWhenUnconnected = true, .kind = ValueKind::Int, .quantity = Quantity::Count });
            return ports;
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true, .quantity = Quantity::Audio },
                PortDescriptor { .id = "switch", .type = SignalType::Event, .label = "Switch" },
                PortDescriptor { .id = "source", .type = SignalType::Signal, .label = "Source", .isInteger = true, .quantity = Quantity::Count },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "cycle.weave.polarity", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f,
                                           .displayName = "Polarity match", .isInteger = true, .kind = ValueKind::Enum,
                                           .enumOptions = { { "off", "Off" }, { "on", "On" } } } };
        }

        void setParameter (const juce::String& id, float value) override
        {
            if (id == "cycle.weave.polarity")
                matchPolarity = value > 0.5f;
            else if (id == "cycle.weave.count")
                storedCount = value;
        }

        void setDataInput (const juce::String& portId, DataPublisher* publisher) noexcept override
        {
            if (portId.startsWith ("in."))
                if (const auto i = portId.fromFirstOccurrenceOf ("in.", false, false).getIntValue(); i >= 0 && i < maxInputs)
                    sources[(size_t) i] = publisher;
        }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            std::array<const DataBuffer*, maxInputs> buffers {};
            for (int i = 0; i < groupCount; ++i)
            {
                const auto* b = sources[(size_t) i] != nullptr ? sources[(size_t) i]->getCurrentForAudioThread() : nullptr;
                buffers[(size_t) i] = b != nullptr && b->live != nullptr && ! b->live->marks.empty() ? b : nullptr;
            }
            const auto* countIn = inputs[groupCount];

            for (int s = 0; s < numSamples; ++s)
            {
                auto switched = false;
                if (! playing || position >= end)
                {
                    if (playing)
                        --left;
                    playing = false;
                    const auto count = juce::jlimit (1, 16, (int) std::lround (std::isnan (countIn[s]) ? storedCount : countIn[s]));
                    auto next = current;
                    if (left <= 0 || current < 0)
                    {
                        next = nextSource (buffers, current);
                        left = count;
                    }
                    if (next >= 0)
                    {
                        const auto turnStarts = next != current || left == count;
                        switched = next != current;
                        current = next;
                        playing = pick (*buffers[(size_t) current], current, s, numSamples, turnStarts);
                    }
                }

                auto value = 0.0f;
                if (playing && buffers[(size_t) current] != nullptr)
                {
                    const auto* b = buffers[(size_t) current];
                    value = b->rawData()[(size_t) (position % b->frames())] * (invert ? -1.0f : 1.0f);
                    ++position;
                }
                outputs[0][s] = value;
                outputs[1][s] = switched ? 1.0f : 0.0f;
                outputs[2][s] = (float) juce::jmax (0, current);
            }
        }

    private:
        /** The next connected source after `from`, cycling; -1 when none is. */
        int nextSource (const std::array<const DataBuffer*, maxInputs>& buffers, int from) const noexcept
        {
            for (int step = 1; step <= groupCount; ++step)
            {
                const auto i = ((from < 0 ? -1 : from) + step) % groupCount;
                if (buffers[(size_t) i] != nullptr)
                    return i;
            }
            return -1;
        }

        /** Chooses source `k`'s waveset to play now: at a turn's start the newest
            complete one; within a turn the one after the last played, once it is
            complete (else the newest again). False when it has none yet. */
        bool pick (const DataBuffer& buffer, int k, int s, int numSamples, bool turnStarts) noexcept
        {
            const auto& live = *buffer.live;
            const auto marks = (int64_t) live.marks.size();
            const auto now = live.written - numSamples + s; // the newest frame written at this sample
            auto newest = live.markCount - 1;
            while (newest >= 1 && newest > live.markCount - marks && live.marks[(size_t) (newest % marks)] > now)
                --newest; // marks found later in this block are still in the future here
            if (newest < 1 || newest <= live.markCount - marks || live.marks[(size_t) (newest % marks)] > now)
                return false;

            auto j = newest;
            if (! turnStarts && lastIndex[(size_t) k] >= live.markCount - marks && lastIndex[(size_t) k] + 1 <= newest)
                j = lastIndex[(size_t) k] + 1;
            const auto start = live.marks[(size_t) ((j - 1) % marks)];
            const auto stop = live.marks[(size_t) (j % marks)];
            if (stop <= start || now - start >= (int64_t) buffer.frames() - live.guard)
                return false; // already overwritten
            lastIndex[(size_t) k] = j;
            position = start;
            end = stop;

            // A waveset should start on the rising side; one that starts falling
            // (a cut forced by silence) is turned over.
            invert = false;
            if (matchPolarity)
            {
                const auto quarter = start + juce::jmax<int64_t> (1, (stop - start) / 4);
                invert = buffer.rawData()[(size_t) (quarter % buffer.frames())] < 0.0f;
            }
            return true;
        }

        std::array<DataPublisher*, maxInputs> sources {};
        std::array<int64_t, maxInputs> lastIndex {};
        float storedCount = 1.0f;
        bool matchPolarity = true;

        bool playing = false, invert = false;
        int current = -1, left = 0;
        int64_t position = 0, end = 0;
    };
}
