#pragma once

#include <array>

namespace bazalt::engine
{
    /** M21 — what the host hands the engine each block, in plain data, so
        the nodes at the plugin boundary (io.audioIn, io.control,
        io.transport) can read it without `engine/` knowing anything about
        JUCE's processor, buses or playhead (CLAUDE.md rule 4).

        The plugin fills one of these and calls `ExecutionPlan::
        applyHostInputs()` immediately before each `process()` call; every
        node that declared `wantsHostInputs()` then receives it through
        `Node::setHostInputs()`. This avoids the pattern `io.noteIn` still
        uses - poking a concrete node found by a hardcoded id
        (`getNodeById ("noteIn")`), which silently ignores a note-in the user
        placed under any other id. Note events aren't folded in here: they're
        sample-accurate, which a once-per-range snapshot can't carry
        (ADR-0028).

        Everything here describes the range of samples about to be
        processed, so it is valid only for that one `process()` call: the
        audio pointers are already offset to the range's first sample, and
        `ppqPosition`/`timeSeconds` are the values at that first sample. A
        node that needs anything across calls copies it.
    */
    struct HostInputs
    {
        /** Main stereo bus + 4 stereo sidechain aux buses (ARCHITECTURE.md §4.1). */
        static constexpr int numAudioBuses = 5;
        static constexpr int channelsPerBus = 2;
        static constexpr int numControllers = 128;

        /** `audio[bus][channel]`: nullptr when that bus isn't active in the
            host (a host that never enabled the aux buses) — a reader must
            treat that as silence, never as an error.
        */
        std::array<std::array<const float*, channelsPerBus>, numAudioBuses> audio {};

        /** Current MIDI controller state, normalised to 0..1 (CC value / 127),
            indexed by CC number. Omni: the most recent value on any channel.
        */
        std::array<float, numControllers> controllers {};
        float channelPressure = 0.0f; // 0..1
        float pitchBend = 0.0f;       // -1..1, 0 = centre

        bool transportPlaying = false;
        double tempoBpm = 120.0;
        double ppqPosition = 0.0; // quarter notes since the start of the timeline
        double timeSeconds = 0.0;
        double sampleRate = 44100.0;
    };
}
