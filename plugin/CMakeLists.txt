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
        source/MacroParameters.cpp
        source/GraphEditController.cpp
        source/NodeDescriptorJson.cpp
        source/StandaloneApp.cpp
)

target_compile_definitions(BazaltPlugin
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        # Debug builds point the WebView at the Vite dev server for hot
        # reload; release builds serve ui/dist from disk via a resource
        # provider (findUiDistRoot()/serveResource() in PluginEditor.cpp,
        # fed by this file's own CopyUiDist.cmake post-build step) — real as
        # of 2026-10-03, not aspirational. This comment used to describe
        # that as a stale, never-actually-built claim ("tracked for M5,
        # this is the interim path") while the real code always served a
        # hardcoded placeholder regardless of ui/dist's existence — see
        # wiki/NODES_Gaps.md's own write-up for the full story. Full
        # binary-embedded (not copied-alongside) release assets, the
        # originally-envisioned ARCHITECTURE.md §7 end state, is still real,
        # separate future work if a single-file .vst3 with no sibling ui/
        # folder is ever needed.
        BAZALT_UI_DEV_SERVER_URL="http://localhost:5173"
        # Standalone only (JucePlugin_Build_Standalone-guarded): use our own
        # source/StandaloneApp.cpp instead of JUCE's stock Standalone app, so
        # we can default "mute input to avoid feedback loop" OFF instead of
        # JUCE's hardcoded-on default — see that file's own comment.
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
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

# Ship ui/dist (cd ui && npm run build) alongside a Release build so it's
# genuinely self-contained on a machine with no Node/dev-server at all —
# PluginEditor.cpp's own Release-build resource provider (findUiDistRoot())
# reads it from exactly these two locations. Harmless, cheap no-op for
# Debug (which never reads this — Debug loads the live Vite dev server
# instead) and for anyone who hasn't run `npm run build` yet
# (cmake/CopyUiDist.cmake skips with a status message, never fails the
# build) — see that script's own comment for the full reasoning.
if(TARGET BazaltPlugin_VST3)
    # VST3 bundle layout: Bazalt.vst3/Contents/x86_64-win/Bazalt.vst3 (the
    # binary itself, what TARGET_FILE_DIR points at) alongside
    # Bazalt.vst3/Contents/Resources/ — ship ui/dist under a "ui" subfolder
    # there, next to moduleinfo.json's own existing neighbour.
    add_custom_command(TARGET BazaltPlugin_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DSRC=${CMAKE_SOURCE_DIR}/ui/dist"
            "-DDST=$<TARGET_FILE_DIR:BazaltPlugin_VST3>/../Resources/ui"
            -P "${CMAKE_SOURCE_DIR}/cmake/CopyUiDist.cmake"
        VERBATIM
        COMMENT "Bazalt: copying ui/dist into the VST3 bundle (if built)")
endif()

if(TARGET BazaltPlugin_Standalone)
    # Standalone layout: Bazalt.exe sits flat in its own output directory,
    # no bundle nesting — ship ui/dist as a flat sibling "ui" folder.
    add_custom_command(TARGET BazaltPlugin_Standalone POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DSRC=${CMAKE_SOURCE_DIR}/ui/dist"
            "-DDST=$<TARGET_FILE_DIR:BazaltPlugin_Standalone>/ui"
            -P "${CMAKE_SOURCE_DIR}/cmake/CopyUiDist.cmake"
        VERBATIM
        COMMENT "Bazalt: copying ui/dist next to the Standalone executable (if built)")
endif()
