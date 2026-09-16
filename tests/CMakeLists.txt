# Catch2 suite, linked against engine ONLY — never plugin — so it stays
# headless. See docs/ARCHITECTURE.md §8 for the full suite this grows into
# starting M1 (aliasing, SVF response, block-size invariance, etc).

add_executable(EngineTests
    AlignedBufferTests.cpp
    BlockSizeInvarianceTests.cpp
    EngineVersionTests.cpp
    GraphCompilerTests.cpp
    NanGuardTests.cpp
    OversamplingStageTests.cpp
    PlanSwapperTests.cpp
    PolyBlepOscillatorTests.cpp
    ProofGraphRenderTests.cpp
    RtAllocationTrapTests.cpp
    SampleRateCoverageTests.cpp
    SmoothedParameterTests.cpp
    SvfFilterTests.cpp
    VoiceManagerTests.cpp
)

target_link_libraries(EngineTests
    PRIVATE
        bazalt::engine
        Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(EngineTests)
