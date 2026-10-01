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
    /** Stable type id: "instance.allocate.swarmTransient" (Domain Extensions
        batch, the real test of `InstanceOriginNode`'s generalization —
        `PluginProcessor`'s internal-trigger-relay mechanism now dispatches a
        non-Voice origin for the first time).

        Opens an instanced region whose spawn source is a plain `Event`
        (`spawn`), not a `Note` — no pitch/velocity concept at all
        (`spawnInstance()`'s own args are ignored, matching
        `InstanceOriginNode`'s own doc comment). Each firing restarts this
        instance's own gate/age/random state (fresh `random1`/`random2`/
        `position`, same `(seed, instanceIndex)` determinism
        `09-28-InstanceAllocator.2` built for Voice, reused verbatim via
        `combineInstanceSeed`) and auto-releases itself after `duration`
        seconds — there is no separate "release" input port; a transient is
        inherently self-terminating, unlike Voice's explicit note-off.

        **Known scope limit, found while building this (not fixed — a
        deeper redesign than this batch's own scope):** `PluginProcessor`'s
        internal-trigger relay (shared with Voice's own internally-sequenced
        case, `renderOriginVoiceRange`) reads ONE node copy's own
        gate/instanceIndex state to detect "a spawn or release just
        happened, dispatch it to whichever physical voice lane
        `VoiceManager` assigns" — that single copy can only ever represent
        ONE in-flight transient at a time. A spawn arriving while an earlier
        one (from the SAME internal generator) hasn't yet auto-released
        overwrites that copy's own tracked state before its matching
        release is ever read, so the earlier transient's own voice lane
        never gets an explicit `noteOff` — it's not a permanent leak
        (`VoiceManager`'s existing stealing policy reclaims any lane once
        every physical lane is busy, exactly as it already does for a real
        MIDI note flurry exceeding `maxInstances`), but `maxInstances` only
        really behaves as a genuine "several simultaneously live transients"
        ceiling once something can give each spawn its own real identity —
        not available today for a purely internal generator feeding one
        `Event` port. Fine for the realistic, tested case (sequential
        spawn/auto-release, well below `duration`'s own rate) — flagged
        here rather than silently overclaiming more than what's built, this
        project's own established pattern for documenting a real scope cut.
    */
    class InstanceSwarmTransientNode : public Node, public InstanceOriginNode
    {
    public:
        static constexpr int numInputs = 1;  // spawn
        static constexpr int numOutputs = 8; // gate, index, age, random1, random2, start, stop, position

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            recomputeDurationSamples();
        }

        void reset() override
        {
            gate = false;
            ageSamples = 0;
            startEvent = false;
            stopEvent = false;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Swarm (Transient)"; }
        // "Domain/Allocate" — same flyout as Voice/Swarm-population (09-29-AddMenu.1).
        juce::String getCategory() const override { return "Domain/Allocate"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "spawn", .type = SignalType::Event } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "gate", .type = SignalType::Boolean, .isPrimaryOutput = true },
                PortDescriptor { .id = "instanceIndex", .type = SignalType::Control, .isInteger = true, .quantity = Quantity::Count, .step = 1.0f },
                PortDescriptor { .id = "instanceAge", .type = SignalType::Control, .quantity = Quantity::Time },
                PortDescriptor { .id = "random1", .type = SignalType::Control, .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "random2", .type = SignalType::Control, .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "start", .type = SignalType::Event },
                PortDescriptor { .id = "stop", .type = SignalType::Event },
                PortDescriptor { .id = "position", .type = SignalType::Control, .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "instance.allocate.swarmTransient.maxInstances",
                                       .minValue = 1.0f, .maxValue = 64.0f, .defaultValue = 8.0f,
                                       .displayName = "Max Instances", .isInteger = true, .isStructural = true },
                ParameterDescriptor { .id = "instance.allocate.swarmTransient.seed",
                                       .minValue = 0.0f, .maxValue = 999999.0f, .defaultValue = 1.0f,
                                       .displayName = "Seed", .isInteger = true,
                                       .quantity = Quantity::Count, .step = 1.0f, .isStructural = true },
                // Not structural — purely an internal "when to auto-release"
                // threshold read every processSample() call, no compiled-plan
                // shape implication, live-editable like random.stepped.rate.
                ParameterDescriptor { .id = "instance.allocate.swarmTransient.duration",
                                       .minValue = 0.001f, .maxValue = 5.0f, .defaultValue = 0.1f,
                                       .unit = "s", .displayName = "Duration",
                                       .quantity = Quantity::Time, .curve = Curve::Logarithmic },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "instance.allocate.swarmTransient.maxInstances")
                maxInstances = (int) std::lround (value);
            else if (parameterId == "instance.allocate.swarmTransient.seed")
                seed = (int) std::lround (value);
            else if (parameterId == "instance.allocate.swarmTransient.duration")
            {
                durationSeconds = juce::jmax (0.001f, value);
                recomputeDurationSamples();
            }
        }

        int getMaxInstances() const noexcept override { return maxInstances; }

        bool getGate() const noexcept override { return gate; }
        int consumeSpawnEventsThisBlock() noexcept override { return spawnEventsThisBlock.exchange (0, std::memory_order_relaxed); }

        /** InstanceOriginNode's generic dispatch surface — pitch/velocity
            are accepted (interface shape) but ignored entirely, matching
            the interface's own doc comment ("Swarm-transient/Trigger:
            ignore the args"). Also the SAME entry point this node's own
            wired "spawn" Event input calls internally (processSample()
            below) — unlike Voice, there's no separate "real" named method,
            since there's no direct pitch/velocity-bearing poke convention
            for this type to preserve.
        */
        void spawnInstance (float, float) noexcept override
        {
            gate = true;
            ageSamples = 0;
            startEvent = true;
            ++instanceIndex;

            // 09-28-InstanceAllocator.2's own determinism, reused verbatim.
            auto perSpawnRandom = juce::Random (combineInstanceSeed (seed, instanceIndex));
            random1Value = perSpawnRandom.nextFloat() * 2.0f - 1.0f;
            random2Value = perSpawnRandom.nextFloat() * 2.0f - 1.0f;
            positionValue = perSpawnRandom.nextFloat() * 2.0f - 1.0f;

            spawnEventsThisBlock.fetch_add (1, std::memory_order_relaxed);
        }

        void releaseInstance() noexcept override
        {
            gate = false;
            stopEvent = true;
            spawnEventsThisBlock.fetch_add (1, std::memory_order_relaxed);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            // Event convention (RandomSteppedNode.h's own "trigger" input):
            // non-zero this sample = fired, no edge-detection needed - an
            // Event signal is semantically a one-sample-wide impulse.
            if (inputs != nullptr && std::fabs (inputs[0]) > 0.0f)
                spawnInstance (0.0f, 0.0f);

            if (gate && ageSamples >= durationSamples)
                releaseInstance();

            outputs[0] = gate ? 1.0f : 0.0f;
            outputs[1] = (float) instanceIndex;
            outputs[2] = (float) ageSamples / (float) (sampleRate > 0.0 ? sampleRate : 44100.0);
            outputs[3] = random1Value;
            outputs[4] = random2Value;
            outputs[5] = startEvent ? 1.0f : 0.0f;
            outputs[6] = stopEvent ? 1.0f : 0.0f;
            outputs[7] = positionValue;

            ++ageSamples;
            startEvent = false;
            stopEvent = false;
        }

    private:
        void recomputeDurationSamples() noexcept
        {
            durationSamples = (int64_t) ((double) durationSeconds * (sampleRate > 0.0 ? sampleRate : 44100.0));
        }

        double sampleRate = 44100.0;
        int maxInstances = 8;
        int seed = 1;
        float durationSeconds = 0.1f;
        int64_t durationSamples = 4410;

        std::atomic<int> spawnEventsThisBlock { 0 };

        bool gate = false;
        int64_t ageSamples = 0;
        bool startEvent = false;
        bool stopEvent = false;
        int instanceIndex = 0;
        float random1Value = 0.0f;
        float random2Value = 0.0f;
        float positionValue = 0.0f;
    };
}
