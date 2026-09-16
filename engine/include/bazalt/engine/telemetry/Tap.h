#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace bazalt::engine
{
    /** A named, preallocated SPSC ring buffer (ARCHITECTURE.md §6.1) that
        the audio thread writes raw samples into via push(), and the
        (single) analysis thread reads the most recent window of via
        readLatest(). Overflow policy is overwrite-oldest: push() always
        advances and writes, never blocks, never checks how far behind the
        reader is — visualization tolerates dropped/stale samples, the
        audio thread tolerates nothing that could block it.
    */
    class Tap
    {
    public:
        void prepare (size_t capacityPowerOfTwo)
        {
            buffer.assign (capacityPowerOfTwo, 0.0f);
            mask = capacityPowerOfTwo - 1;
            writeIndex.store (0, std::memory_order_relaxed);
        }

        size_t getCapacity() const noexcept { return buffer.size(); }

        /** Audio thread. Never allocates, never blocks. */
        void push (const float* samples, int numSamples) noexcept
        {
            auto w = writeIndex.load (std::memory_order_relaxed);

            for (int i = 0; i < numSamples; ++i)
            {
                buffer[(size_t) (w & mask)] = samples[i];
                ++w;
            }

            writeIndex.store (w, std::memory_order_release);
        }

        /** Analysis thread. Copies out up to maxSamples of the most
            recently written data, oldest-to-newest, and returns how many
            were actually available. If the writer advances by a full
            buffer's worth of samples while this call is mid-copy — only
            possible if the analysis thread stalls for an entire tap
            capacity's worth of audio, not a normal operating condition —
            the copy may be torn; that's the same "tolerates dropped
            samples, never blocks the writer" tradeoff the ring buffer
            itself makes, not a new risk this introduces.
        */
        int readLatest (float* dest, int maxSamples) const noexcept
        {
            const auto w = writeIndex.load (std::memory_order_acquire);
            const auto available = (int) std::min<uint64_t> ((uint64_t) maxSamples,
                                                              std::min (w, (uint64_t) buffer.size()));
            const auto start = w - (uint64_t) available;

            for (int i = 0; i < available; ++i)
                dest[i] = buffer[(size_t) ((start + (uint64_t) i) & mask)];

            return available;
        }

    private:
        std::vector<float> buffer;
        size_t mask = 0;
        std::atomic<uint64_t> writeIndex { 0 };
    };
}
