#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "time.gateLength". Direct feedback: `note.assemble`
        (and anything else driven by a plain gate — `env.adsr`, `instance.
        allocate.voice.spawn` via `note.assemble`) has no built-in way to
        say "hold this note for N seconds" from a bare trigger — the
        workaround was two clocks, one retriggering the other, which is
        real complexity for what should be a one-node primitive. This is
        that primitive: a monostable trigger-to-gate, the standard
        "Gate Length" utility a modular rack always has one of.

        On `trigger`, opens `gate` for `length` seconds, sample-accurately
        (a `length` of 0 opens for zero samples — never actually visible on
        `gate`, not a special case). A new `trigger` arriving before the
        gate has closed RETRIGGERS — restarts the countdown from the full
        `length`, the conventional monostable behaviour (Eurorack's own
        "Gate Length"/"Trigger to Gate" utilities all retrigger this way)
        — rather than queuing, extending additively, or being ignored.

        Deliberately NOT auto-inserted by `canConnect` for a plain
        `Event -> Boolean` wire-drag: `length` is a real musical/timing
        choice, not a mechanical, opinion-free crossing (the same bar
        `env.follower` failed for `Audio -> Control` auto-insertion, wiki/
        plans/AudioControlBridge.md §5) — placed by hand, on purpose.
    */
    class GateLengthNode : public Node
    {
    public:
        static constexpr float defaultLengthSeconds = 0.2f;
        static constexpr int numInputs = 2;  // trigger, length
        static constexpr int numOutputs = 1; // gate

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }
        void reset() override { remainingSamples = 0; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Gate Length"; }
        juce::String getCategory() const override { return "Time"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "trigger", .type = SignalType::Event, .label = "Trigger" },
                ValueTypes::timeSecondsPort ("length", "Length", defaultLengthSeconds),
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "gate", .type = SignalType::Signal, .isPrimaryOutput = true, .kind = ValueKind::Bool, .quantity = Quantity::Boolean } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "length")
                storedLength = std::max (0.0f, value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto triggerFired = std::fabs (inputs[0]) > 0.0f;
            const auto length = std::isnan (inputs[1]) ? storedLength : std::max (0.0f, inputs[1]);

            if (triggerFired)
                remainingSamples = (int) std::lround ((double) length * sampleRate);

            const auto gateOpen = remainingSamples > 0;
            if (remainingSamples > 0)
                --remainingSamples;

            outputs[0] = gateOpen ? 1.0f : 0.0f;
        }

    private:
        double sampleRate = 44100.0;
        float storedLength = defaultLengthSeconds;
        int remainingSamples = 0;
    };
}
