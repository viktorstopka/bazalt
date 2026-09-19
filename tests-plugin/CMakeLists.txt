# Plugin-level Catch2 suite — unlike tests/ (engine-only, headless per
# ARCHITECTURE.md §1), this exercises PluginProcessor's host-facing
# behaviour (bus layout, patch state round-trip, MIDI-to-audio rendering,
# macro automation stability) by direct construction/calls, no DAW host.
#
# NOTE: this does NOT link against the `BazaltPlugin` target. JUCE's
# juce_add_plugin recompiles its "shared code" per plugin FORMAT (VST3,
# Standalone, ...) with format-specific compile definitions — it's not a
# reusable static lib for arbitrary external linking, and linking against
# it here pulled in JUCE module sources (e.g. juce_gui_extra.cpp) without
# their required include paths (WebView2, engine's FILE_SET headers).
# Instead this target compiles plugin/source/*.cpp itself, as its own
# juce_add_console_app (so NEEDS_WEBVIEW2 gets the same automatic
# find_package(WebView2)/linkage juce_add_plugin gives BazaltPlugin) — the
# same pattern JUCE's own format-wrapper targets use, just with Catch2
# driving main() instead of a plugin wrapper.

juce_add_console_app(PluginTests
    PRODUCT_NAME "bazalt-plugin-tests"
    NEEDS_WEBVIEW2 TRUE
)

target_sources(PluginTests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/plugin/source/PluginProcessor.cpp
        ${CMAKE_SOURCE_DIR}/plugin/source/PluginEditor.cpp
        ${CMAKE_SOURCE_DIR}/plugin/source/MacroParameters.cpp
        ${CMAKE_SOURCE_DIR}/plugin/source/GraphEditController.cpp
        ${CMAKE_SOURCE_DIR}/plugin/source/NodeDescriptorJson.cpp
        BusLayoutTests.cpp
        ConnectWithAutoAdaptTests.cpp
        GraphEditControllerTests.cpp
        MacroParametersTests.cpp
        NodeDescriptorJsonTests.cpp
        PatchStateTests.cpp
        StressGraphTests.cpp
        VisualizationTapTests.cpp
        VoiceRenderTests.cpp
)

target_include_directories(PluginTests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/plugin/source
)

target_compile_definitions(PluginTests
    PRIVATE
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
        JUCE_USE_CURL=0
        # juce_add_plugin normally generates the JucePlugin_* macros for a
        # real plugin-format target; this is a test executable, not one, so
        # only the one actually referenced (PluginProcessor::getName())
        # needs defining by hand.
        JucePlugin_Name="Bazalt"
        BAZALT_UI_DEV_SERVER_URL="http://localhost:5173"
)

target_link_libraries(PluginTests
    PRIVATE
        bazalt::engine
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(PluginTests)
