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

        Boolean (NODE_EDITOR.md §5, added M7): a genuinely different buffer-
        level contract from Control — no smoothing, no skew, one bit of
        state — not just a UI-side colour distinction. The UI's Modulation/
        Value/Integer palette entries are presentation classifications
        layered on top of Control + PortDescriptor's numeric metadata
        (NODE_EDITOR.md §5), not separate SignalType values.

        Data (SIGNAL_TYPES.md §2, added M15, see Data.h): an immutable,
        reference-counted buffer — tables, modal sets, scales, wavetables,
        curves, impulse responses. Not a buffer-shape tag like every other
        member here (GraphCompiler doesn't allocate a `blockBuffers`/
        `regionScalars` slot for it the way it does for Audio/Control) —
        a Data-typed port instead holds a `const DataBuffer*` published by
        `DataPublisher` (Data.h), swapped, never copied per sample. ADR-0016
        already flags that `SignalType` mixes two concerns (compiler
        buffer-shape vs. UI/connection semantics); Data is unambiguously
        real regardless of how that's eventually resolved.
    */
    enum class SignalType
    {
        Audio,
        Control,
        Event,
        Note,
        Spectral,
        Boolean,
        Data
    };
}
