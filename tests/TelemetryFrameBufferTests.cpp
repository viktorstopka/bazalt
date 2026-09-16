#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/telemetry/TelemetryFrameBuffer.h"
#include <algorithm>

using namespace bazalt::engine;

TEST_CASE ("TelemetryFrameBuffer::readLatest returns the most recently published frame", "[engine][telemetry]")
{
    TelemetryFrameBuffer buffer;
    buffer.prepare (64);

    std::byte frame1[8];
    std::fill (std::begin (frame1), std::end (frame1), std::byte { 1 });
    buffer.publish (frame1, sizeof (frame1));

    std::byte frame2[8];
    std::fill (std::begin (frame2), std::end (frame2), std::byte { 2 });
    buffer.publish (frame2, sizeof (frame2));

    std::byte dest[64] = {};
    const auto numBytes = buffer.readLatest (dest, sizeof (dest));

    REQUIRE (numBytes == 8);
    for (size_t i = 0; i < 8; ++i)
        CHECK (dest[i] == std::byte { 2 });
}

TEST_CASE ("TelemetryFrameBuffer::readLatest returns 0 before anything is published", "[engine][telemetry]")
{
    TelemetryFrameBuffer buffer;
    buffer.prepare (64);

    std::byte dest[64] = {};
    CHECK (buffer.readLatest (dest, sizeof (dest)) == 0);
}
