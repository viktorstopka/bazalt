# plugin — JUCE AudioProcessor wrapper: buses, state, WebView bridge. Links
# engine plus JUCE's plugin-client modules and owns everything host-facing.
# engine itself must never link back against this target or against
# juce_audio_processors/juce_gui_basics — see engine/CMakeLists.txt.

juce_add_plugin(BazaltPlugin
    VERSION                    0.1.0
    COMPANY_NAME                "Bazalt"
    PRODUCT_NAME                "Bazalt"
    PLUGIN_MANUFACTURER_CODE    Bzlt
    PLUGIN_CODE                 Bzl1
    FORMATS                     VST3 Standalone
    VST3_CATEGORIES             Instrument Synth
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEBVIEW2              TRUE
    # Off by default: installing into "C:/Program Files/Common Files/VST3"
    # needs admin rights this dev shell doesn't have. Point pluginval/DAWs at
    # the build output directly (BazaltPlugin_artefacts/<config>/VST3/), or
    # flip this on and build from an elevated shell when you need a real
    # install. See CLAUDE.md.
    COPY_PLUGIN_AFTER_BUILD     FALSE
)

target_sources(BazaltPlugin
    PRIVATE
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
)

target_compile_definitions(BazaltPlugin
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        # Debug builds point the WebView at the Vite dev server for hot
        # reload; release builds serve ui/dist from disk via a resource
        # provider. See ARCHITECTURE.md §7 — full binary-embedded release
        # assets are a follow-up (tracked for M5), this is the interim path.
        BAZALT_UI_DEV_SERVER_URL="http://localhost:5173"
)

target_link_libraries(BazaltPlugin
    PRIVATE
        bazalt::engine
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
