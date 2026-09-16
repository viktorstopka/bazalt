#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace bazalt::engine
{
    /** Belongs at the final output stage: replaces non-finite samples with
        silence and raises a non-blocking, lock-free fault flag the UI can
        surface, rather than ever sending NaN/Inf to the host's speakers
        (ARCHITECTURE.md §5).
    */
    class NanGuard
    {
    public:
        void process (juce::AudioBuffer<float>& buffer) noexcept;

        /** Returns true if a fault was caught since the last call, clearing the flag. */
        bool consumeFaultFlag() noexcept { return faultFlag.exchange (false, std::memory_order_relaxed); }

    private:
        std::atomic<bool> faultFlag { false };
    };
}
