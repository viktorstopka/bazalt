#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/nodes/InstanceSeeding.h"
#include <cmath>
#include <juce_core/juce_core.h>

namespace bazalt::engine::nodes
{
    /** Stable type id: "instance.allocate.swarmPopulation" (Domain
        Extensions batch, `wiki/NODES.Status.md`). Opens an instanced (Poly)
        region like `instance.allocate.voice`, but "fixed count, always
        live" (`archive_docs/DOMAINS.md` §3's own table) — no spawn source
        at all, deliberately NOT implementing `InstanceOriginNode` (there is
        nothing to spawn or release after `prepare()`; every live instance
        is simply Active from the very first block onward).

        Like Voice, this node is compiled once per physical voice-plan slot
        in its origin bundle (`BazaltAudioProcessor::numVoices`, currently
        8) — but unlike Voice, nothing ever calls `noteOn()`/`noteOff()` to
        distinguish one compiled copy from another. `setInstanceSlot()`
        (called once, right after compile, by whoever owns the per-slot
        compile loop — `GraphEditController::recompileAndPublish()`, the
        exact same place that already reads `InstanceVoiceNode::
        getMaxInstances()` back off a freshly-compiled node) is what tells
        each physical copy which of the N slots it physically is, so
        `populationSize` can decide which slots report `Gate = true` versus
        silently unused, and so each slot's own deterministic random/
        position values differ from its siblings'.

        Shared instance-context outputs match `InstanceVoiceNode`'s own
        non-Voice-specific seven exactly (`gate`/`instanceIndex`/
        `instanceAge`/`random1`/`random2`/`start`/`stop`) plus `position`
        (`Control`, `Bipolar` — a pan-like per-instance value):
        `DOMAINS.md` §4's "Position (spatial: pan, distance) for swarm
        configurations" scoped down to one plain value for MVP rather than a
        full spatial-position-model subsystem — a deliberate scope cut, not
        an oversight; revisit if a real patch ever needs more than "stable
        per-instance pan."
    */
    class InstanceSwarmPopulationNode : public Node
    {
    public:
        static constexpr int numInputs = 0;
        static constexpr int numOutputs = 8; // gate, instanceIndex, instanceAge, random1, random2, start, stop, position

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
        }

        void reset() override
        {
            ageSamples = 0;
            startEvent = true; // fires once — the block this instance first becomes live
            stopEvent = false;
            randomsComputed = false;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Swarm (Population)"; }
        // Same "Domain/Allocate" flyout Voice already nests under
        // (09-29-AddMenu.1) — the Add menu's category tree builds itself
        // dynamically from real descriptors, so this needs no UI-side work.
        juce::String getCategory() const override { return "Domain/Allocate"; }

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
                // NOT named "maxInstances" — there is no demand-driven
                // ceiling to distinguish from the live count here. This
                // many instances are ALWAYS live, full stop.
                ParameterDescriptor { .id = "instance.allocate.swarmPopulation.populationSize",
                                       .minValue = 1.0f,
                                       .maxValue = 64.0f,
                                       .defaultValue = 8.0f,
                                       .displayName = "Population Size",
                                       .isInteger = true,
                                       .quantity = Quantity::Count,
                                       .step = 1.0f,
                                       .isStructural = true },
                ParameterDescriptor { .id = "instance.allocate.swarmPopulation.seed",
                                       .minValue = 0.0f,
                                       .maxValue = 999999.0f,
                                       .defaultValue = 1.0f,
                                       .displayName = "Seed",
                                       .isInteger = true,
                                       .quantity = Quantity::Count,
                                       .step = 1.0f,
                                       .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "instance.allocate.swarmPopulation.populationSize")
                populationSize = (int) std::lround (value);
            else if (parameterId == "instance.allocate.swarmPopulation.seed")
                seed = (int) std::lround (value);
        }

        int getPopulationSize() const noexcept { return populationSize; }

        /** Message-thread only — called exactly once, right after this
            node is freshly compiled into a specific physical voice-plan
            slot, by whoever owns the per-slot compile loop. See this
            class's own doc comment for why this exists at all (nothing
            else distinguishes one compiled copy of this node from
            another — there's no noteOn()-driven instanceIndex the way
            Voice has).
        */
        void setInstanceSlot (int slotIndexIn) noexcept
        {
            slotIndex = slotIndexIn;
            randomsComputed = false; // re-derive if the slot assignment ever changes
        }

        int getInstanceSlot() const noexcept { return slotIndex; }

        void processSample (const float*, float* outputs) noexcept override
        {
            if (! randomsComputed)
            {
                auto random = juce::Random (combineInstanceSeed (seed, slotIndex));
                random1Value = random.nextFloat() * 2.0f - 1.0f;
                random2Value = random.nextFloat() * 2.0f - 1.0f;
                positionValue = random.nextFloat() * 2.0f - 1.0f;
                randomsComputed = true;
            }

            const auto isLive = slotIndex < populationSize;

            outputs[0] = isLive ? 1.0f : 0.0f;
            outputs[1] = (float) slotIndex;
            outputs[2] = (float) ageSamples / (float) (sampleRate > 0.0 ? sampleRate : 44100.0);
            outputs[3] = random1Value;
            outputs[4] = random2Value;
            outputs[5] = startEvent ? 1.0f : 0.0f;
            outputs[6] = stopEvent ? 1.0f : 0.0f;
            outputs[7] = positionValue;

            ++ageSamples;
            startEvent = false;
        }

    private:
        double sampleRate = 44100.0;
        int populationSize = 8;
        int seed = 1; // deterministic by default, matching random.stepped.seed's own convention
        int slotIndex = 0; // which of the N physical voice-plan slots this compiled copy is

        int64_t ageSamples = 0;
        bool startEvent = false;
        bool stopEvent = false;

        bool randomsComputed = false;
        float random1Value = 0.0f;
        float random2Value = 0.0f;
        float positionValue = 0.0f;
    };
}
