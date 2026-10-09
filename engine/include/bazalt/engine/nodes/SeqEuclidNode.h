#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "seq.euclid" (wiki/NODES.md's `seq.*` row, the
        Clock+Seq batch). Euclidean rhythm generator: distributes `pulses`
        hits as evenly as possible across `steps`.

        **Algorithm**: step `i` (0-indexed) is a pulse iff
        `floor(i*pulses/steps) != floor((i-1)*pulses/steps)` — the standard
        "digital differential analyzer" formula, equivalent to Bjorklund's
        algorithm's own onset pattern without needing its recursive
        construction. Using signed double division, `i=0`'s comparison
        against `i=-1` falls out correctly with no special case (`floor` of
        a negative fraction is already what makes the first step a pulse
        whenever `pulses > 0`). Verified by hand for `steps=8, pulses=3`:
        onsets at 0, 3, 6 - the textbook tresillo (3-3-2) pattern.

        `rotate` shifts which step of the pattern is read at a given
        position (`((index + rotate) % steps + steps) % steps`) without
        moving the sequencer's own advancing index - rotating the read head
        over a fixed pattern, not regenerating a shifted one.

        **`gate`'s type**: the catalog leaves it unmarked (no explicit
        `Event`/`bool` tag) - every other "gate" port in the whole catalog
        is Boolean (`env.adsr`, `note.gate`, ...), so this follows that
        convention rather than inventing a new one.

        Convention (matches `seq.steps`): holds at step 0 until the first
        tick, which advances to step 1 - see that node's own comment for the
        full reasoning.
    */
    class SeqEuclidNode : public Node
    {
    public:
        static constexpr int numInputs = 5;  // tick, steps, pulses, rotate, reset
        static constexpr int numOutputs = 2; // trigger, gate

        void reset() override { currentIndex = 0; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Euclidean"; }
        juce::String getCategory() const override { return "Sequencing"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "tick", .type = SignalType::Event, .label = "Tick" },
                PortDescriptor { .id = "seq.euclid.steps", .type = SignalType::Signal, .label = "Steps",
                                  .minValue = 1.0f, .maxValue = 64.0f, .defaultValue = 16.0f, .isInteger = true,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Count, .step = 1.0f },
                PortDescriptor { .id = "seq.euclid.pulses", .type = SignalType::Signal, .label = "Pulses",
                                  .minValue = 0.0f, .maxValue = 64.0f, .defaultValue = 4.0f, .isInteger = true,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Count, .step = 1.0f },
                PortDescriptor { .id = "seq.euclid.rotate", .type = SignalType::Signal, .label = "Rotate",
                                  .minValue = -64.0f, .maxValue = 64.0f, .defaultValue = 0.0f, .isInteger = true,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Count, .step = 1.0f },
                PortDescriptor { .id = "reset", .type = SignalType::Event, .label = "Reset" },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "trigger", .type = SignalType::Event, .label = "Trigger", .isPrimaryOutput = true },
                PortDescriptor { .id = "gate", .type = SignalType::Signal, .label = "Gate", .kind = ValueKind::Bool, .quantity = Quantity::Boolean },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "seq.euclid.steps")
                storedSteps = juce::jmax (1, (int) std::lround (value));
            else if (parameterId == "seq.euclid.pulses")
                storedPulses = juce::jmax (0, (int) std::lround (value));
            else if (parameterId == "seq.euclid.rotate")
                storedRotate = (int) std::lround (value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto tickIn = std::fabs (inputs[0]) > 0.0f;
            const auto steps = std::isnan (inputs[1]) ? storedSteps : juce::jmax (1, (int) std::lround (inputs[1]));
            const auto pulses = std::isnan (inputs[2]) ? storedPulses : juce::jmax (0, (int) std::lround (inputs[2]));
            const auto rotate = std::isnan (inputs[3]) ? storedRotate : (int) std::lround (inputs[3]);
            const auto resetIn = std::fabs (inputs[4]) > 0.0f;

            if (resetIn)
                currentIndex = 0;

            auto triggered = false;

            if (tickIn)
            {
                currentIndex = (currentIndex + 1) % steps;
                triggered = true;
            }

            const auto effective = ((currentIndex + rotate) % steps + steps) % steps;
            const auto onPulse = isPulse (effective, steps, pulses);

            outputs[0] = (triggered && onPulse) ? 1.0f : 0.0f;
            outputs[1] = onPulse ? 1.0f : 0.0f;
        }

    private:
        static bool isPulse (int i, int steps, int pulses) noexcept
        {
            if (pulses <= 0)
                return false;
            if (pulses >= steps)
                return true;

            const auto current = (int) std::floor ((double) i * pulses / (double) steps);
            const auto previous = (int) std::floor ((double) (i - 1) * pulses / (double) steps);
            return current != previous;
        }

        int currentIndex = 0;
        int storedSteps = 16;
        int storedPulses = 4;
        int storedRotate = 0;
    };
}
