#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <juce_core/juce_core.h>

namespace bazalt::engine::nodes
{
    /** Stable type id: "instance.allocator" (M17). `DOMAINS.md` §3's
        Instance Allocator — **Voice configuration only** for M17; Swarm-
        population/Swarm-transient/Trigger are M28 (Batch H). Supersedes
        the upstream half of "util.voiceSum" (`RECONCILIATION.md` 3.1).

        `spawn` (`Note` input, declared for schema completeness per
        `SIGNAL_TYPES.md`'s "ports never renamed once shipped") is **not
        wireable/functional yet** — `Note` has no real producer until M18
        (`io.noteIn`). For M17, whoever drives voices (`PluginProcessor`)
        pokes this node directly via `noteOn()`/`noteOff()`, exactly the
        same "poke a concrete node type via `getNodeById` + `dynamic_cast`"
        pattern `AdsrNode`'s own `noteOn()`/`noteOff()` already uses — this
        node's job is to be the ONE thing poked (replacing separate pokes
        into "osc"/"env" by name) and expose the result as real, ordinary
        output ports. M18 replaces the poke with real `Note`-port
        delivery; the output ports below don't change shape when that
        happens.

        Deliberately narrower than `NODE_CATALOG (1).md`'s full port list:
        `Pressure`/`Slide`/`ReleaseVelocity`/`Position`/`UnisonIndex`/
        `UnisonDetune` are omitted for M17 — nothing upstream (today's
        plain MIDI note-on/off) produces that data yet, and adding a port
        later is safe/additive (`VALUE_MODEL.md` §8's stability table),
        unlike removing one. Add them once something can actually drive
        them.
    */
    class InstanceAllocatorNode : public Node
    {
    public:
        static constexpr int numInputs = 1;  // spawn (inert, see class comment)
        static constexpr int numOutputs = 9; // gate, pitch, velocity, index, age, random1, random2, start, stop

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            random.setSeedRandomly(); // per-process-lifetime — patch-level determinism (DOMAINS.md §4) needs a real seed parameter, not built yet (M28 territory once Swarm needs it for real)
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

        juce::String getTitle() const override { return "Instance Allocator"; }
        juce::String getCategory() const override { return "Domain"; }

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
            return { ParameterDescriptor { .id = "instance.allocator.configuration",
                                            .minValue = 0.0f,
                                            .maxValue = 3.0f,
                                            .defaultValue = 0.0f,
                                            .displayName = "Configuration",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "voice", "Voice" },
                                                              { "swarmPopulation", "Swarm (population)" },
                                                              { "swarmTransient", "Swarm (transient)" },
                                                              { "trigger", "Trigger" } },
                                            .isStructural = true },
                     ParameterDescriptor { .id = "instance.allocator.maxInstances",
                                            .minValue = 1.0f,
                                            .maxValue = 64.0f,
                                            .defaultValue = 8.0f,
                                            .displayName = "Max Instances",
                                            .isInteger = true,
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "instance.allocator.configuration")
            {
                // M17 only ever actually runs as Voice — a non-zero value
                // here doesn't switch behaviour yet (Swarm/Trigger are
                // M28); recorded so the descriptor round-trips honestly
                // once a real UI can set it, not silently ignored.
                configuration = (int) (value + 0.5f);
            }
            else if (parameterId == "instance.allocator.maxInstances")
            {
                maxInstances = (int) (value + 0.5f);
            }
        }

        /** Direct C++ poke, message/audio-thread call from whoever drives
            this voice (PluginProcessor) — see class comment.
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
        int configuration = 0;
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
    };
}
