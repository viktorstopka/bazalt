#pragma once

#include "bazalt/engine/telemetry/PhaseSnapshot.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
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
            phaseBuffer.assign (capacityPowerOfTwo, std::numeric_limits<float>::quiet_NaN());
            mask = capacityPowerOfTwo - 1;
            writeIndex.store (0, std::memory_order_relaxed);
            snapshotSequence.store (0, std::memory_order_relaxed);
            snapshot = {};
        }

        size_t getCapacity() const noexcept { return buffer.size(); }

        /** Total samples ever pushed since prepare() (not clamped to the
            capacity). A reader that remembers the last value it saw knows
            exactly how many samples are new - and a test can tell "the
            audio thread is still pushing" from "there is stale data here".
        */
        uint64_t getTotalPushed() const noexcept { return writeIndex.load (std::memory_order_acquire); }

        /** Audio thread. Never allocates, never blocks. `phases` (optional)
            is the buffer's phase source's per-sample position
            (Node::getPhaseTrack()), written alongside each sample so the two
            can never drift apart; without one the phase reads as NaN. */
        void push (const float* samples, int numSamples, const float* phases = nullptr) noexcept
        {
            auto w = writeIndex.load (std::memory_order_relaxed);

            for (int i = 0; i < numSamples; ++i)
            {
                buffer[(size_t) (w & mask)] = samples[i];
                phaseBuffer[(size_t) (w & mask)] = phases != nullptr ? phases[i] : std::numeric_limits<float>::quiet_NaN();
                ++w;
            }

            writeIndex.store (w, std::memory_order_release);
        }

        /** Audio thread, once per block: the latest PhaseSnapshot of this
            tap's phase source. A seqlock — the writer never waits; the
            (single) reader retries if it raced a write. */
        void publishSnapshot (const PhaseSnapshot& next) noexcept
        {
            const auto sequence = snapshotSequence.load (std::memory_order_relaxed);
            snapshotSequence.store (sequence + 1, std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_release);
            std::memcpy (&snapshot, &next, sizeof (PhaseSnapshot));
            snapshotSequence.store (sequence + 2, std::memory_order_release);
        }

        /** Analysis thread. False if nothing has been published yet, or the
            writer kept racing the read. */
        bool readSnapshot (PhaseSnapshot& dest) const noexcept
        {
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                const auto before = snapshotSequence.load (std::memory_order_acquire);
                if (before == 0 || (before & 1u) != 0)
                    continue;
                std::memcpy (&dest, &snapshot, sizeof (PhaseSnapshot));
                std::atomic_thread_fence (std::memory_order_acquire);
                if (snapshotSequence.load (std::memory_order_relaxed) == before)
                    return true;
            }
            return false;
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
        int readLatest (float* dest, int maxSamples, float* phaseDest = nullptr) const noexcept
        {
            const auto w = writeIndex.load (std::memory_order_acquire);
            const auto available = (int) std::min<uint64_t> ((uint64_t) maxSamples,
                                                              std::min (w, (uint64_t) buffer.size()));
            const auto start = w - (uint64_t) available;

            for (int i = 0; i < available; ++i)
            {
                dest[i] = buffer[(size_t) ((start + (uint64_t) i) & mask)];
                if (phaseDest != nullptr)
                    phaseDest[i] = phaseBuffer[(size_t) ((start + (uint64_t) i) & mask)];
            }

            return available;
        }

    private:
        std::vector<float> buffer;
        std::vector<float> phaseBuffer;
        size_t mask = 0;
        std::atomic<uint64_t> writeIndex { 0 };
        std::atomic<uint32_t> snapshotSequence { 0 };
        PhaseSnapshot snapshot;
    };
}
