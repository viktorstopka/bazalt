#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "note.assemble" (wiki/NODES.md's `note.*` row — the
        Note Stream batch's own deferred entry, picked up here). Every other
        `note.*` node (gate/value/quantize/transpose/filter/humanize) only
        RESHAPES a `Note` stream that already exists; `io.noteIn` (real host
        MIDI) has been the only thing that can PRODUCE one at all. This is
        the other half: turns a plain trigger + a tracked pitch into a real
        `Note` stream from scratch — the piece that was missing to make a
        `clock.*`/`random.*`/`data.lookup` chain able to play a synth voice,
        or a pitch-tracked audio input (`analysis.pitch`/`analysis.onset`,
        both still 📋) playable as an instrument. Direct feedback (a live
        design session on the Note Stream system's own gaps) named this
        exact node as the single highest-leverage piece missing — nothing
        else in the catalog can create a `Note` from algorithmic primitives.

        Zero `Note` inputs, one `Note` output — comfortably inside today's
        one-`Note`-port-per-node engine limit (`NoteFilterNode.h`'s own
        shared comment), no redesign needed to build this one.

        **Behavior, this node's own concrete design** (the catalog names the
        ports, not their exact contract):
        - `trigger` always starts a note, even while one is already held —
          a legato retrigger with no forced note-off first, the same
          convention `IoNoteInNode::injectNoteOn` already establishes (a
          real MIDI keyboard's overlapping notes work the same way).
          Suppressed when `confidence < confidenceGate`, so a low-confidence
          pitch-tracker reading (noise, silence) can't spawn a bogus note.
        - `pitch` is tracked CONTINUOUSLY while a note is held, not just
          captured at the trigger instant — matching `[audio]` in the
          catalog notation, and needed for both an audio-tracked
          instrument's natural vibrato/bend and an algorithmically
          modulated pitch feeding this node.
        - `velocity` is captured once, at the trigger instant, and held for
          the rest of the note — ordinary MIDI velocity semantics, not a
          continuously-tracked signal.
        - A note ends on an explicit `release` event, OR — since a
          monophonic pitch tracker has no natural discrete "note off" of its
          own — automatically once `confidence` drops back below
          `confidenceGate`. Neither wired (the plain generative case: a
          clock/random chain with no pitch-tracking involved at all) means
          `confidence`'s own unconnected fallback (1.0, "fully confident")
          never drops, so the note is held until an explicit `release`
          arrives — ordinary MIDI note-on/note-off semantics, no invented
          auto-timeout. `confidence`/`confidenceGate` both default such
          that an entirely generative patch with neither wired behaves
          exactly as if the gate didn't exist at all.
    */
    class NoteAssembleNode : public Node
    {
    public:
        static constexpr float defaultPitch = 60.0f;
        static constexpr float defaultVelocity = 1.0f;
        static constexpr float defaultConfidence = 1.0f;
        static constexpr float defaultConfidenceGate = 0.5f;
        static constexpr int numInputs = 6;  // trigger, release, pitch, velocity, confidence, confidenceGate
        static constexpr int numOutputs = 1; // notes, Note

        void prepare (const NodePrepareInfo& info) override
        {
            scratch.assign ((size_t) info.maxBlockSize, NoteEvent {});
        }

        void reset() override
        {
            held = false;
            heldPitch = defaultPitch;
            heldVelocity = defaultVelocity;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Assemble Note"; }
        juce::String getCategory() const override { return "Note"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "trigger", .type = SignalType::Event, .label = "Trigger" },
                PortDescriptor { .id = "release", .type = SignalType::Event, .label = "Release" },
                PortDescriptor { .id = "pitch", .type = SignalType::Control, .label = "Pitch",
                                  .unit = "st", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = defaultPitch,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Pitch },
                PortDescriptor { .id = "velocity", .type = SignalType::Control, .label = "Velocity",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultVelocity,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "confidence", .type = SignalType::Control, .label = "Confidence",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultConfidence,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "confidenceGate", .type = SignalType::Control, .label = "Confidence Gate",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultConfidenceGate,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "notes", .type = SignalType::Note, .isPrimaryOutput = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "pitch")
                storedPitch = value;
            else if (parameterId == "velocity")
                storedVelocity = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "confidence")
                storedConfidence = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "confidenceGate")
                storedConfidenceGate = juce::jlimit (0.0f, 1.0f, value);
        }

        // Mixes a Note output with several ordinary Event/Control inputs -
        // the same reason NoteGateNode/IoNoteInNode both hand-write
        // processBlock() rather than delegating to the base per-sample
        // loop: processSample()'s signature has no room for Note data, and
        // this node needs BOTH the ordinary inputs array AND its own
        // held-note state together, every sample.
        void processBlock (const float* const* inputs, float* const*, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const auto triggerFired = std::fabs (inputs[0][i]) > 0.0f;
                const auto releaseFired = std::fabs (inputs[1][i]) > 0.0f;
                const auto pitchIn = inputs[2][i];
                const auto velocityIn = inputs[3][i];
                const auto confidenceIn = inputs[4][i];
                const auto gateIn = inputs[5][i];

                const auto pitch = std::isnan (pitchIn) ? storedPitch : pitchIn;
                const auto velocity = std::isnan (velocityIn) ? storedVelocity : velocityIn;
                const auto confidence = std::isnan (confidenceIn) ? storedConfidence : confidenceIn;
                const auto confidenceGate = std::isnan (gateIn) ? storedConfidenceGate : gateIn;
                const auto confident = confidence >= confidenceGate;

                auto& ev = scratch[(size_t) i];
                ev.startEvent = false;
                ev.stopEvent = false;

                if (triggerFired && confident)
                {
                    held = true;
                    heldPitch = pitch;
                    heldVelocity = velocity;
                    ev.startEvent = true;
                }
                else if (held && (releaseFired || ! confident))
                {
                    held = false;
                    ev.stopEvent = true;
                }

                if (held)
                    heldPitch = pitch;

                ev.gate = held;
                ev.pitch = heldPitch;
                ev.velocity = heldVelocity;
            }
        }

        void produceNoteBlock (NoteEvent* output, int numSamples) noexcept override
        {
            for (int i = 0; i < numSamples; ++i)
                output[i] = scratch[(size_t) i];
        }

    private:
        bool held = false;
        float heldPitch = defaultPitch;
        float heldVelocity = defaultVelocity;

        float storedPitch = defaultPitch;
        float storedVelocity = defaultVelocity;
        float storedConfidence = defaultConfidence;
        float storedConfidenceGate = defaultConfidenceGate;

        std::vector<NoteEvent> scratch;
    };
}
