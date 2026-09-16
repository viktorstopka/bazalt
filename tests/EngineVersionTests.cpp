#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include "bazalt/engine/EngineVersion.h"

TEST_CASE("Engine reports a version", "[engine][sanity]")
{
    const auto version = bazalt::engine::getVersion();

    CHECK(version.major == 0);
    CHECK(version.minor == 1);
    CHECK(version.patch == 0);

    REQUIRE(std::string_view(bazalt::engine::getVersionString()) == "0.1.0");
}
