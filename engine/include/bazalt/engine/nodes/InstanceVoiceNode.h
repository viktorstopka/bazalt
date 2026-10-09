#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/nodes/InstanceOriginNode.h"
#include "bazalt/engine/nodes/InstanceSeeding.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <juce_core/juce_core.h>

namespace bazalt::engine::nodes
{
    /** Stable type id: "instance.allocate.voice" (M17, renamed from "instance.allocator"
        in 09-28-InstanceAllocator.3, renamed again from "instance.voice" in
        09-29-AddMenu.1 — CLAUDE.md rule 3 is suspended, so both are direct
        renames, no migration path). The second rename inserts an "allocate"
        namespace segment purely so the Add menu's category tree (also
        09-29-AddMenu.1) can nest Voice under Domain > Allocate — `instance.sum`
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
    class InstanceVoiceNode : public Node, public InstanceOriginNode
    {
    public:
        static constexpr int numInputs = 1;  // spawn
        static constexpr int numOutputs = 9; // gate, pitch, velocity, index, age, random1, random2, start, stop

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
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
        // flyout (09-29-AddMenu.1), alongside instance.sum which stays flat
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
                PortDescriptor { .id = "gate", .type = SignalType::Signal, .isPrimaryOutput = true, .quantity = Quantity::Boolean },
                // Absolute pitch (VALUE_MODEL.md §3: "semitones, 60 = middle
                // C") — deliberately NOT ValueTypes::pitchPort(), which is a
                // *relative* transpose/bend amount (its own doc comment says
                // so); this port's default IS the anchor, not an offset
                // from it.
                PortDescriptor { .id = "pitch", .type = SignalType::Signal, .unit = "st", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 60.0f, .quantity = Quantity::Pitch },
                PortDescriptor { .id = "velocity", .type = SignalType::Signal, .quantity = Quantity::Unipolar, .polarity = Polarity::Unipolar },
                PortDescriptor { .id = "instanceIndex", .type = SignalType::Signal, .isInteger = true, .quantity = Quantity::Count, .step = 1.0f },
                PortDescriptor { .id = "instanceAge", .type = SignalType::Signal, .quantity = Quantity::Time },
                PortDescriptor { .id = "random1", .type = SignalType::Signal, .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "random2", .type = SignalType::Signal, .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar },
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
                                            .isStructural = true },
                     // 09-28-InstanceAllocator.2: real patch-level determinism
                     // at last — same shape/convention as random.stepped.seed/
                     // random.drift.seed (RandomSteppedNode.h/RandomDriftNode.h),
                     // deterministic-by-default rather than time-based.
                     ParameterDescriptor { .id = "instance.allocate.voice.seed",
                                            .minValue = 0.0f,
                                            .maxValue = 999999.0f,
                                            .defaultValue = 1.0f,
                                            .displayName = "Seed",
                                            .isInteger = true,
                                            .quantity = Quantity::Count,
                                            .step = 1.0f,
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "instance.allocate.voice.maxInstances")
                maxInstances = (int) (value + 0.5f);
            else if (parameterId == "instance.allocate.voice.seed")
                seed = (int) std::lround (value);
        }

        /** wiki/plans/DomainRedesign.md Batch 4: this was declared and
            editable but read nowhere — GraphEditController now reads it
            straight off this node, once per recompile, to actually enforce
            it via VoiceManager::setMaxActiveVoices() (closing the gap this
            class's own comment used to flag) and to report the "maxCount"
            half of the UI's instance-count badge.
        */
        int getMaxInstances() const noexcept override { return maxInstances; }

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

            // 09-28-InstanceAllocator.2: random1/random2 are now a pure
            // function of (patch seed, spawn ordinal) - a FRESH juce::Random
            // constructed here every spawn, never a persistent member
            // advanced call-to-call. That's what makes "same patch, same
            // MIDI, same seed -> bit-identical output" hold (DOMAINS.md §4's
            // own stated reason these ports are allocator-owned state in the
            // first place): the old persistent-member design reseeded
            // randomly once per plugin-process-lifetime (prepare()'s own
            // setSeedRandomly() call) and then depended on exactly how many
            // nextFloat() calls had already happened this run - neither of
            // which is reproducible across runs, hosts, or even two voices
            // racing in a different note order.
            auto perSpawnRandom = juce::Random (combineInstanceSeed (seed, instanceIndex));
            random1Value = perSpawnRandom.nextFloat() * 2.0f - 1.0f;
            random2Value = perSpawnRandom.nextFloat() * 2.0f - 1.0f;

            spawnEventsThisBlock.fetch_add (1, std::memory_order_relaxed);
        }

        void noteOff() noexcept
        {
            gate = false;
            stopEvent = true;
            spawnEventsThisBlock.fetch_add (1, std::memory_order_relaxed);
        }

        /** DomainRedesign.md Batch 1b — the MIDI-independence fix. Root
            cause: `VoiceManager`'s lanes only ever leave `VoiceStage::Idle`
            via `PluginProcessor::handleMidiEvent`, driven by real host
            MIDI — a graph that drives this node's own "spawn" input purely
            from an internal Note-typed producer (clock -> seq ->
            note.assemble, no io.noteIn anywhere) has that Note event reach
            `noteOn()`/`noteOff()` above correctly (real, working
            `Note`-port delivery since M18), but that only matters once the
            lane is already rendering — which it never becomes, since
            nothing ever calls `VoiceManager::noteOn()` for it.

            The fix doesn't live here: `PluginProcessor` keeps ONE voice
            slot per origin running every block regardless of `VoiceStage`
            (mirroring how a mono graph already runs unconditionally),
            purely to let this node's own graph-wired trigger evaluate, and
            watches this counter for a change to detect "the graph itself
            just fired a noteOn/noteOff, independent of host MIDI" — then
            drives `VoiceManager` (and, for any OTHER voice slot the
            allocator picks, a direct `noteOn()`/`noteOff()` poke on that
            slot's own compiled instance) exactly as if a real MIDI message
            had arrived. Real host MIDI keeps working unchanged — this
            counter simply also increments on that path, harmlessly, since
            nothing reads it there.

            One counter for both directions (not a separate noteOn/noteOff
            tally) — deliberately: the driver only needs to know a
            transition HAPPENED, and reads this node's own current `gate`
            state (already real, already correct) to tell which one.
            `std::atomic` even though today's only reader is the same audio
            thread that writes it within the same `processBlock()` call —
            cheap, and matches this codebase's own established pattern for
            an audio-thread-written, occasionally-read count
            (`PluginProcessor::auxPeakLevels`).
        */
        int consumeSpawnEventsThisBlock() noexcept override { return spawnEventsThisBlock.exchange (0, std::memory_order_relaxed); }

        bool getGate() const noexcept override { return gate; }
        float getPitch() const noexcept override { return pitch; }
        float getVelocity() const noexcept override { return velocity; }

        /** InstanceOriginNode's generic dispatch surface — forwards to the
            real noteOn()/noteOff() below, which stay the real, direct-poke
            entry points (tests/tools/real MIDI dispatch all call those by
            name, unchanged). These two exist only so
            PluginProcessor's origin-relay dispatch can target ANY
            InstanceOriginNode polymorphically, Voice included, without a
            concrete-type special case.
        */
        void spawnInstance (float pitchIn, float velocityIn) noexcept override { noteOn (pitchIn, velocityIn); }
        void releaseInstance() noexcept override { noteOff(); }

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
        int seed = 1; // deterministic by default, matching random.stepped.seed's own convention

        std::atomic<int> spawnEventsThisBlock { 0 }; // DomainRedesign.md Batch 1b — see consumeSpawnEventsThisBlock()

        bool gate = false;
        float pitch = 60.0f;
        float velocity = 0.0f;
        int64_t ageSamples = 0;
        bool startEvent = false;
        bool stopEvent = false;
        int instanceIndex = 0;
        float random1Value = 0.0f;
        float random2Value = 0.0f;

        const NoteEvent* pendingNoteBlock = nullptr; // M18 — valid only for the processBlock() call following consumeNoteBlock()
        int pendingNoteBlockLength = 0;
    };
}
