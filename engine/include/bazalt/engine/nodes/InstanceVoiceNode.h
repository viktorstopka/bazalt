#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <juce_core/juce_core.h>

namespace bazalt::engine::nodes
{
    /** Stable type id: "instance.allocate.voice" (M17, renamed from "instance.allocator"
        in 09-28-InstanceAllocator.3, renamed again from "instance.voice" in
        09-29-AddMenu.1 — CLAUDE.md rule 3 is suspended, so both are direct
        renames, no migration path). The second rename inserts an "allocate"
        namespace segment purely so the Add menu's category tree (also
        09-29-AddMenu.1) can nest Voice under Domain > Allocate — `instance.mix`
        deliberately stays flat (no such segment) since it isn't one of several
        spawn-mechanism siblings the way Voice/Swarm/Trigger are. `DOMAINS.md`
        §3's Instance Allocator concept — **Voice configuration only**, and (as
        of the first rename) the ONLY configuration this class implements at
        all: the
        `configuration` enum parameter (Voice/Swarm-population/Swarm-transient/
        Trigger) that used to live here has been removed outright, not just
        defaulted — three of its four options never did anything (M17-M27's
        own comment on `setParameter` admitted as much), and beyond the "real
        UI surface with dead options" cost, the four configurations don't even
        share a port shape (Voice needs a `Note` `spawn` input; Swarm-
        population needs none at all), a real structural mismatch for one node
        with a mode switch. Swarm-population/Swarm-transient/Trigger become
        their OWN real node types later, once actual Swarm/Trigger runtime
        machinery exists (a `VoiceManager`-equivalent for each) — not empty
        shells bolted onto this one now, which would just recreate the same
        dead-surface problem this rename fixes. `wiki/reports/
        InstanceAllocator_2026-09-28.md` has the full reasoning.

        Supersedes the upstream half of "util.voiceSum" (`RECONCILIATION.md`
        3.1).

        `spawn` (`Note` input, declared for schema completeness per
        `SIGNAL_TYPES.md`'s "ports never renamed once shipped") is real,
        functional `Note`-port delivery as of M18 (`io.noteIn`) — see
        `consumeNoteBlock()` below.

        Deliberately narrower than `NODE_CATALOG (1).md`'s full port list:
        `Pressure`/`Slide`/`ReleaseVelocity`/`Position`/`UnisonIndex`/
        `UnisonDetune` are omitted — nothing upstream (today's plain MIDI
        note-on/off) produces that data yet, and adding a port later is
        safe/additive (`VALUE_MODEL.md` §8's stability table), unlike removing
        one. Add them once something can actually drive them.
    */
    class InstanceVoiceNode : public Node
    {
    public:
        static constexpr int numInputs = 1;  // spawn
        static constexpr int numOutputs = 9; // gate, pitch, velocity, index, age, random1, random2, start, stop

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            random.setSeedRandomly(); // per-process-lifetime — patch-level determinism (DOMAINS.md §4) needs a real seed parameter, not built yet (09-28-InstanceAllocator.2)
        }

        void reset() override
        {
            gate = false;
            pitch = 60.0f;
            velocity = 0.0f;
            ageSamples = 0;
            startEvent = false;
            stopEvent = false;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Voice"; }
        // "Domain/Allocate" — nests under the Add menu's Domain category as a
        // flyout (09-29-AddMenu.1), alongside instance.mix which stays flat
        // "Domain" (see the class comment above for why). Sibling spawn
        // mechanisms (instance.allocate.swarmPopulation, etc., M28) land in
        // the same "Domain/Allocate" flyout once built.
        juce::String getCategory() const override { return "Domain/Allocate"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "spawn", .type = SignalType::Note } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "gate", .type = SignalType::Boolean, .isPrimaryOutput = true },
                // Absolute pitch (VALUE_MODEL.md §3: "semitones, 60 = middle
                // C") — deliberately NOT ValueTypes::pitchPort(), which is a
                // *relative* transpose/bend amount (its own doc comment says
                // so); this port's default IS the anchor, not an offset
                // from it.
                PortDescriptor { .id = "pitch", .type = SignalType::Control, .unit = "st", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 60.0f, .quantity = Quantity::Pitch },
                PortDescriptor { .id = "velocity", .type = SignalType::Control, .quantity = Quantity::Unipolar, .polarity = Polarity::Unipolar },
                PortDescriptor { .id = "instanceIndex", .type = SignalType::Control, .isInteger = true, .quantity = Quantity::Count, .step = 1.0f },
                PortDescriptor { .id = "instanceAge", .type = SignalType::Control, .quantity = Quantity::Time },
                PortDescriptor { .id = "random1", .type = SignalType::Control, .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "random2", .type = SignalType::Control, .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "start", .type = SignalType::Event },
                PortDescriptor { .id = "stop", .type = SignalType::Event },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            // 09-28-InstanceAllocator.3: "configuration" removed outright —
            // see the class comment. maxInstances is the only real structural
            // parameter this node has left.
            return { ParameterDescriptor { .id = "instance.allocate.voice.maxInstances",
                                            .minValue = 1.0f,
                                            .maxValue = 64.0f,
                                            .defaultValue = 8.0f,
                                            .displayName = "Max Instances",
                                            .isInteger = true,
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "instance.allocate.voice.maxInstances")
                maxInstances = (int) (value + 0.5f);
        }

        /** M18 (ADR-0024): the real end of the "spawn" input's Note-typed
            wiring — `io.noteIn`'s produceNoteBlock() feeds this per-sample
            buffer, whose start/stop edges call the SAME noteOn()/noteOff()
            below that a direct C++ poke (tests, render-cli-style tools)
            also calls. `pendingNoteBlock` is only valid for the immediately
            following processBlock() call this block, per Node.h's own
            contract — never retained past it.
        */
        void consumeNoteBlock (const NoteEvent* input, int numSamples) noexcept override
        {
            pendingNoteBlock = input;
            pendingNoteBlockLength = numSamples;
        }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            juce::ignoreUnused (inputs);

            for (int i = 0; i < numSamples; ++i)
            {
                if (pendingNoteBlock != nullptr && i < pendingNoteBlockLength)
                {
                    const auto& note = pendingNoteBlock[i];
                    if (note.startEvent)
                        noteOn (note.pitch, note.velocity);
                    if (note.stopEvent)
                        noteOff();
                }

                float sampleOut[numOutputs];
                processSample (nullptr, sampleOut);
                for (int o = 0; o < numOutputs; ++o)
                    outputs[o][i] = sampleOut[o];
            }

            pendingNoteBlock = nullptr;
        }

        /** Direct C++ poke, message/audio-thread call from whoever drives
            this voice (PluginProcessor, or a test/tool bypassing the graph
            entirely) — see class comment. Also what consumeNoteBlock()
            above calls internally on a real Note connection's start/stop
            edges, so both paths share one implementation.
        */
        void noteOn (float pitchIn, float velocityIn) noexcept
        {
            gate = true;
            pitch = pitchIn;
            velocity = velocityIn;
            ageSamples = 0;
            startEvent = true;
            ++instanceIndex;
            random1Value = random.nextFloat() * 2.0f - 1.0f;
            random2Value = random.nextFloat() * 2.0f - 1.0f;
        }

        void noteOff() noexcept
        {
            gate = false;
            stopEvent = true;
        }

        void processSample (const float*, float* outputs) noexcept override
        {
            outputs[0] = gate ? 1.0f : 0.0f;
            outputs[1] = pitch;
            outputs[2] = velocity;
            outputs[3] = (float) instanceIndex;
            outputs[4] = (float) ageSamples / (float) (sampleRate > 0.0 ? sampleRate : 44100.0);
            outputs[5] = random1Value;
            outputs[6] = random2Value;
            outputs[7] = startEvent ? 1.0f : 0.0f;
            outputs[8] = stopEvent ? 1.0f : 0.0f;

            ++ageSamples;
            startEvent = false;
            stopEvent = false;
        }

    private:
        double sampleRate = 44100.0;
        int maxInstances = 8;

        bool gate = false;
        float pitch = 60.0f;
        float velocity = 0.0f;
        int64_t ageSamples = 0;
        bool startEvent = false;
        bool stopEvent = false;
        int instanceIndex = 0;
        float random1Value = 0.0f;
        float random2Value = 0.0f;
        juce::Random random;

        const NoteEvent* pendingNoteBlock = nullptr; // M18 — valid only for the processBlock() call following consumeNoteBlock()
        int pendingNoteBlockLength = 0;
    };
}
