cmake_minimum_required(VERSION 3.24)

project(Bazalt VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS YES)

option(BAZALT_BUILD_TESTS "Build the Catch2 engine test suite" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(FetchContent)

# ---------------------------------------------------------------------------
# JUCE — pinned to an exact tag (never a moving branch), see
# docs/decisions/0001-juce-version-and-license.md
# ---------------------------------------------------------------------------
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.2
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Catch2 — engine test suite only, never linked into plugin/ or tools/
# ---------------------------------------------------------------------------
if(BAZALT_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.9.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

add_subdirectory(engine)
add_subdirectory(plugin)

if(BAZALT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
