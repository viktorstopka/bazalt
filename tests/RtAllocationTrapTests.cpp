#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/RtAllocationTrap.h"

TEST_CASE ("ScopedAudioThreadAllocationTrap throws when an allocation happens inside it", "[engine][RtAllocationTrap]")
{
    bool threw = false;

    try
    {
        bazalt::engine::ScopedAudioThreadAllocationTrap trap;
        int* p = new int (5); // heap allocation — should be caught
        delete p;
    }
    catch (const bazalt::engine::AudioThreadAllocationViolation&)
    {
        threw = true;
    }

   #if BAZALT_ENABLE_RT_ALLOCATION_TRAP
    CHECK (threw);
   #else
    CHECK_FALSE (threw); // trap is compiled out entirely outside Debug
   #endif
}

TEST_CASE ("ScopedAudioThreadAllocationTrap allows allocation-free code to run untouched", "[engine][RtAllocationTrap]")
{
    int sum = 0;

    {
        bazalt::engine::ScopedAudioThreadAllocationTrap trap;

        for (int i = 0; i < 1000; ++i)
            sum += i;
    }

    CHECK (sum == 499500);
}

TEST_CASE ("The trap is armed/disarmed correctly, including nested scopes", "[engine][RtAllocationTrap]")
{
    CHECK_FALSE (bazalt::engine::isAudioThreadAllocationTrapArmed());

    {
        bazalt::engine::ScopedAudioThreadAllocationTrap outer;
        CHECK (bazalt::engine::isAudioThreadAllocationTrapArmed());

        {
            bazalt::engine::ScopedAudioThreadAllocationTrap inner;
            CHECK (bazalt::engine::isAudioThreadAllocationTrapArmed());
        }

        CHECK (bazalt::engine::isAudioThreadAllocationTrapArmed()); // still armed by outer
    }

    CHECK_FALSE (bazalt::engine::isAudioThreadAllocationTrapArmed());
}
