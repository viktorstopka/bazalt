#pragma once

namespace bazalt::engine
{
    /** ARCHITECTURE.md §3.3. Audio and Control share the same buffer shape
        (float32, block-length) — Control may run at full sample rate too
        (e.g. FM/PM) — but carry a distinct tag so the compiler and UI can
        reason about range/units. Event and Note aren't wired through node
        ports yet in M2 (voices are driven directly by the voice manager
        poking node parameters); the tags exist now so the M3 MIDI/note
        data model doesn't require a port-type migration. Spectral is
        reserved, unimplemented.
    */
    enum class SignalType
    {
        Audio,
        Control,
        Event,
        Note,
        Spectral
    };
}
