#pragma once

#include <exception>

namespace bazalt::engine
{
    /** Thrown by the overridden global operator new when an allocation is
        attempted while the trap is armed on the calling thread. Only ever
        thrown in builds compiled with BAZALT_ENABLE_RT_ALLOCATION_TRAP (on
        by default in Debug — see engine/CMakeLists.txt). This is the
        debug-build RT-allocation trap from ARCHITECTURE.md §5: it catches
        accidental allocation on the audio thread instead of just asserting,
        so a violation is a deterministic, catchable failure in tests rather
        than something that only shows up under a debugger.
    */
    struct AudioThreadAllocationViolation : std::exception
    {
        const char* what() const noexcept override
        {
            return "Allocation attempted while the audio-thread allocation trap was armed";
        }
    };

    // Caveat worth knowing before relying on catching this: if the
    // allocation that trips the trap happens inside a function marked
    // noexcept, the thrown exception can't propagate — it calls
    // std::terminate() instead (standard C++ behaviour for an exception
    // escaping noexcept). In practice this bites the single most common
    // accidental-allocation mistake: MSVC's debug STL (_ITERATOR_DEBUG_LEVEL
    // == 2, the Debug default) allocates a small "container proxy" inside
    // std::vector's DEFAULT CONSTRUCTOR, which is itself noexcept for a
    // stateless allocator like std::allocator<int> — so `std::vector<float>
    // temp;` on the audio thread terminates the process rather than
    // throwing a catchable AudioThreadAllocationViolation. That's still a
    // loud, unambiguous failure (the point of the trap), just not a
    // recoverable one in a test's try/catch — don't build a test around
    // catching a violation from inside a noexcept call path.

    // Nestable per-thread arm/disarm. Not for direct use — prefer
    // ScopedAudioThreadAllocationTrap below.
    void pushAudioThreadAllocationTrap() noexcept;
    void popAudioThreadAllocationTrap() noexcept;
    bool isAudioThreadAllocationTrapArmed() noexcept;

    /** RAII scope guard: brackets the region where allocation must not
        happen (e.g. around a processBlock call in tests). Nestable.
    */
    class ScopedAudioThreadAllocationTrap
    {
    public:
        ScopedAudioThreadAllocationTrap() noexcept { pushAudioThreadAllocationTrap(); }
        ~ScopedAudioThreadAllocationTrap() noexcept { popAudioThreadAllocationTrap(); }

        ScopedAudioThreadAllocationTrap (const ScopedAudioThreadAllocationTrap&) = delete;
        ScopedAudioThreadAllocationTrap& operator= (const ScopedAudioThreadAllocationTrap&) = delete;
    };
}
