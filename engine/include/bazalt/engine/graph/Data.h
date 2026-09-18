#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace bazalt::engine
{
    /** Semantic tag for a `DataBuffer` (SIGNAL_TYPES.md §2's Data type
        details: "a small header (element type, length, semantic tag such
        as modal-set, scale, wavetable, curve, ir)"). A consuming port
        declares which tag(s) it accepts (`PortDescriptor::dataTags`);
        `canConnect` (M16) rejects a mismatch at compile time — "wiring a
        scale into a Modal Bank is rejected." `Unknown` never satisfies a
        specific requirement; there is no wildcard match, matching
        SIGNAL_TYPES.md §5's "anything -> Data: rejected, no implicit
        construction" spirit applied to tags too.
    */
    enum class DataTag
    {
        Unknown,
        Curve,
        Scale,
        Wavetable,
        ModalSet,
        Sample,
        ImpulseResponse
    };

    /** An immutable block of values (SIGNAL_TYPES.md §2). Built once,
        never mutated after construction — a producer that wants to change
        the content builds a *new* `DataBuffer` and publishes it via
        `DataPublisher`, it never edits one in place. Storage is a flat
        `float` array; `stride` is how many floats make up one logical
        element (1 for a plain curve/scale value, e.g. 3 for a
        frequency/gain/decay modal-set triple) — this is deliberately
        generic rather than a per-tag C++ type, since no real Data-
        producing/consuming node exists yet to know the final per-tag
        layouts should be anything more specific (M23/M24's job).
        `sampleRate` is meaningful only for tags where it matters
        (Sample/ImpulseResponse/Wavetable); left at 0 otherwise.
    */
    class DataBuffer
    {
    public:
        DataBuffer (DataTag tagIn, std::vector<float> valuesIn, int strideIn = 1, double sampleRateIn = 0.0) noexcept
            : tag_ (tagIn), values_ (std::move (valuesIn)), stride_ (strideIn > 0 ? strideIn : 1), sampleRate_ (sampleRateIn)
        {
        }

        /** Lifecycle bookkeeping for `DataPublisher`, not part of this
            buffer's logical immutable content — set exactly once, by
            `DataPublisher::publish()`, before the buffer becomes visible
            to any reader. Mirrors `ExecutionPlan::generation` exactly, and
            for the same reason: `getCurrentForAudioThread()` must record
            the generation of the *specific slot it just dereferenced*, not
            some separately-tracked "latest published" counter — with more
            than one concurrent reader (unlike `PlanSwapper`'s single
            reader), a slow reader holding an old slot can otherwise be
            hidden behind a fast reader's newer epoch update, and
            `reclaim()` frees the slot the slow reader is still using.
            Public for the same reason `ExecutionPlan::generation` is.
        */
        uint64_t generation = 0;

        DataTag tag() const noexcept { return tag_; }
        int stride() const noexcept { return stride_; }
        double sampleRate() const noexcept { return sampleRate_; }

        /** Number of logical elements — `rawSize() / stride()`, not the
            raw float count.
        */
        int length() const noexcept { return (int) (values_.size() / (size_t) stride_); }

        size_t rawSize() const noexcept { return values_.size(); }
        const float* rawData() const noexcept { return values_.data(); }

        /** `stride() == 1` convenience — a plain curve/scale read. */
        float at (int index) const noexcept { return values_[(size_t) index]; }

        /** `stride() > 1` — one float within logical element `element`. */
        float at (int element, int channel) const noexcept
        {
            return values_[(size_t) element * (size_t) stride_ + (size_t) channel];
        }

    private:
        DataTag tag_;
        std::vector<float> values_;
        int stride_;
        double sampleRate_;
    };

    /** Lock-free single-writer (a producer node's own worker/message-
        thread build step) / single-reader (the audio thread) publish +
        epoch-based reclamation for `DataBuffer`, mirroring `PlanSwapper`'s
        proven pattern exactly (ARCHITECTURE.md §3.2) rather than reaching
        for `std::shared_ptr` — a shared_ptr's atomic refcount can
        decrement to zero (and therefore call `delete`) on whichever
        thread happens to drop the last reference, which could be the
        audio thread. Every buffer here is instead owned by a fixed slot
        the audio thread only ever reads a raw, borrowed pointer from;
        freeing only ever happens via `reclaim()`, called from a
        non-audio thread once the audio thread's own recorded epoch
        proves it can no longer be reading the slot being freed.

        **Single-reader, same as `PlanSwapper`, and for the same reason**:
        one `DataPublisher` per Data-typed *output port instance*, read
        from *the* audio thread — this codebase has exactly one (voices
        are processed sequentially within a single `processBlock` call,
        not on separate concurrent OS threads per NODE_EDITOR.md/
        ARCHITECTURE.md's actual threading model). Several consuming
        ports may each call `getCurrentForAudioThread()` on the same
        publisher, any number of times per block — that's fine, they run
        sequentially on the one audio thread, never concurrently with
        each other. An earlier draft of this class mistakenly generalized
        to "any number of concurrent readers" and used a single shared
        epoch to prove it — that's genuinely unsound (a fast reader's
        epoch update can hide a slow reader's still-in-flight slot from
        `reclaim()`, a real use-after-free caught by this file's own
        stress test). Do not add a second genuinely-concurrent reader
        thread to this class without redesigning the epoch scheme
        (per-reader hazard tracking or similar) — the current one only
        proves correct for exactly one.

        `getCurrentForAudioThread()` only loads and records an epoch, it
        never mutates shared state other than its own atomics — cheap
        enough to call repeatedly, sequentially, within one block (e.g.
        once per consuming port that happens to read this publisher),
        exactly like `PlanSwapper` is called once per voice's plan.
    */
    class DataPublisher
    {
    public:
        // Smaller than PlanSwapper's 16 — a Data publish is a discrete,
        // user-driven edit (a drawn curve, a loaded sample, a changed
        // material), not a per-block stream; a burst of more than a
        // handful of back-to-back publishes with no reclaim() in between
        // is not a realistic scenario the way rapid graph edits are.
        static constexpr int numSlots = 8;

        /** Non-audio-thread side. Returns false if every slot is still
            occupied (reclaim() hasn't freed one yet) — the caller keeps
            its buffer and retries after the next reclaim().
        */
        bool publish (std::unique_ptr<DataBuffer> buffer) noexcept
        {
            int freeSlot = -1;

            for (int i = 0; i < numSlots; ++i)
            {
                if (! slotOccupied[(size_t) i].load (std::memory_order_acquire))
                {
                    freeSlot = i;
                    break;
                }
            }

            if (freeSlot == -1)
                return false;

            buffer->generation = nextGeneration++;
            slots[(size_t) freeSlot] = std::move (buffer);
            slotOccupied[(size_t) freeSlot].store (true, std::memory_order_release);
            currentSlot.store (freeSlot, std::memory_order_release);
            return true;
        }

        /** Audio-thread side. Safe to call every sample or once per block
            — either way, allocation-free and never blocks. Returns
            nullptr if nothing has ever been published (a required Data
            port with nothing wired is a compile-time rejection per
            SIGNAL_TYPES.md §5, so a real graph should never actually hit
            this, but an unpublished/newly-constructed publisher must still
            behave safely for tests and prepare()-time calls).
        */
        const DataBuffer* getCurrentForAudioThread() noexcept
        {
            const auto slot = currentSlot.load (std::memory_order_acquire);
            const auto* buffer = slot >= 0 ? slots[(size_t) slot].get() : nullptr;

            if (buffer != nullptr)
                audioThreadEpoch.store (buffer->generation, std::memory_order_release);

            return buffer;
        }

        /** Non-audio-thread side, called periodically (same cadence as
            `PlanSwapper::reclaim()` — the plugin layer's existing ~50ms
            message-thread timer is the natural place to also call this).
            Frees any slot that's neither the current buffer nor still
            possibly being read by the audio thread.
        */
        void reclaim() noexcept
        {
            const auto liveSlot = currentSlot.load (std::memory_order_acquire);
            const auto epoch = audioThreadEpoch.load (std::memory_order_acquire);

            for (int i = 0; i < numSlots; ++i)
            {
                if (i == liveSlot || ! slotOccupied[(size_t) i].load (std::memory_order_acquire))
                    continue;

                // slots[i]->generation: safe to read without atomics here —
                // publish() (the only writer of both the slot and its
                // buffer's generation) and reclaim() always run on the same
                // non-audio thread, exactly like ExecutionPlan::generation.
                if (slots[(size_t) i]->generation < epoch)
                {
                    slots[(size_t) i].reset();
                    slotOccupied[(size_t) i].store (false, std::memory_order_release);
                }
            }
        }

        int getNumOccupiedSlots() const noexcept
        {
            int count = 0;
            for (int i = 0; i < numSlots; ++i)
                if (slotOccupied[(size_t) i].load (std::memory_order_acquire))
                    ++count;
            return count;
        }

    private:
        std::array<std::unique_ptr<DataBuffer>, numSlots> slots;
        std::array<std::atomic<bool>, numSlots> slotOccupied {};
        std::atomic<int> currentSlot { -1 };
        std::atomic<uint64_t> audioThreadEpoch { 0 };
        // Written only by publish(), which always runs on the same
        // non-audio thread as reclaim() — plain value suffices.
        uint64_t nextGeneration = 1;
    };

    /** SIGNAL_TYPES.md §2: "consumers declare which semantic tags they
        accept. A Modal Bank accepts modal-set; wiring a scale into it is
        rejected at compile time." This is that rule, standalone for now —
        M16 gives it a real `canConnect` call site; until then it's tested
        directly against `DataTag`/`DataBuffer` (this milestone's own exit
        criteria). No wildcard: `DataTag::Unknown` never matches, on either
        side.
    */
    inline bool dataTagAccepted (DataTag produced, DataTag required) noexcept
    {
        return produced == required && produced != DataTag::Unknown;
    }
}
