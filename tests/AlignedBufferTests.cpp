#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/AlignedBuffer.h"
#include <cstdint>

TEST_CASE ("AlignedBuffer resizes and reports correct dimensions", "[engine][AlignedBuffer]")
{
    bazalt::engine::AlignedBuffer buffer;
    buffer.resize (2, 512);

    CHECK (buffer.getNumChannels() == 2);
    CHECK (buffer.getNumSamples() == 512);
}

TEST_CASE ("AlignedBuffer storage is SIMD-aligned", "[engine][AlignedBuffer]")
{
    bazalt::engine::AlignedBuffer buffer;
    buffer.resize (2, 64);

    for (size_t ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* ptr = buffer.getBlock().getChannelPointer (ch);
        const auto address = reinterpret_cast<std::uintptr_t> (ptr);
        CHECK ((address % 16) == 0);
    }
}

TEST_CASE ("AlignedBuffer::clear zeroes all samples", "[engine][AlignedBuffer]")
{
    bazalt::engine::AlignedBuffer buffer;
    buffer.resize (1, 16);

    auto* ptr = buffer.getBlock().getChannelPointer (0);
    for (size_t i = 0; i < buffer.getNumSamples(); ++i)
        ptr[i] = 1.0f;

    buffer.clear();

    for (size_t i = 0; i < buffer.getNumSamples(); ++i)
        CHECK (ptr[i] == 0.0f);
}
