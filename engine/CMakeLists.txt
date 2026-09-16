# engine — pure C++ DSP + graph runtime library.
#
# Dependency rule (enforced here, not just by convention — see
# docs/ARCHITECTURE.md §1): engine may depend on juce_core, juce_audio_basics,
# and juce_dsp (data structures only), and must NEVER depend on
# juce_audio_processors, juce_gui_basics, or anything plugin/UI-shaped. That
# is what keeps `tools/render-cli` (added in M1) and the Catch2 suite in
# tests/ headless and buildable without JUCE's plugin-client machinery.

add_library(engine STATIC)
add_library(bazalt::engine ALIAS engine)

target_sources(engine
    PRIVATE
        src/EngineVersion.cpp
        src/NanGuard.cpp
        src/PolyBlepOscillator.cpp
        src/RtAllocationTrap.cpp
        src/graph/ExecutionPlan.cpp
        src/graph/GraphCompiler.cpp
        src/patch/PatchSerializer.cpp
        src/telemetry/AnalysisThread.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/bazalt/engine/AlignedBuffer.h
            include/bazalt/engine/EngineVersion.h
            include/bazalt/engine/NanGuard.h
            include/bazalt/engine/OversamplingStage.h
            include/bazalt/engine/PolyBlepOscillator.h
            include/bazalt/engine/RtAllocationTrap.h
            include/bazalt/engine/SmoothedParameter.h
            include/bazalt/engine/SvfFilter.h
            include/bazalt/engine/graph/SignalType.h
            include/bazalt/engine/graph/PortDescriptor.h
            include/bazalt/engine/graph/Node.h
            include/bazalt/engine/graph/NodeGraph.h
            include/bazalt/engine/graph/NodeFactory.h
            include/bazalt/engine/graph/ExecutionPlan.h
            include/bazalt/engine/graph/GraphCompiler.h
            include/bazalt/engine/graph/PlanSwapper.h
            include/bazalt/engine/graph/VoiceManager.h
            include/bazalt/engine/graph/ProofGraphs.h
            include/bazalt/engine/nodes/OscillatorNode.h
            include/bazalt/engine/nodes/SvfFilterNode.h
            include/bazalt/engine/nodes/AdsrNode.h
            include/bazalt/engine/nodes/GainNode.h
            include/bazalt/engine/nodes/NoiseBurstNode.h
            include/bazalt/engine/nodes/MixNode.h
            include/bazalt/engine/nodes/DelayNode.h
            include/bazalt/engine/nodes/OnePoleFilterNode.h
            include/bazalt/engine/patch/PatchDocument.h
            include/bazalt/engine/patch/PatchSerializer.h
            include/bazalt/engine/telemetry/Tap.h
            include/bazalt/engine/telemetry/TelemetryFrame.h
            include/bazalt/engine/telemetry/TelemetryFrameBuffer.h
            include/bazalt/engine/telemetry/TelemetryHub.h
            include/bazalt/engine/telemetry/MeterBallistics.h
            include/bazalt/engine/telemetry/AnalysisThread.h
)

target_compile_features(engine PUBLIC cxx_std_20)

target_link_libraries(engine
    PUBLIC
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_dsp
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(engine
    PUBLIC
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        # Debug-build RT-allocation trap (ARCHITECTURE.md §5) — overrides
        # global operator new/delete to throw if armed via
        # ScopedAudioThreadAllocationTrap. See RtAllocationTrap.cpp for why
        # this only activates in binaries that actually use it. PUBLIC so
        # consumers (tests, in particular) can #if on it too, rather than
        # duplicating the Debug-only condition themselves.
        $<$<CONFIG:Debug>:BAZALT_ENABLE_RT_ALLOCATION_TRAP=1>
)
