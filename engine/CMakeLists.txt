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
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/bazalt/engine/EngineVersion.h
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
)
