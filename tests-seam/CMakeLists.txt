# The seam test host (wiki/plans/Release.md R0.5): the real plugin layer
# serving the built UI to headless Chromium. See SeamHost.cpp and run.mjs.
juce_add_console_app(SeamHost
    PRODUCT_NAME "bazalt-seam-host"
    NEEDS_WEBVIEW2 TRUE
)

target_sources(SeamHost
    PRIVATE
        ${CMAKE_SOURCE_DIR}/plugin/source/PluginProcessor.cpp
        ${CMAKE_SOURCE_DIR}/plugin/source/PluginEditor.cpp
        ${CMAKE_SOURCE_DIR}/plugin/source/MacroParameters.cpp
        ${CMAKE_SOURCE_DIR}/plugin/source/GraphEditController.cpp
        ${CMAKE_SOURCE_DIR}/plugin/source/NodeDescriptorJson.cpp
        SeamHost.cpp
)

target_include_directories(SeamHost
    PRIVATE
        ${CMAKE_SOURCE_DIR}/plugin/source
)

target_compile_definitions(SeamHost
    PRIVATE
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
        JUCE_USE_CURL=0
        JucePlugin_Name="Bazalt"
        BAZALT_UI_DEV_SERVER_URL="http://localhost:5173"
        JUCE_USE_MP3AUDIOFORMAT=1
)

target_link_libraries(SeamHost
    PRIVATE
        bazalt::engine
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
