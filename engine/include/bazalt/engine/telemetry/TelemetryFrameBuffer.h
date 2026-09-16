#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

namespace bazalt::engine
{
    /** Lock-free single-writer (analysis thread) / single-reader (whoever
        serves the WebView resource-provider request, typically the
        message thread) publish for one telemetry stream, via the same
        atomic-index-swap pattern PlanSwapper uses for ExecutionPlan
        (ARCHITECTURE.md §3.2, §6.2). Three preallocated slots: the writer
        always writes into (current + 1) mod 3 and never touches the slot
        it just published, so a reader mid-copy from the current slot is
        never disturbed — the classic triple-buffer technique, safe here
        because the writer can't lap the reader within one publish (the
        reader's copy is a fast memcpy, not held across multiple
        publishes).
    */
    class TelemetryFrameBuffer
    {
    public:
        static constexpr int numSlots = 3;

        void prepare (size_t maxFrameBytes)
        {
            for (auto& slot : slots)
                slot.assign (maxFrameBytes, std::byte { 0 });
        }

        /** Analysis thread. Publishes a complete frame (already serialized
            by the caller, e.g. via serializeTelemetryFrame) as the new
            latest value for this stream.
        */
        void publish (const std::byte* frameData, size_t frameBytes) noexcept
        {
            const auto current = currentSlot.load (std::memory_order_acquire);
            const auto next = (current + 1) % numSlots;

            const auto bytesToCopy = std::min (frameBytes, slots[(size_t) next].size());
            std::memcpy (slots[(size_t) next].data(), frameData, bytesToCopy);

            slotLengths[(size_t) next].store (bytesToCopy, std::memory_order_relaxed);
            currentSlot.store (next, std::memory_order_release);
        }

        /** Reader side. Copies the latest published frame into dest
            (capacity destCapacity) and returns how many bytes were
            written — 0 if nothing has ever been published.
        */
        size_t readLatest (std::byte* dest, size_t destCapacity) const noexcept
        {
            const auto slot = currentSlot.load (std::memory_order_acquire);
            const auto length = std::min (slotLengths[(size_t) slot].load (std::memory_order_relaxed), destCapacity);

            std::memcpy (dest, slots[(size_t) slot].data(), length);
            return length;
        }

    private:
        std::array<std::vector<std::byte>, numSlots> slots;
        std::array<std::atomic<size_t>, numSlots> slotLengths {};
        std::atomic<int> currentSlot { 0 };
    };
}
