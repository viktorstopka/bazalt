#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/telemetry/Tap.h"

using namespace bazalt::engine;

TEST_CASE ("Tap::readLatest returns exactly what was pushed, in order", "[engine][Tap]")
{
    Tap tap;
    tap.prepare (16);

    float input[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    tap.push (input, 8);

    float output[8] = {};
    const auto numRead = tap.readLatest (output, 8);

    REQUIRE (numRead == 8);
    for (int i = 0; i < 8; ++i)
        CHECK (output[i] == (float) i);
}

TEST_CASE ("Tap::readLatest returns fewer samples than requested if not enough have been pushed yet", "[engine][Tap]")
{
    Tap tap;
    tap.prepare (16);

    float input[3] = { 1, 2, 3 };
    tap.push (input, 3);

    float output[8] = {};
    const auto numRead = tap.readLatest (output, 8);

    REQUIRE (numRead == 3);
    CHECK (output[0] == 1.0f);
    CHECK (output[1] == 2.0f);
    CHECK (output[2] == 3.0f);
}

TEST_CASE ("Tap overwrites the oldest samples once capacity is exceeded, never blocking", "[engine][Tap]")
{
    Tap tap;
    tap.prepare (4);

    float input[6] = { 1, 2, 3, 4, 5, 6 };
    tap.push (input, 6); // capacity 4 -> only the last 4 survive: 3,4,5,6

    float output[4] = {};
    const auto numRead = tap.readLatest (output, 4);

    REQUIRE (numRead == 4);
    CHECK (output[0] == 3.0f);
    CHECK (output[1] == 4.0f);
    CHECK (output[2] == 5.0f);
    CHECK (output[3] == 6.0f);
}

TEST_CASE ("Tap::readLatest caps at the buffer's capacity even if more were requested", "[engine][Tap]")
{
    Tap tap;
    tap.prepare (4);

    float input[10];
    for (int i = 0; i < 10; ++i)
        input[i] = (float) i;
    tap.push (input, 10);

    float output[100] = {};
    const auto numRead = tap.readLatest (output, 100);

    CHECK (numRead == 4);
}
