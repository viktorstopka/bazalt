#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/SampleData.h"
#include <array>
#include <atomic>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "data.record" — Record (wiki/plans/Sampling.md §2.5, 3a.3).
        The live half of samples: holds what is happening as a **live**
        `Data(sample)` that a Sampler (or, later, Cycle Cuts / the Convolver) plays
        while it is still being written — the last seconds of a guitar, a loop
        being overdubbed, a modulation's movement.

        **In:** `in` (any signal; a mono cable fills both sides), `record`
        (records while high; unwired: always), `clear` (Event: forget it all),
        Feedback (Loop: how much of the loop each overdub keeps).
        **Settings:** Length (s), Channels (Mono / Stereo), Mode —
          - Ring: always the last Length seconds;
          - Once: one take from the start, stops when full (record going high
            again after a Clear starts a new take);
          - Loop: the first take sets the loop's length (when Record goes low,
            or at Length), then every pass while Record is high overdubs it.
        **Out:** `sample` (the live recording), `position` (0–1, the write
        head; the loop's playhead in Loop), `recording` (high while writing).

        The buffer is allocated once, here on the message thread (prepare() /
        a structural Length or Channels change, which builds a fresh node); the
        audio thread writes into it in place (DataBuffer::Live). Freeze
        (GraphEditController::freezeRecord) pauses the writes, copies the
        recording into the sample library, and resumes. */
    class RecordNode : public Node
    {
    public:
        enum class Mode { Ring, Once, Loop };
        static constexpr int overviewBuckets = 160;
        static constexpr float maxSeconds = 60.0f;
        static constexpr int maxFrames = 1 << 23; // DataBuffer::encodeWindow stays exact in a float

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            maxBlockSize = juce::jmax (1, info.maxBlockSize);
            track.assign ((size_t) maxBlockSize, 0.0f);
            publisher.setCompanion (track.data(), maxBlockSize);
            prepared = true;
            allocate();
        }

        void reset() override { clearRecording(); }

        int getNumInputPorts() const noexcept override { return 4; }
        int getNumInputChannels() const noexcept override { return 5; }
        int getNumOutputPorts() const noexcept override { return 3; }
        juce::String getTitle() const override { return "Record"; }
        juce::String getCategory() const override { return "Data"; }
        bool supportsPerSample() const noexcept override { return false; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "in", .type = SignalType::Signal, .label = "In", .channels = Channels::Stereo },
                PortDescriptor { .id = "record", .type = SignalType::Signal, .label = "Record", .hasFallbackWhenUnconnected = true,
                                  .quantity = Quantity::Boolean },
                PortDescriptor { .id = "clear", .type = SignalType::Event, .label = "Clear" },
                PortDescriptor { .id = "data.record.feedback", .type = SignalType::Signal, .label = "Feedback", .minValue = 0.0f,
                                  .maxValue = 1.0f, .defaultValue = 1.0f, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "sample", .type = SignalType::Data, .label = "Sample", .isPrimaryOutput = true,
                                  .dataTags = { DataTag::Sample } },
                PortDescriptor { .id = "position", .type = SignalType::Signal, .label = "Position", .minValue = 0.0f, .maxValue = 1.0f,
                                  .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "recording", .type = SignalType::Signal, .label = "Recording", .quantity = Quantity::Boolean },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "data.record.length", .minValue = 0.1f, .maxValue = maxSeconds, .defaultValue = 8.0f,
                                      .unit = "s", .displayName = "Length", .quantity = Quantity::Time, .isStructural = true },
                ParameterDescriptor { .id = "data.record.channels", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f,
                                      .displayName = "Channels", .isInteger = true, .kind = ValueKind::Enum,
                                      .enumOptions = { { "mono", "Mono" }, { "stereo", "Stereo" } }, .isStructural = true },
                ParameterDescriptor { .id = "data.record.mode", .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 0.0f,
                                      .displayName = "Mode", .isInteger = true, .kind = ValueKind::Enum,
                                      .enumOptions = { { "ring", "Ring" }, { "once", "Once" }, { "loop", "Loop" } } },
            };
        }

        void setParameter (const juce::String& id, float value) override
        {
            if (id == "data.record.length") // structural: message thread, a fresh node
            {
                lengthSeconds = juce::jlimit (0.1f, maxSeconds, value);
                allocate();
            }
            else if (id == "data.record.channels")
            {
                stereo = value > 0.5f;
                allocate();
            }
            else if (id == "data.record.mode")
            {
                const auto next = (Mode) juce::jlimit (0, 2, (int) std::lround (value));
                if (next != mode)
                {
                    mode = next;
                    clearPending = true; // the recording's shape means something else now
                }
            }
            else if (id == "data.record.feedback")
                storedFeedback = value;
        }

        DataPublisher* getDataPublisher() noexcept override { return &publisher; }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            auto* buffer = publisher.getCurrentForAudioThread();
            auto* data = buffer != nullptr ? buffer->liveData() : nullptr;
            const auto isPaused = pauseRequested.load (std::memory_order_acquire);
            paused.store (isPaused, std::memory_order_release);

            for (int s = 0; s < numSamples; ++s)
            {
                if (inputs[clearIn][s] > 0.0f || clearPending)
                    clearRecording();

                const auto gate = inputs[recordIn][s];
                const auto high = std::isnan (gate) || gate > 0.5f;
                const auto rose = high && ! wasHigh;
                wasHigh = high;
                auto writing = false;

                if (data != nullptr && ! isPaused)
                {
                    const auto left = inputs[inLeft][s], right = inputs[inRight][s];
                    switch (mode)
                    {
                        case Mode::Ring:
                            if (high)
                            {
                                put (data, writeHead, left, right, 0.0f);
                                writing = true;
                                filled = juce::jmin (capacity, filled + 1);
                                if (++writeHead >= capacity)
                                {
                                    writeHead = 0;
                                    wrapped = true;
                                }
                            }
                            break;

                        case Mode::Once:
                            if (rose && takeDone)
                            {
                                filled = 0;
                                takeDone = false;
                            }
                            if (high && ! takeDone && filled < capacity)
                            {
                                put (data, filled++, left, right, 0.0f);
                                writing = true;
                            }
                            if (filled >= capacity || (! high && filled > 0))
                                takeDone = true;
                            writeHead = filled;
                            break;

                        case Mode::Loop:
                            if (loopLength == 0)
                            {
                                if (high && filled < capacity)
                                {
                                    put (data, filled++, left, right, 0.0f);
                                    writing = true;
                                }
                                if (filled >= capacity || (! high && filled > 0))
                                {
                                    loopLength = filled;
                                    writeHead = 0;
                                }
                                else
                                    writeHead = filled;
                            }
                            else
                            {
                                if (high)
                                {
                                    const auto feedback = std::isnan (inputs[feedbackIn][s]) ? storedFeedback : inputs[feedbackIn][s];
                                    put (data, writeHead, left, right, juce::jlimit (0.0f, 1.0f, feedback));
                                    writing = true;
                                }
                                if (++writeHead >= loopLength)
                                    writeHead = 0;
                            }
                            break;
                    }
                }

                const auto window = DataBuffer::encodeWindow (mode == Mode::Loop && loopLength > 0 ? loopLength : filled, writeHead, capacity,
                                                              mode == Mode::Ring && wrapped);
                if (s < (int) track.size())
                    track[(size_t) s] = window;
                const auto span = mode == Mode::Loop && loopLength > 0 ? loopLength : capacity;
                outputs[positionOut][s] = span > 0 ? (float) writeHead / (float) span : 0.0f;
                outputs[recordingOut][s] = writing ? 1.0f : 0.0f;
            }

            if (buffer != nullptr && buffer->live != nullptr && numSamples > 0)
                buffer->live->lastWindow.store (track[(size_t) juce::jmin (numSamples, (int) track.size()) - 1], std::memory_order_release);
        }

        // ---- Freeze (message thread) ----

        /** Asks the audio thread to stop writing (true) or go on (false). */
        void setPaused (bool shouldPause) noexcept { pauseRequested.store (shouldPause, std::memory_order_release); }
        /** True once the audio thread has seen the pause and stopped writing. */
        bool isPaused() const noexcept { return paused.load (std::memory_order_acquire); }

        /** The recording as it stands, oldest first, interleaved — call while
            paused (or while no audio runs). Empty when nothing is recorded. */
        std::vector<float> copyRecording (int& framesOut, int& channelsOut) const
        {
            framesOut = 0;
            channelsOut = allocated != nullptr ? allocated->channels() : 1;
            if (allocated == nullptr || allocated->live == nullptr)
                return {};
            const auto window = allocated->windowFor (allocated->live->lastWindow.load (std::memory_order_acquire));
            const auto channels = allocated->channels();
            const auto* data = allocated->rawData();
            std::vector<float> out ((size_t) window.length * (size_t) channels);
            for (int f = 0; f < window.length; ++f)
            {
                const auto from = (window.origin + f) % allocated->frames();
                for (int c = 0; c < channels; ++c)
                    out[(size_t) f * (size_t) channels + (size_t) c] = data[(size_t) from * (size_t) channels + (size_t) c];
            }
            framesOut = window.length;
            return out;
        }

        double getSampleRate() const noexcept { return sampleRate; }

        /** Message thread (the body): the buffer's waveform as min/max pairs in
            buffer order, where the write head is (0–1 of the buffer) and how
            much of it is recorded (0–1). Kept by the writer as it goes, so
            reading it never touches the samples being written. */
        struct View
        {
            std::vector<float> peaks;
            float head = 0.0f, filled = 0.0f;
            bool wrapped = false;
        };
        View getView() const
        {
            View view;
            view.peaks.resize (overview.size());
            for (size_t i = 0; i < overview.size(); ++i)
                view.peaks[i] = overview[i].load (std::memory_order_relaxed);
            if (allocated != nullptr && allocated->live != nullptr && allocated->frames() > 0)
            {
                const auto encoded = allocated->live->lastWindow.load (std::memory_order_acquire);
                const auto window = allocated->windowFor (encoded);
                const auto frames = (float) allocated->frames();
                view.wrapped = window.firstReadable > 0;
                view.filled = (float) window.length / frames;
                view.head = view.wrapped ? (float) window.origin / frames : view.filled;
            }
            return view;
        }

        /** Message thread (the node's body): seconds recorded so far. */
        float getRecordedSeconds() const noexcept
        {
            if (allocated == nullptr || allocated->live == nullptr || sampleRate <= 0.0)
                return 0.0f;
            return (float) ((double) allocated->windowFor (allocated->live->lastWindow.load (std::memory_order_acquire)).length / sampleRate);
        }

    private:
        static constexpr int inLeft = 0, inRight = 1, recordIn = 2, clearIn = 3, feedbackIn = 4;
        static constexpr int positionOut = 1, recordingOut = 2;

        void put (float* data, int frame, float left, float right, float keep) noexcept
        {
            auto* at = data + (size_t) frame * (size_t) (stereo ? 2 : 1);
            float lo, hi;
            if (stereo)
            {
                at[0] = at[0] * keep + left;
                at[1] = at[1] * keep + right;
                lo = juce::jmin (at[0], at[1]);
                hi = juce::jmax (at[0], at[1]);
            }
            else
                lo = hi = at[0] = at[0] * keep + 0.5f * (left + right);

            // The body's waveform: a bucket starts over when the writer enters it.
            const auto bucket = (int) ((int64_t) frame * overviewBuckets / juce::jmax (1, capacity));
            auto& low = overview[(size_t) bucket * 2];
            auto& high = overview[(size_t) bucket * 2 + 1];
            if (bucket != writingBucket)
            {
                writingBucket = bucket;
                low.store (lo, std::memory_order_relaxed);
                high.store (hi, std::memory_order_relaxed);
            }
            else
            {
                low.store (juce::jmin (lo, low.load (std::memory_order_relaxed)), std::memory_order_relaxed);
                high.store (juce::jmax (hi, high.load (std::memory_order_relaxed)), std::memory_order_relaxed);
            }
        }

        void clearRecording() noexcept
        {
            for (auto& v : overview)
                v.store (0.0f, std::memory_order_relaxed);
            writingBucket = -1;
            clearPending = false;
            filled = writeHead = loopLength = 0;
            wrapped = false;
            takeDone = false;
        }

        /** Message thread only: a fresh node, before it is ever on the audio
            thread (prepare(), or a structural setting right after it). */
        void allocate()
        {
            if (! prepared)
                return;
            const auto channels = stereo ? 2 : 1;
            const auto frames = juce::jlimit (1, maxFrames - 1, (int) std::lround ((double) lengthSeconds * sampleRate));
            if (allocated != nullptr && allocated->frames() == frames && allocated->channels() == channels)
                return;

            auto buffer = std::make_unique<DataBuffer> (DataTag::Sample, std::vector<float> ((size_t) frames * (size_t) channels, 0.0f),
                                                        channels, sampleRate);
            buffer->sampleMeta.channelQuantities.assign ((size_t) channels, (uint8_t) Quantity::Audio);
            buffer->live = std::make_unique<DataBuffer::Live>();
            buffer->live->guard = maxBlockSize + 8; // this block's writes, plus the interpolation's reach
            capacity = frames;
            clearRecording();
            allocated = buffer.get();
            publisher.reclaim();
            publisher.publish (std::move (buffer));
        }

        DataPublisher publisher;
        const DataBuffer* allocated = nullptr; // message thread's view of the one buffer this node publishes
        std::vector<float> track;              // the companion: the recording's window, per sample

        double sampleRate = 48000.0;
        int maxBlockSize = 512;
        bool prepared = false;
        float lengthSeconds = 8.0f;
        bool stereo = true;
        Mode mode = Mode::Ring;
        float storedFeedback = 1.0f;

        std::atomic<bool> pauseRequested { false }, paused { false };
        std::array<std::atomic<float>, (size_t) overviewBuckets * 2> overview {};
        int writingBucket = -1;

        // Audio thread.
        int capacity = 0, filled = 0, writeHead = 0, loopLength = 0;
        bool wrapped = false, takeDone = false, wasHigh = false, clearPending = false;
    };
}
