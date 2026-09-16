# tools/render-cli — patch + MIDI → WAV, headless. For M1 this just renders
# a fixed tone: the point is proving the engine-only build/link path works
# end to end with no plugin/UI dependency (docs/MILESTONES.md, M1). Real
# patch/MIDI input lands once NodeGraph exists (M2/M3).
#
# Links engine plus whatever extra JUCE modules the CLI itself needs for
# file I/O (juce_audio_formats) — this does NOT loosen engine's own
# dependency rule, since render-cli is a separate target, not engine.

juce_add_console_app(RenderCli
    PRODUCT_NAME "bazalt-render-cli"
)

target_sources(RenderCli
    PRIVATE
        source/Main.cpp
)

target_link_libraries(RenderCli
    PRIVATE
        bazalt::engine
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
