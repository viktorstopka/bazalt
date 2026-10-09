#pragma once

namespace bazalt::engine
{
    /** How a port's value is carried — never what it means (that is its
        Quantity, PortDescriptor.h). wiki/plans/DataAndWavetable.md D1.

        Signal: one float per sample — a sound, a frequency, a modulation, a
        true/false gate are all Signals, told apart by their Quantity
        (Quantity::Audio for a waveform meant to be heard, Quantity::Boolean
        for a 0/1 gate). Until stage 1 these were three types (Audio,
        Control, Boolean); the buffers were always identical and the type
        only ever said "these don't belong together", which the connection
        rules no longer believe. Every Signal can carry channels (stereo).

        Event: a moment (non-zero on the sample it fires — its strength).
        Note: a note payload on its own buffer (ExecutionPlan::noteBuffers).
        Data: an immutable, reference-counted buffer — tables, curves,
        scales, wavetables (Data.h), swapped, never copied per sample.
        Spectral: reserved, unimplemented.
    */
    enum class SignalType
    {
        Signal,
        Event,
        Note,
        Spectral,
        Data
    };
}
