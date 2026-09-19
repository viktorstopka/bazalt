#pragma once

namespace bazalt::engine
{
    /** M18 (ADR-0024) — the compound payload a connected `SignalType::Note`
        port actually carries per sample. Like `Data` (`Data.h`), this is
        NOT a float and doesn't fit `ExecutionPlan`'s ordinary
        `blockBuffers`/`regionScalars` model — `GraphCompiler.cpp` gives
        every connected Note port its own `ExecutionPlan::noteBuffers`
        entry instead (one `NoteEvent` per sample), read/written via
        `Node::consumeNoteBlock()`/`produceNoteBlock()` rather than the
        ordinary `processSample()` float array.

        Deliberately narrower than `SIGNAL_TYPES.md` §2's full "id, pitch,
        velocity, pressure, slide, release velocity" — the same narrowing
        `InstanceAllocatorNode.h` already applied to its own ports in M17:
        add a field once something can actually drive it, not before.
    */
    struct NoteEvent
    {
        bool gate = false;
        float pitch = 60.0f;     // absolute, semitones, 60 = middle C — continuous, so bend needs no special path (VALUE_MODEL.md §3)
        float velocity = 0.0f;   // 0..1
        bool startEvent = false; // true for exactly the one sample a note-on lands on
        bool stopEvent = false;  // true for exactly the one sample a note-off lands on
    };
}
