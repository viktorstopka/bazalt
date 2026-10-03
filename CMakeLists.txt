cmake_minimum_required(VERSION 3.24)

project(Bazalt VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS YES)

# Real Release builds ship with real debug symbols too — same optimization
# level, just not symbol-stripped. Found missing the first time this plugin
# was ever loaded by a real DAW (Ableton) and it crashed: the Release build
# had zero PDBs anywhere, so even a Windows Event Viewer crash record would
# only ever say "some module, some hex offset" — completely undiagnosable
# beyond that. Applied globally, before JUCE/Catch2 are even fetched, so a
# crash inside JUCE's own code (entirely plausible — this is a WebView2-
# backed editor) is just as symbolicated as a crash inside Bazalt's own.
# `/Z7` (not `/Zi`) embeds debug info directly in each .obj rather than a
# shared per-directory .pdb, so parallel compiles of the same static library
# (engine) can never contend over one shared file; the final link step
# (`/DEBUG`) then collects all of it into one real .pdb next to each actual
# output binary. Debug builds are untouched — MSVC already defaults to full
# symbols there.
if (MSVC)
    add_compile_options ($<$<CONFIG:Release>:/Z7>)
    add_link_options ($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()

option(BAZALT_BUILD_TESTS "Build the Catch2 engine test suite" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(FetchContent)

# ---------------------------------------------------------------------------
# JUCE — pinned to an exact tag (never a moving branch), see
# docs/decisions/0001-juce-version-and-license.md
#
# PATCH_COMMAND: disables WebView2's own native Ctrl+MouseWheel/pinch zoom in
# the vendored Windows WebBrowserComponent backend — see
# archive_docs/decisions/0032-disable-webview2-native-zoom.md and
# cmake/ApplyJuceWebView2ZoomPatch.cmake's own header comment for the full
# reasoning. Runs once, only on a fresh fetch (FetchContent's own documented
# behaviour — a `build/_deps/juce-src` that already exists is left alone),
# and is itself idempotent if CMake ever re-runs it anyway.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.2
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
    PATCH_COMMAND  ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyJuceWebView2ZoomPatch.cmake"
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Catch2 — tests/ (engine-only, headless) and tests-plugin/ (links the
# plugin's shared-code target directly). Never linked into tools/render-cli.
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
add_subdirectory(tools/render-cli)

if(BAZALT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(tests-plugin)
endif()
