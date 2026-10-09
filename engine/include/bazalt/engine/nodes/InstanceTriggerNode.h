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
    /** Stable type id: "life.trigger" (Domain Extensions
        batch, Batch 4 — reuses everything Batch 3's `InstanceSwarmTransientNode`
        and `PluginProcessor`'s generalized relay already built; the only new
        work is this node class itself).

        Opens an instanced region with exactly one instance at a time — no
        `maxInstances` parameter at all (`InstanceOriginNode::getMaxInstances()`'s
        own default of `1` is never overridden, matching Voice's own
        precedent of only exposing parameters that actually do something).
        Spawn source is a plain `Event` (`trigger`), not `Note` — same
        pitch/velocity-ignoring shape Swarm-transient has. No `position`
        output either — not a swarm type at all
        (`archive_docs/DOMAINS.md` §4's own table: Position is swarm-specific).

        **Deliberately no auto-release** (unlike Swarm-transient's own
        `duration` parameter — DOMAINS.md's own table doesn't describe one
        for this configuration either): a trigger's gate stays asserted
        until the NEXT trigger fires. "A second trigger re-triggers the same
        instance" falls out of `maxInstances == 1` for free, via
        `VoiceManager`'s own existing, already-tested stealing policy —
        firing `trigger` again while the single lane is still Active finds
        no idle lane, steals the one lane that exists (fading briefly per
        `VoiceManager`'s own fade, then retriggering fresh gate/age/random
        via the exact same `spawnInstance()` the relay already calls
        polymorphically), with zero special-casing needed here for
        "re-trigger" behavior specifically.
    */
    class InstanceTriggerNode : public Node, public InstanceOriginNode
    {
    public:
        static constexpr int numInputs = 1;  // trigger
        static constexpr int numOutputs = 7; // gate, index, age, random1, random2, start, stop

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }

        void reset() override
        {
            gate = false;
            ageSamples = 0;
            startEvent = false;
            stopEvent = false;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Trigger"; }
        // "Domain/Allocate" — same flyout as Voice/Swarm-population/Swarm-transient (09-29-AddMenu.1).
        juce::String getCategory() const override { return "Life-cycle"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "trigger", .type = SignalType::Event } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "gate", .type = SignalType::Signal, .isPrimaryOutput = true, .quantity = Quantity::Boolean },
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
            // 09-28-InstanceAllocator.3's own precedent: only expose a
            // parameter that actually does something. maxInstances is
            // implicitly 1 for this type (InstanceOriginNode's own default
            // getMaxInstances(), never overridden) - not an editable
            // parameter at all.
            return {
                ParameterDescriptor { .id = "life.trigger.seed",
                                       .minValue = 0.0f, .maxValue = 999999.0f, .defaultValue = 1.0f,
                                       .displayName = "Seed", .isInteger = true,
                                       .quantity = Quantity::Count, .step = 1.0f, .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "life.trigger.seed")
                seed = (int) std::lround (value);
        }

        bool getGate() const noexcept override { return gate; }
        int consumeSpawnEventsThisBlock() noexcept override { return spawnEventsThisBlock.exchange (0, std::memory_order_relaxed); }

        /** InstanceOriginNode's generic dispatch surface — pitch/velocity
            ignored, same as Swarm-transient. Also the entry point this
            node's own wired "trigger" Event input calls internally
            (processSample() below).
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

            spawnEventsThisBlock.fetch_add (1, std::memory_order_relaxed);
        }

        /** Real and callable (InstanceOriginNode requires it), but nothing
            in this node's own processSample() ever calls it - deliberately
            no auto-release (see class comment). Exists for interface
            symmetry and for whatever future explicit-release mechanism
            might eventually call it, same as every InstanceOriginNode
            implementor provides one.
        */
        void releaseInstance() noexcept override
        {
            gate = false;
            stopEvent = true;
            spawnEventsThisBlock.fetch_add (1, std::memory_order_relaxed);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            // Event convention (RandomSteppedNode.h's own "trigger" input):
            // non-zero this sample = fired.
            if (inputs != nullptr && std::fabs (inputs[0]) > 0.0f)
                spawnInstance (0.0f, 0.0f);

            outputs[0] = gate ? 1.0f : 0.0f;
            outputs[1] = (float) instanceIndex;
            outputs[2] = (float) ageSamples / (float) (sampleRate > 0.0 ? sampleRate : 44100.0);
            outputs[3] = random1Value;
            outputs[4] = random2Value;
            outputs[5] = startEvent ? 1.0f : 0.0f;
            outputs[6] = stopEvent ? 1.0f : 0.0f;

            ++ageSamples;
            startEvent = false;
            stopEvent = false;
        }

    private:
        double sampleRate = 44100.0;
        int seed = 1;

        std::atomic<int> spawnEventsThisBlock { 0 };

        bool gate = false;
        int64_t ageSamples = 0;
        bool startEvent = false;
        bool stopEvent = false;
        int instanceIndex = 0;
        float random1Value = 0.0f;
        float random2Value = 0.0f;
    };
}
