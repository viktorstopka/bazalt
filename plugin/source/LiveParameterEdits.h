#pragma once

#include "bazalt/engine/graph/ExecutionPlan.h"
#include <array>
#include <atomic>
#include <cmath>

namespace bazalt
{
    /** Values that move WHILE a slider is being dragged — so you can sweep a
        property to find where it sounds best, hear every step of the way, and
        let go there. A drag never recompiles: the UI streams values here
        (graphSetParameterLive), the audio thread glides each node's value
        toward them, and only the final value on release is committed as an
        ordinary graph edit — which, being value-only, keeps the running node
        (ExecutionPlan::pendingParameterUpdates), so letting go doesn't click
        either.

        Smoothing: each block moves the applied value a fixed fraction of the
        way to the target (a one-pole glide, ~15 ms time constant), so a
        drag's coarse, irregular UI updates become a continuous sweep instead
        of audible steps.

        Threading — the same shape as MacroParameters: a small fixed pool of
        slots. The message thread claims a free slot for a (node, parameter)
        pair, writes its ids, then publishes it (`state` release); after
        that it only writes the atomic target. The audio thread reads ids
        only from published slots, and returns a slot to the pool once it has
        been released and its glide has arrived. Ids are never written while
        a slot is published, so the audio thread never sees one change.
        Nothing here allocates on the audio thread (a juce::String compare
        and an ExecutionPlan id lookup by an existing string do not).
    */
    class LiveParameterEdits
    {
    public:
        static constexpr int maxSlots = 8;
        static constexpr double glideTimeConstantSeconds = 0.015;

        void prepare (double newSampleRate) noexcept { sampleRate = newSampleRate; }

        /** Message thread. Starts or continues a live edit. False if every
            slot is busy (the edit then just takes effect on commit). */
        bool set (const juce::String& nodeId, const juce::String& parameterId, float value)
        {
            if (auto* slot = findPublished (nodeId, parameterId))
            {
                slot->target.store (value, std::memory_order_relaxed);
                slot->released.store (false, std::memory_order_relaxed);
                return true;
            }

            for (auto& slot : slots)
            {
                if (slot.state.load (std::memory_order_acquire) != State::free)
                    continue;
                slot.nodeId = nodeId;
                slot.parameterId = parameterId;
                slot.target.store (value, std::memory_order_relaxed);
                slot.released.store (false, std::memory_order_relaxed);
                slot.hasCurrent = false;
                slot.state.store (State::published, std::memory_order_release);
                return true;
            }
            return false;
        }

        /** Message thread: is a live edit of this pair already running? */
        bool isActive (const juce::String& nodeId, const juce::String& parameterId) noexcept
        {
            return findPublished (nodeId, parameterId) != nullptr;
        }

        /** Message thread, on release: the slot finishes its glide to the
            last target and then frees itself. */
        void release (const juce::String& nodeId, const juce::String& parameterId) noexcept
        {
            if (auto* slot = findPublished (nodeId, parameterId))
                slot->released.store (true, std::memory_order_relaxed);
        }

        /** Audio thread, once per block, before the plans process. */
        void advance (int numSamples) noexcept
        {
            const auto alpha = sampleRate > 0.0 ? (float) (1.0 - std::exp (-(double) numSamples / (glideTimeConstantSeconds * sampleRate))) : 1.0f;
            for (auto& slot : slots)
            {
                if (slot.state.load (std::memory_order_acquire) != State::published)
                    continue;
                const auto target = slot.target.load (std::memory_order_relaxed);
                if (! slot.hasCurrent)
                {
                    slot.current = target; // the first value of a drag applies at once
                    slot.hasCurrent = true;
                }
                else
                    slot.current += (target - slot.current) * alpha;

                slot.arrived = std::abs (target - slot.current) <= 1.0e-6f * std::max (1.0f, std::abs (target));
                if (slot.arrived)
                    slot.current = target;
            }
        }

        /** Audio thread: applies every published slot to `plans`. */
        void applyToPlans (bazalt::engine::ExecutionPlan* const* plans, int numPlans) noexcept
        {
            for (auto& slot : slots)
            {
                if (slot.state.load (std::memory_order_acquire) != State::published || ! slot.hasCurrent)
                    continue;
                for (int i = 0; i < numPlans; ++i)
                    if (plans[i] != nullptr)
                        if (auto* node = plans[i]->getNodeById (slot.nodeId))
                            node->setParameter (slot.parameterId, slot.current);
            }
        }

        /** Audio thread, after every applyToPlans() of the block: frees the
            slots whose drag has ended and whose glide has arrived. */
        void retireFinished() noexcept
        {
            for (auto& slot : slots)
                if (slot.state.load (std::memory_order_relaxed) == State::published
                    && slot.released.load (std::memory_order_relaxed) && slot.arrived)
                    slot.state.store (State::free, std::memory_order_release);
        }

    private:
        enum class State : int { free, published };

        struct Slot
        {
            std::atomic<State> state { State::free };
            juce::String nodeId, parameterId; // written only while free (message thread)
            std::atomic<float> target { 0.0f };
            std::atomic<bool> released { false };
            float current = 0.0f; // audio thread only
            bool hasCurrent = false;
            bool arrived = false;
        };

        Slot* findPublished (const juce::String& nodeId, const juce::String& parameterId) noexcept
        {
            for (auto& slot : slots)
                if (slot.state.load (std::memory_order_acquire) == State::published && slot.nodeId == nodeId && slot.parameterId == parameterId)
                    return &slot;
            return nullptr;
        }

        std::array<Slot, maxSlots> slots;
        double sampleRate = 44100.0;
    };
}
