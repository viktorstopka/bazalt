#pragma once

#include "bazalt/engine/telemetry/Tap.h"
#include "bazalt/engine/telemetry/TapSettings.h"
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
    /** M20: which frame type(s) a subscriber actually needs computed for a
        tap — a Waveform preview only needs Oscilloscope frames, a Meter
        preview only needs Meter frames, etc. Every field defaults true so
        an existing caller that doesn't pass this (M4's baseline main/aux
        taps, still wanting all three for the M5 analysis panel) keeps
        getting exactly today's behaviour unchanged.
    */
    struct TelemetryFrameTypesNeeded
    {
        bool oscilloscope = true;
        bool spectrum = true;
        bool meter = true;
        bool eventImpulse = true;
        bool rollingHistory = true;
    };

    class TelemetryHub
    {
    public:
        static constexpr size_t maxTaps = 64;

        /** Direct, reproducible feedback: "when i switched the output in
            settings, the ripple stopped responding." Root cause: switching
            the Standalone app's audio output device re-runs
            BazaltAudioProcessor::prepareToPlay() (a new sample rate/block
            size, or just a fresh device) — which re-prepares this hub, and
            this function used to unconditionally wipe EVERY slot, silently
            dropping every subscription, including every dynamically-
            subscribed per-node preview tap (NodePreview.tsx/RippleBody.tsx's
            own subscribeNodePreview uses this exact mechanism — not Ripple-
            specific at all, every visible node preview was equally broken
            by this). PluginProcessor::prepareToPlay() re-subscribes its own
            5 baseline main/aux taps right after calling this, which is
            exactly why only the DYNAMIC taps looked broken and the M5
            analysis panel didn't — nothing re-subscribes those. The client
            that asked for a dynamic tap has no signal at all that it needs
            to ask again (no "the engine just reset" event reaches it), so
            the fix is here: an already-active slot's SUBSCRIPTION (name/
            settings/frameTypesMask/synthetic) survives a re-prepare
            untouched, and so does its Tap* (the same object, re-prepared in
            place) — only the buffer CONTENTS are cleared below (stale/
            meaningless after a reprepare either way). A never-subscribed slot is still
            reset to the same clean "nothing here" state as before.
        */
        void prepare (size_t tapCapacitySamples, size_t maxFrameBytes)
        {
            for (auto& slot : slots)
            {
                const auto wasActive = slot.active.load (std::memory_order_relaxed);

                // Allocated once, then re-prepared IN PLACE on every later
                // call: a live ExecutionPlan (tapForBufferIndex) and
                // PluginProcessor's tapPointers[] cache these Tap*s, so
                // replacing the object here would leave the audio thread
                // pushing into freed memory — and the analysis thread
                // reading a fresh, never-written Tap (the real cause of
                // "the ripple stopped responding" after a device switch).
                if (slot.tap == nullptr)
                    slot.tap = std::make_unique<Tap>();
                slot.tap->prepare (tapCapacitySamples);

                for (auto& buffer : slot.frameBuffers)
                {
                    if (buffer == nullptr)
                        buffer = std::make_unique<TelemetryFrameBuffer>();
                    buffer->prepare (maxFrameBytes);
                }

                if (! wasActive)
                {
                    slot.active.store (false, std::memory_order_relaxed);
                    slot.synthetic.store (false, std::memory_order_relaxed);
                    slot.name.clear();
                    slot.lastUsedSequence = 0;
                }
                // else: name/active/synthetic/frameTypesMask/settings/
                // lastUsedSequence are all left exactly as they were - still
                // correct for a subscriber who was never told anything changed.
            }

            // Deliberately NOT reset: a preserved active slot keeps its own
            // (possibly large) lastUsedSequence, and resetting this counter
            // to 0 here would make every freshly-subscribed tap AFTER this
            // call look older than an untouched one to findLruVictim() below
            // - backwards. Monotonic for this TelemetryHub's whole lifetime;
            // a uint64_t never realistically overflows from subscription
            // churn alone.
        }

        /** Message-thread only. Returns a stable Tap* for `name` — if
            already subscribed, refreshes its LRU timestamp and returns the
            existing slot; otherwise claims a free slot, or LRU-evicts the
            least-recently-subscribed active slot if the pool is full.
            Never returns nullptr — with maxTaps slots there is always an
            eviction victim.
        */
        Tap* subscribeTap (const juce::String& name, TelemetryFrameTypesNeeded needed = {})
        {
            if (auto* existing = findSlotByName (name))
            {
                existing->lastUsedSequence = ++sequenceCounter;
                existing->frameTypesMask.store (frameTypesToMask (needed), std::memory_order_relaxed);
                return existing->tap.get();
            }

            auto* slot = findFreeSlot();
            if (slot == nullptr)
                slot = findLruVictim();

            slot->name = name;
            slot->lastUsedSequence = ++sequenceCounter;
            slot->tap->prepare (slot->tap->getCapacity()); // reset ring contents for the new subscriber
            slot->frameTypesMask.store (frameTypesToMask (needed), std::memory_order_relaxed);
            storeSettings (*slot, TapSettings {}); // a new subscriber starts from the defaults
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

        /** ADR-0029. Message-thread only. Sets how the named tap is analysed
            (window, FFT size, meter mode, ...); a no-op if it isn't
            subscribed. Written as relaxed atomics, so AnalysisThread picks the
            new values up on its next drain without any locking.
        */
        void setTapSettings (const juce::String& name, const TapSettings& settings)
        {
            if (auto* slot = findSlotByName (name))
                storeSettings (*slot, settings);
        }

        /** ADR-0029. Analysis thread: this slot's current settings. Fields are
            read individually, so a snapshot taken mid-update can mix old and
            new values for one drain - harmless, since every field is
            independently valid.
        */
        TapSettings getTapSettingsBySlot (size_t slotIndex) const noexcept
        {
            const auto& slot = slots[slotIndex];
            TapSettings settings;
            settings.scopeWindowSeconds = slot.scopeWindowSeconds.load (std::memory_order_relaxed);
            settings.scopeTrigger = (ScopeTriggerMode) slot.scopeTrigger.load (std::memory_order_relaxed);
            settings.historyWindowSeconds = slot.historyWindowSeconds.load (std::memory_order_relaxed);
            settings.fftOrder = slot.fftOrder.load (std::memory_order_relaxed);
            settings.spectrumTiltDbPerOctave = slot.spectrumTilt.load (std::memory_order_relaxed);
            settings.spectrumAveraging = slot.spectrumAveraging.load (std::memory_order_relaxed);
            settings.meterMode = (MeterMode) slot.meterMode.load (std::memory_order_relaxed);
            return settings;
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

        /** M20 — whether `type` is actually needed for this slot's current
            subscriber (a Waveform preview only ever needs Oscilloscope, for
            instance). AnalysisThread checks this before doing the work to
            compute+publish each frame type, so a narrowly-scoped preview
            tap stops paying for FFTs/ballistics nobody reads. Every M4
            baseline tap subscribes with the all-true default, so this is a
            pure scope *reduction* for taps that ask for it, never a
            behaviour change for anything that doesn't.
        */
        bool isFrameTypeNeeded (size_t slotIndex, TelemetryFrameType type) const noexcept
        {
            const auto mask = slots[slotIndex].frameTypesMask.load (std::memory_order_relaxed);
            return (mask & (1u << (uint32_t) type)) != 0;
        }

        Tap* getTapBySlot (size_t slotIndex) const noexcept { return slots[slotIndex].tap.get(); }

        TelemetryFrameBuffer* getFrameBufferBySlot (size_t slotIndex, TelemetryFrameType type) const noexcept
        {
            return slots[slotIndex].frameBuffers[(size_t) type].get();
        }

    private:
        template <typename SlotT>
        static void storeSettings (SlotT& slot, const TapSettings& settings) noexcept
        {
            slot.scopeWindowSeconds.store (settings.scopeWindowSeconds, std::memory_order_relaxed);
            slot.scopeTrigger.store ((int) settings.scopeTrigger, std::memory_order_relaxed);
            slot.historyWindowSeconds.store (settings.historyWindowSeconds, std::memory_order_relaxed);
            slot.fftOrder.store (std::clamp (settings.fftOrder, TapSettings::minFftOrder, TapSettings::maxFftOrder), std::memory_order_relaxed);
            slot.spectrumTilt.store (settings.spectrumTiltDbPerOctave, std::memory_order_relaxed);
            slot.spectrumAveraging.store (settings.spectrumAveraging, std::memory_order_relaxed);
            slot.meterMode.store ((int) settings.meterMode, std::memory_order_relaxed);
        }

        static uint32_t frameTypesToMask (TelemetryFrameTypesNeeded needed) noexcept
        {
            uint32_t mask = 0;
            if (needed.oscilloscope)  mask |= (1u << (uint32_t) TelemetryFrameType::Oscilloscope);
            if (needed.spectrum)      mask |= (1u << (uint32_t) TelemetryFrameType::Spectrum);
            if (needed.meter)         mask |= (1u << (uint32_t) TelemetryFrameType::Meter);
            if (needed.eventImpulse)  mask |= (1u << (uint32_t) TelemetryFrameType::EventImpulse);
            if (needed.rollingHistory) mask |= (1u << (uint32_t) TelemetryFrameType::RollingHistory);
            return mask;
        }

        struct Slot
        {
            juce::String name; // message-thread-owned only
            std::atomic<bool> active { false };
            std::atomic<bool> synthetic { false }; // see subscribeTap()'s "demo." note
            std::atomic<uint32_t> frameTypesMask { 0b11111 }; // M20 — see isFrameTypeNeeded()'s own comment; defaults to all 5

            // ADR-0029 - see TapSettings. Individually atomic; a snapshot is
            // getTapSettingsBySlot().
            std::atomic<float> scopeWindowSeconds { 0.0f };
            std::atomic<int> scopeTrigger { (int) ScopeTriggerMode::Free };
            std::atomic<float> historyWindowSeconds { 0.0f };
            std::atomic<int> fftOrder { TapSettings::defaultFftOrder };
            std::atomic<float> spectrumTilt { 0.0f };
            std::atomic<float> spectrumAveraging { 0.0f };
            std::atomic<int> meterMode { (int) MeterMode::Peak };
            uint64_t lastUsedSequence = 0; // message-thread-owned only
            std::unique_ptr<Tap> tap;
            std::array<std::unique_ptr<TelemetryFrameBuffer>, 5> frameBuffers;
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
