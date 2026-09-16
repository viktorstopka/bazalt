#include "bazalt/engine/RtAllocationTrap.h"

#if BAZALT_ENABLE_RT_ALLOCATION_TRAP

#include <cstdlib>
#include <new>

namespace bazalt::engine
{
    namespace
    {
        thread_local int trapDepth = 0;
    }

    void pushAudioThreadAllocationTrap() noexcept { ++trapDepth; }
    void popAudioThreadAllocationTrap() noexcept { --trapDepth; }
    bool isAudioThreadAllocationTrapArmed() noexcept { return trapDepth > 0; }
}

namespace
{
    // Throwing a C++ exception can itself allocate (e.g. the MSVC runtime
    // copying the exception object into its per-thread exception slot).
    // Without this guard that reentrant allocation would hit the trap again
    // and throw again mid-unwind. This flag lets exactly that one reentrant
    // allocation through as a real malloc, while the trap stays armed for
    // everything else.
    thread_local bool throwInProgress = false;

    void* allocateOrThrow (std::size_t size)
    {
        if (! throwInProgress && bazalt::engine::isAudioThreadAllocationTrapArmed())
        {
            throwInProgress = true;
            struct ResetOnUnwind
            {
                ~ResetOnUnwind() { throwInProgress = false; }
            } resetOnUnwind;

            throw bazalt::engine::AudioThreadAllocationViolation {};
        }

        if (auto* p = std::malloc (size == 0 ? 1 : size))
            return p;

        throw std::bad_alloc {};
    }
}

// Overrides the process-wide global operator new/delete once this object
// file is linked in (which only happens for binaries that actually use
// ScopedAudioThreadAllocationTrap — see RtAllocationTrap.h). Deliberately
// only the plain new/delete pair, not the nothrow or aligned-new overloads:
// nothing in the engine uses them, so leaving them un-overridden narrows
// what gets caught rather than adding untested surface area.
void* operator new (std::size_t size)
{
    return allocateOrThrow (size);
}

void* operator new[] (std::size_t size)
{
    return allocateOrThrow (size);
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

#else

namespace bazalt::engine
{
    void pushAudioThreadAllocationTrap() noexcept {}
    void popAudioThreadAllocationTrap() noexcept {}
    bool isAudioThreadAllocationTrapArmed() noexcept { return false; }
}

#endif
