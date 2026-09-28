#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <juce_core/juce_core.h>

namespace bazalt::engine::nodes
{
    /** Stable type id: "io.noteIn" (M18, ADR-0024). NODE_CATALOG.md's
        plugin I/O boundary for MIDI: one `Note`-typed output ("notes"),
        no inputs. Translates MIDI into `NoteEvent`s (`produceNoteBlock()`)
        for whatever's wired downstream — `instance.allocate.voice`'s "spawn"
        input is the only consumer today (M18's own scope).

        Driven by direct C++ pokes from `PluginProcessor::handleMidiEvent`
        (`injectNoteOn`/`injectNoteOff`/`injectPitchBend`), the same
        "poke a concrete node type via getNodeById + dynamic_cast" pattern
        `AdsrNode`/`InstanceAllocatorNode` already use — this node is now
        the ONE thing poked for note delivery; everything downstream of it
        (`instance.allocate.voice`, `env.adsr`'s gate, `osc.analog`'s pitch)
        receives real, ordinary port data instead of further direct pokes.

        `channel` (Omni/1-16) and `mpeMode` (off/MPE) are schema-only for
        M18 — recorded so the descriptor round-trips honestly, not yet
        behavior-changing (every channel is accepted, exactly like Omni),
        matching `InstanceAllocatorNode.h`'s own M17 precedent for
        `configuration`. Pitch bend is folded into the continuous `pitch`
        field directly (ADR-0024) rather than becoming its own port —
        `SIGNAL_TYPES.md` §9's "channel-level data is Control in the mono
        domain, not part of Note" is why `io.control` stays separate and
        will eventually carry mod wheel/CC/sustain instead.
    */
    class IoNoteInNode : public Node
    {
    public:
        static constexpr int numOutputs = 1; // notes, Note

        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Note In"; }
        juce::String getCategory() const override { return "I/O"; }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "notes", .type = SignalType::Note, .isPrimaryOutput = true } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "io.noteIn.channel",
                                            .minValue = 0.0f,
                                            .maxValue = 16.0f,
                                            .defaultValue = 0.0f,
                                            .displayName = "Channel",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "omni", "Omni" }, { "1", "1" }, { "2", "2" }, { "3", "3" },
                                                              { "4", "4" }, { "5", "5" }, { "6", "6" }, { "7", "7" },
                                                              { "8", "8" }, { "9", "9" }, { "10", "10" }, { "11", "11" },
                                                              { "12", "12" }, { "13", "13" }, { "14", "14" }, { "15", "15" },
                                                              { "16", "16" } },
                                            .isStructural = true },
                     ParameterDescriptor { .id = "io.noteIn.mpeMode",
                                            .minValue = 0.0f,
                                            .maxValue = 1.0f,
                                            .defaultValue = 0.0f,
                                            .displayName = "MPE Mode",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "off", "Off" }, { "mpe", "MPE" } },
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            // Schema-only for M18 — see class comment.
            if (parameterId == "io.noteIn.channel")
                channel = (int) (value + 0.5f);
            else if (parameterId == "io.noteIn.mpeMode")
                mpeMode = (int) (value + 0.5f);
        }

        void reset() override
        {
            gate = false;
            pitch = 60.0f;
            velocity = 0.0f;
            pitchBendSemitones = 0.0f;
            pendingStart = false;
            pendingStop = false;
        }

        /** Direct C++ poke — see class comment. `pitchIn` is the absolute
            MIDI note number (0-127), matching `instance.allocate.voice.pitch`'s
            own contract exactly.
        */
        void injectNoteOn (float pitchIn, float velocityIn) noexcept
        {
            gate = true;
            pitch = pitchIn;
            velocity = velocityIn;
            pendingStart = true;
        }

        void injectNoteOff() noexcept
        {
            gate = false;
            pendingStop = true;
        }

        /** Continuous per-channel bend, in semitones — added to `pitch`
            every sample (ADR-0024), so a downstream continuous Pitch port
            needs no special-cased path for bend at all.
        */
        void injectPitchBend (float semitones) noexcept
        {
            pitchBendSemitones = semitones;
        }

        // This node's only output is Note-typed and routed entirely through
        // produceNoteBlock() below (called separately by
        // ExecutionPlan::process()) — the base class's default
        // processBlock() would otherwise loop calling processSample(),
        // which this node never overrides (there's no ordinary float
        // output to compute), hitting the base's jassertfalse stub.
        void processBlock (const float* const*, float* const*, int) noexcept override {}

        void produceNoteBlock (NoteEvent* output, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
            {
                output[i].gate = gate;
                output[i].pitch = pitch + pitchBendSemitones;
                output[i].velocity = velocity;
                output[i].startEvent = pendingStart && i == 0;
                output[i].stopEvent = pendingStop && i == 0;
            }

            pendingStart = false;
            pendingStop = false;
        }

    private:
        int channel = 0;
        int mpeMode = 0;

        bool gate = false;
        float pitch = 60.0f;
        float velocity = 0.0f;
        float pitchBendSemitones = 0.0f;
        bool pendingStart = false;
        bool pendingStop = false;
    };
}
