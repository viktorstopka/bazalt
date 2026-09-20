# Catch2 suite, linked against engine ONLY — never plugin — so it stays
# headless. See docs/ARCHITECTURE.md §8 for the full suite this grows into
# starting M1 (aliasing, SVF response, block-size invariance, etc).

add_executable(EngineTests
    AdapterNodeTests.cpp
    AlignedBufferTests.cpp
    AnalysisThreadBudgetTests.cpp
    AnalysisThreadTests.cpp
    BlockSizeInvarianceTests.cpp
    CanConnectTests.cpp
    DataTests.cpp
    DomainSplitterTests.cpp
    EngineVersionTests.cpp
    ExecutionPlanTapTests.cpp
    GraphCompilerTests.cpp
    GrowablePortsTests.cpp
    InheritingPortsTests.cpp
    LogicNodeTests.cpp
    MathNodeTests.cpp
    MixNodeTests.cpp
    ModulatablePortsTests.cpp
    NanGuardTests.cpp
    NodeDescriptorTests.cpp
    NoteEventTests.cpp
    OversamplingStageTests.cpp
    PatchSerializerTests.cpp
    PlanSwapperTests.cpp
    PolyBlepOscillatorTests.cpp
    ProofGraphRenderTests.cpp
    RtAllocationTrapTests.cpp
    SampleRateCoverageTests.cpp
    SmoothedParameterTests.cpp
    SvfFilterTests.cpp
    TapTests.cpp
    TelemetryFrameBufferTests.cpp
    TelemetryFrameTests.cpp
    TelemetryHubTests.cpp
    UtilityNodeTests.cpp
    ValueContractTests.cpp
    VoiceManagerTests.cpp
)

target_link_libraries(EngineTests
    PRIVATE
        bazalt::engine
        Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(EngineTests)
