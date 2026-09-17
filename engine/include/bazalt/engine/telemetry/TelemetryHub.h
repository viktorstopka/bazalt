#pragma once

#include "bazalt/engine/telemetry/Tap.h"
#include "bazalt/engine/telemetry/TelemetryFrame.h"
#include "bazalt/engine/telemetry/TelemetryFrameBuffer.h"
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace bazalt::engine
{
    /** Owns a fixed, preallocated pool of named taps and their
        per-frame-type published buffers (M8, NODE_EDITOR.md §9 —
        superseding M4's fixed 5-tap set). `maxTaps` slots are allocated
        once in prepare(); subscribeTap()/unsubscribeTap() (message-thread
        only) assign/release slots by name at runtime, LRU-evicting the
        least-recently-subscribed slot when the pool is full rather than
        growing it — "many small, dynamic taps... subscribed on demand,
        driven by what is visible in the viewport" needs a bounded cost,
        not an unbounded one.

        Threading (this is the part that matters):
        - subscribeTap()/unsubscribeTap()/getFrameBuffer() (by name): message
          thread only (the WebView resource provider and any future command
          handler both run there — never concurrent with each other, so a
          plain, non-atomic name->slot lookup is safe).
        - A slot's Tap* returned by subscribeTap() is STABLE for the life of
          this TelemetryHub, even across that slot being reused for a
          different name later — the *pointer* never moves, only what it
          currently represents does. Whoever pushes into it should cache
          that pointer once (exactly M4's tapPointers[] pattern) rather than
          re-resolving by name on every push.
        - The analysis thread never looks up by name at all — it iterates
          slot indices 0..maxTaps-1 via isSlotActive()/getTapBySlot()/
          getFrameBufferBySlot(), reading only the atomic `active` flag
          cross-thread. This sidesteps needing `juce::String` to be safely
          readable across threads entirely.
        - Known, accepted gap (documented in CLAUDE.md): if a slot is
          unsubscribed and immediately reused for a different name while an
          old pusher (e.g. a not-yet-reclaimed stale ExecutionPlan, M7's
          PlanSwapper epoch mechanism) still holds that slot's stable Tap*,
          the new subscriber can see a few stray samples from the old one
          until the old pusher is retired. Telemetry is already bounded-
          stale/best-effort (ADR-0005) — a brief cross-talk glitch on reuse
          is an acceptable, visible-only-as-one-frame-of-noise cost, not a
          safety issue. Revisit if M11's real per-node wiring makes this
          worse than that in practice.
    */
    class TelemetryHub
    {
    public:
        static constexpr size_t maxTaps = 64;

        void prepare (size_t tapCapacitySamples, size_t maxFrameBytes)
        {
            for (auto& slot : slots)
            {
                slot.tap = std::make_unique<Tap>();
                slot.tap->prepare (tapCapacitySamples);

                for (auto& buffer : slot.frameBuffers)
                {
                    buffer = std::make_unique<TelemetryFrameBuffer>();
                    buffer->prepare (maxFrameBytes);
                }

                slot.active.store (false, std::memory_order_relaxed);
                slot.synthetic.store (false, std::memory_order_relaxed);
                slot.name.clear();
                slot.lastUsedSequence = 0;
            }

            sequenceCounter = 0;
        }

        /** Message-thread only. Returns a stable Tap* for `name` — if
            already subscribed, refreshes its LRU timestamp and returns the
            existing slot; otherwise claims a free slot, or LRU-evicts the
            least-recently-subscribed active slot if the pool is full.
            Never returns nullptr — with maxTaps slots there is always an
            eviction victim.
        */
        Tap* subscribeTap (const juce::String& name)
        {
            if (auto* existing = findSlotByName (name))
            {
                existing->lastUsedSequence = ++sequenceCounter;
                return existing->tap.get();
            }

            auto* slot = findFreeSlot();
            if (slot == nullptr)
                slot = findLruVictim();

            slot->name = name;
            slot->lastUsedSequence = ++sequenceCounter;
            slot->tap->prepare (slot->tap->getCapacity()); // reset ring contents for the new subscriber
            // "demo."-prefixed taps have no real pusher (M8's UI-only
            // rendering stress test subscribes synthetic per-cable taps
            // that don't correspond to any real engine signal, on purpose
            // — see StressTestCanvas.tsx) — AnalysisThread generates their
            // content itself instead of waiting for data that will never
            // arrive. Decided at subscribe time (message thread) and
            // stored as an atomic flag so the analysis thread never needs
            // to read `name` itself (TelemetryHub's threading note above).
            slot->synthetic.store (name.startsWith ("demo."), std::memory_order_relaxed);
            slot->active.store (true, std::memory_order_release);

            return slot->tap.get();
        }

        /** Message-thread only. No-op if `name` isn't currently subscribed. */
        void unsubscribeTap (const juce::String& name)
        {
            if (auto* slot = findSlotByName (name))
            {
                slot->active.store (false, std::memory_order_release);
                slot->name.clear();
            }
        }

        /** Message-thread only (e.g. the WebView resource provider fetching
            by the name a URL names).
        */
        TelemetryFrameBuffer* getFrameBuffer (const juce::String& name, TelemetryFrameType type) noexcept
        {
            auto* slot = findSlotByName (name);
            return slot == nullptr ? nullptr : slot->frameBuffers[(size_t) type].get();
        }

        size_t getNumActiveTaps() const noexcept
        {
            size_t count = 0;
            for (const auto& slot : slots)
                if (slot.active.load (std::memory_order_acquire))
                    ++count;
            return count;
        }

        // ---- Analysis-thread-facing iteration: slot-indexed, no names ----

        bool isSlotActive (size_t slotIndex) const noexcept
        {
            return slots[slotIndex].active.load (std::memory_order_acquire);
        }

        bool isSlotSynthetic (size_t slotIndex) const noexcept
        {
            return slots[slotIndex].synthetic.load (std::memory_order_relaxed);
        }

        Tap* getTapBySlot (size_t slotIndex) const noexcept { return slots[slotIndex].tap.get(); }

        TelemetryFrameBuffer* getFrameBufferBySlot (size_t slotIndex, TelemetryFrameType type) const noexcept
        {
            return slots[slotIndex].frameBuffers[(size_t) type].get();
        }

    private:
        struct Slot
        {
            juce::String name; // message-thread-owned only
            std::atomic<bool> active { false };
            std::atomic<bool> synthetic { false }; // see subscribeTap()'s "demo." note
            uint64_t lastUsedSequence = 0; // message-thread-owned only
            std::unique_ptr<Tap> tap;
            std::array<std::unique_ptr<TelemetryFrameBuffer>, 3> frameBuffers;
        };

        Slot* findSlotByName (const juce::String& name) noexcept
        {
            for (auto& slot : slots)
                if (slot.active.load (std::memory_order_relaxed) && slot.name == name)
                    return &slot;
            return nullptr;
        }

        Slot* findFreeSlot() noexcept
        {
            for (auto& slot : slots)
                if (! slot.active.load (std::memory_order_relaxed))
                    return &slot;
            return nullptr;
        }

        Slot* findLruVictim() noexcept
        {
            Slot* victim = nullptr;
            for (auto& slot : slots)
                if (victim == nullptr || slot.lastUsedSequence < victim->lastUsedSequence)
                    victim = &slot;
            return victim;
        }

        std::array<Slot, maxTaps> slots;
        uint64_t sequenceCounter = 0;
    };
}
