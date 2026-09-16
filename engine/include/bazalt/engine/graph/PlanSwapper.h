#pragma once

#include "bazalt/engine/graph/ExecutionPlan.h"
#include <array>
#include <atomic>
#include <memory>

namespace bazalt::engine
{
    /** Lock-free single-writer (compiler thread) / single-reader (audio
        thread) publish + epoch-based reclamation for ExecutionPlan swaps
        (ARCHITECTURE.md §3.2). A fixed pool of plan slots (4 for MVP) —
        the compiler thread builds a new plan, then publishes it via one
        atomic store; the audio thread loads the plan pointer ONCE per
        process() call — never mid-block — runs the whole block against
        it, then records its own epoch. Because the pointer load happens
        exactly once per call, a mid-stream publish from another thread can
        never be observed partway through a block: the invariant that
        matters ("no discontinuity within a single process() call") holds
        by construction, not by care.

        engine has no Timer (it's headless — see engine/CMakeLists.txt's
        dependency rule) so reclaim() isn't scheduled by this class; the
        plugin layer's message-thread timer (~50ms per ARCHITECTURE.md
        §3.2) calls it periodically.
    */
    class PlanSwapper
    {
    public:
        static constexpr int numSlots = 4;

        /** Compiler-thread side. Returns false if every slot is still
            occupied (reclaim() hasn't freed one yet) — the caller keeps
            its plan and retries after the next reclaim().
        */
        bool publish (std::unique_ptr<ExecutionPlan> plan) noexcept
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

            slots[(size_t) freeSlot] = std::move (plan);
            slotOccupied[(size_t) freeSlot].store (true, std::memory_order_release);
            currentSlot.store (freeSlot, std::memory_order_release);
            return true;
        }

        /** Audio-thread side: call exactly once per process() call, before
            processing any samples for that call.
        */
        ExecutionPlan* getCurrentPlanForAudioThread() noexcept
        {
            const auto slot = currentSlot.load (std::memory_order_acquire);
            auto* plan = slot >= 0 ? slots[(size_t) slot].get() : nullptr;

            if (plan != nullptr)
                audioThreadEpoch.store (plan->generation, std::memory_order_release);

            return plan;
        }

        /** Message-thread side, called periodically. Frees any slot that's
            neither the current plan nor still possibly in use by the audio
            thread (tracked via the last epoch it recorded).
        */
        void reclaim() noexcept
        {
            const auto liveSlot = currentSlot.load (std::memory_order_acquire);
            const auto epoch = audioThreadEpoch.load (std::memory_order_acquire);

            for (int i = 0; i < numSlots; ++i)
            {
                if (i == liveSlot || ! slotOccupied[(size_t) i].load (std::memory_order_acquire))
                    continue;

                auto* plan = slots[(size_t) i].get();
                if (plan != nullptr && plan->generation < epoch)
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
        std::array<std::unique_ptr<ExecutionPlan>, numSlots> slots;
        std::array<std::atomic<bool>, numSlots> slotOccupied {};
        std::atomic<int> currentSlot { -1 };
        std::atomic<uint64_t> audioThreadEpoch { 0 };
    };
}
