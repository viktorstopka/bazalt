#pragma once

#include "bazalt/engine/graph/PortDescriptor.h"
#include "bazalt/engine/graph/SignalType.h"
#include <juce_core/juce_core.h>
#include <utility>

namespace bazalt::engine::ValueTypes
{
    /** Named, reusable numeric semantics for PortDescriptor/ParameterDescriptor
        construction — a real answer to "does the engine know a value's type
        beyond its raw SignalType" (direct feedback: "there should definitely
        be some sort of types such as MIDI Note, Frequency, Velocity,
        Pitch... so should have it somehow implemented").

        This is a construction-time convenience, not a new wire-format field:
        every factory here just returns an ordinary PortDescriptor/
        ParameterDescriptor with unit/minValue/maxValue/isInteger/isLogScale
        already filled in consistently. Nothing about the JSON schema
        (NodeDescriptorJson.cpp) or the UI's descriptorTypes.ts changes —
        they already carry this metadata field-for-field; what was missing
        was a shared place nodes get it from, so two nodes with the same
        kind of value (e.g. osc.analog.frequency and filter.svf.cutoff, both
        Hz, both 20-20000, both skew 0.3) don't each hand-type their own
        literals and silently drift apart over time. Call a factory instead
        of writing `{ id, 20.0f, 20000.0f, ... }` yourself.

        Deliberately NOT retrofitted onto every existing node: only
        osc.analog.frequency/filter.svf.cutoff (Frequency), env.adsr's
        attack/decay/release (TimeSeconds), and delay.line.samples
        (TimeSamples) actually match one of these shapes today — everything
        else (filter.svf's resonance, env.adsr's sustain, filter.onepole's
        coefficient, util.constant/adapt.map's deliberately-unbounded
        passthrough values, osc.analog's waveform-shape index, mix.gain's gain)
        is genuinely bespoke to that node and would be guessing at real DSP
        semantics (e.g. whether "gain" means linear or dB) to force through
        a shared type it may not actually match. MidiNote/Velocity/Pitch/
        GainDb/Percent/TimeMs below have no real node using them yet — they
        exist so the next node that needs one doesn't have to invent it.
    */

    // ---- Frequency: Hz, audio range, log-feeling skew ---------------------
    // osc.analog.frequency and filter.svf.cutoff already matched this exactly
    // (min 20, max 20000, skew 0.3, unit "Hz") before this file existed.

    inline ParameterDescriptor frequencyParameter (juce::String id, juce::String displayName, float defaultValue)
    {
        return ParameterDescriptor { .id = std::move (id),
                                      .minValue = 20.0f,
                                      .maxValue = 20000.0f,
                                      .defaultValue = defaultValue,
                                      .skew = 0.3f,
                                      .unit = "Hz",
                                      .displayName = std::move (displayName),
                                      .quantity = Quantity::Frequency,
                                      .curve = Curve::Logarithmic };
    }

    inline PortDescriptor frequencyPort (juce::String id, juce::String label, float defaultValue = 440.0f, bool hasFallbackWhenUnconnected = true)
    {
        return PortDescriptor { .id = std::move (id),
                                 .type = SignalType::Control,
                                 .label = std::move (label),
                                 .unit = "Hz",
                                 .minValue = 20.0f,
                                 .maxValue = 20000.0f,
                                 .defaultValue = defaultValue,
                                 .isLogScale = true,
                                 .hasFallbackWhenUnconnected = hasFallbackWhenUnconnected,
                                 .quantity = Quantity::Frequency,
                                 .curve = Curve::Logarithmic };
    }

    // ---- Time: seconds (envelope-stage shape) / milliseconds / samples ----
    // env.adsr's attack/decay/release already matched timeSeconds exactly
    // (min 0, max 10, skew 0.5, unit "s"); delay.line.samples already
    // matched timeSamplesPort exactly (min 1, isInteger, unit "samples").

    inline ParameterDescriptor timeSecondsParameter (juce::String id, juce::String displayName, float defaultValue, float maxSeconds = 10.0f)
    {
        return ParameterDescriptor { .id = std::move (id),
                                      .minValue = 0.0f,
                                      .maxValue = maxSeconds,
                                      .defaultValue = defaultValue,
                                      .skew = 0.5f,
                                      .unit = "s",
                                      .displayName = std::move (displayName),
                                      .quantity = Quantity::Time,
                                      .curve = Curve::Logarithmic };
    }

    inline ParameterDescriptor timeMsParameter (juce::String id, juce::String displayName, float defaultValue, float maxMs = 5000.0f)
    {
        return ParameterDescriptor { .id = std::move (id),
                                      .minValue = 0.0f,
                                      .maxValue = maxMs,
                                      .defaultValue = defaultValue,
                                      .skew = 0.4f,
                                      .unit = "ms",
                                      .displayName = std::move (displayName),
                                      .quantity = Quantity::Time,
                                      .curve = Curve::Logarithmic };
    }

    inline PortDescriptor timeMsPort (juce::String id, juce::String label, float defaultValue, float maxMs = 5000.0f, bool hasFallbackWhenUnconnected = true)
    {
        return PortDescriptor { .id = std::move (id),
                                 .type = SignalType::Control,
                                 .label = std::move (label),
                                 .unit = "ms",
                                 .minValue = 0.0f,
                                 .maxValue = maxMs,
                                 .defaultValue = defaultValue,
                                 .hasFallbackWhenUnconnected = hasFallbackWhenUnconnected,
                                 .quantity = Quantity::Time,
                                 .curve = Curve::Logarithmic };
    }

    /** `maxSamples` is a node-specific ceiling (e.g. a delay line's own
        allocated buffer size) — never a fixed literal, so it's a required
        argument here rather than a defaulted one.
    */
    inline PortDescriptor timeSamplesPort (juce::String id, juce::String label, int maxSamples, float defaultValue, bool hasFallbackWhenUnconnected = true)
    {
        return PortDescriptor { .id = std::move (id),
                                 .type = SignalType::Control,
                                 .label = std::move (label),
                                 .unit = "samples",
                                 .minValue = 1.0f,
                                 .maxValue = (float) maxSamples,
                                 .defaultValue = defaultValue,
                                 .isInteger = true,
                                 .hasFallbackWhenUnconnected = hasFallbackWhenUnconnected,
                                 .kind = ValueKind::Int,
                                 .quantity = Quantity::Time,
                                 .curve = Curve::Logarithmic,
                                 .step = 1.0f };
    }

    // ---- MIDI note number: 0-127 integer, no unit --------------------------
    // No real node uses this yet — ui/src/graph/mockDescriptors.ts's
    // mock.midiNote is UI-only today; this is what a real note-source node's
    // pitch output (or a pitch-quantizer's input) should use once one exists.

    inline PortDescriptor midiNotePort (juce::String id, juce::String label, float defaultValue = 60.0f, bool hasFallbackWhenUnconnected = true)
    {
        return PortDescriptor { .id = std::move (id),
                                 .type = SignalType::Control,
                                 .label = std::move (label),
                                 .minValue = 0.0f,
                                 .maxValue = 127.0f,
                                 .defaultValue = defaultValue,
                                 .isInteger = true,
                                 .hasFallbackWhenUnconnected = hasFallbackWhenUnconnected,
                                 .kind = ValueKind::Int,
                                 .quantity = Quantity::Count,
                                 .step = 1.0f };
    }

    // ---- Velocity: 0-127 integer -------------------------------------------
    // Same numeric shape as a MIDI note number today, kept as its own named
    // factory rather than an alias since the two mean different things and
    // could reasonably diverge later (e.g. a normalized 0-1 velocity
    // convention) without every velocity call site needing to change too.

    inline PortDescriptor velocityPort (juce::String id, juce::String label, float defaultValue = 100.0f, bool hasFallbackWhenUnconnected = true)
    {
        return PortDescriptor { .id = std::move (id),
                                 .type = SignalType::Control,
                                 .label = std::move (label),
                                 .minValue = 0.0f,
                                 .maxValue = 127.0f,
                                 .defaultValue = defaultValue,
                                 .isInteger = true,
                                 .hasFallbackWhenUnconnected = hasFallbackWhenUnconnected,
                                 .kind = ValueKind::Int,
                                 .quantity = Quantity::Count,
                                 .step = 1.0f };
    }

    // ---- Pitch offset: semitones, bipolar ----------------------------------
    // `rangeSemitones` is the +/- half-width (default +/-60 = 5 octaves
    // either way) — a transpose/pitch-bend amount, not an absolute note.

    inline PortDescriptor pitchPort (juce::String id, juce::String label, float rangeSemitones = 60.0f, float defaultValue = 0.0f, bool hasFallbackWhenUnconnected = true)
    {
        return PortDescriptor { .id = std::move (id),
                                 .type = SignalType::Control,
                                 .label = std::move (label),
                                 .unit = "st",
                                 .minValue = -rangeSemitones,
                                 .maxValue = rangeSemitones,
                                 .defaultValue = defaultValue,
                                 .hasFallbackWhenUnconnected = hasFallbackWhenUnconnected,
                                 .quantity = Quantity::Pitch,
                                 .polarity = Polarity::Bipolar };
    }

    // ---- Gain: decibels -----------------------------------------------------
    // Deliberately not applied to mix.gain's existing "gain" port — that
    // node's own gain could mean a linear multiplier rather than dB, and
    // guessing wrong would be a real behavioural/UI-bound change to an
    // existing node, not a safe refactor.

    // NOTE (M14): VALUE_MODEL.md §3 says Gain's canonical unit is *linear
    // amplitude*, dB is display-only. These two factories store and
    // transmit dB directly instead — a real, known deviation, left as-is
    // because nothing uses them yet (see this file's own top comment) and
    // fixing it means deciding the storage unit before a real gain-in-dB
    // node exists to get it right for, not guessing here. `quantity =
    // Gain` is still set, since the UI/JSON should know "this is a gain
    // control" even while the canonical-unit question is unresolved —
    // just don't treat this pair as a compliant reference implementation
    // yet.
    inline ParameterDescriptor gainDbParameter (juce::String id, juce::String displayName, float defaultValue = 0.0f, float minDb = -60.0f, float maxDb = 12.0f)
    {
        return ParameterDescriptor { .id = std::move (id),
                                      .minValue = minDb,
                                      .maxValue = maxDb,
                                      .defaultValue = defaultValue,
                                      .unit = "dB",
                                      .displayName = std::move (displayName),
                                      .quantity = Quantity::Gain };
    }

    inline PortDescriptor gainDbPort (juce::String id, juce::String label, float defaultValue = 0.0f, float minDb = -60.0f, float maxDb = 12.0f, bool hasFallbackWhenUnconnected = true)
    {
        return PortDescriptor { .id = std::move (id),
                                 .type = SignalType::Control,
                                 .label = std::move (label),
                                 .unit = "dB",
                                 .minValue = minDb,
                                 .maxValue = maxDb,
                                 .defaultValue = defaultValue,
                                 .hasFallbackWhenUnconnected = hasFallbackWhenUnconnected,
                                 .quantity = Quantity::Gain };
    }

    // ---- Percent: 0-100 ------------------------------------------------------

    inline ParameterDescriptor percentParameter (juce::String id, juce::String displayName, float defaultValue)
    {
        return ParameterDescriptor { .id = std::move (id),
                                      .minValue = 0.0f,
                                      .maxValue = 100.0f,
                                      .defaultValue = defaultValue,
                                      .unit = "%",
                                      .displayName = std::move (displayName),
                                      .quantity = Quantity::Ratio };
    }

    inline PortDescriptor percentPort (juce::String id, juce::String label, float defaultValue = 100.0f, bool hasFallbackWhenUnconnected = true)
    {
        return PortDescriptor { .id = std::move (id),
                                 .type = SignalType::Control,
                                 .label = std::move (label),
                                 .unit = "%",
                                 .minValue = 0.0f,
                                 .maxValue = 100.0f,
                                 .defaultValue = defaultValue,
                                 .hasFallbackWhenUnconnected = hasFallbackWhenUnconnected,
                                 .quantity = Quantity::Ratio };
    }
}
