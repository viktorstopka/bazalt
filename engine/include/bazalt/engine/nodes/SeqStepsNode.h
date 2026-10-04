#pragma once

#include "bazalt/engine/graph/Node.h"
#include <array>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "seq.steps" (wiki/NODES.md's `seq.*` row, the
        Clock+Seq batch). A classic step sequencer: holds a bank of
        hand-editable step values, advances through them on `tick`.

        **Two deliberate, documented deviations from the catalog spec**
        (wiki/NODES.Status.md's own note on this node, written before it was
        built): the catalog's `steps` input (`Data(curve)`, optional) isn't
        built — no node in the engine produces a real `Data` value yet (see
        wiki/NODES.Status.md's cross-cutting prerequisite note), so this
        node's step data is *only* the node's own editable bank, exactly the
        fallback the catalog itself names ("otherwise the node's own
        editable step data"). And `length` is capped at 16, not the
        catalog's 1-64 — a classic 16-step sequencer covers the overwhelming
        common case, and raising the cap later is trivial (these are plain,
        individually-numbered `ParameterDescriptor`s, `seq.steps.step.0`
        .. `.15`, not a structural array size baked into the wire format).
        Real per-step editing belongs on `NodeContent` (`wiki/
        NODES.System.md` §3) once that exists; this is the interim shape.

        **`gate`'s convention**: with no separate per-step "enabled" flag in
        this data model, a step storing exactly 0.0 is a rest — `gate` is
        `abs(stepValue) > epsilon`. This is a real, deliberate design choice
        for this MVP, not an oversight; genuinely wanting silence at 0.0
        AND an active step there too would need the separate flag the
        catalog doesn't specify a shape for.

        **Convention (matches `seq.euclid`)**: the sequencer holds at step 0
        until the first tick, which advances it to step 1 - the same
        "power-on shows step 1 active, first clock advances to step 2"
        behaviour classic hardware step sequencers use, not an off-by-one
        bug.
    */
    class SeqStepsNode : public Node
    {
    public:
        static constexpr int maxSteps = 16;
        static constexpr int numInputs = 2;  // tick, reset
        static constexpr int numOutputs = 4; // value, gate, trigger, index

        void reset() override { currentIndex = 0; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Step Sequencer"; }
        juce::String getCategory() const override { return "Sequencing"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "tick", .type = SignalType::Event, .label = "Tick" },
                PortDescriptor { .id = "reset", .type = SignalType::Event, .label = "Reset" },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "value", .type = SignalType::Control, .label = "Value", .isPrimaryOutput = true,
                                  .minValue = -1.0f, .maxValue = 1.0f, .quantity = Quantity::Bipolar },
                PortDescriptor { .id = "gate", .type = SignalType::Boolean, .label = "Gate", .kind = ValueKind::Bool },
                PortDescriptor { .id = "trigger", .type = SignalType::Event, .label = "Trigger" },
                PortDescriptor { .id = "index", .type = SignalType::Control, .label = "Index",
                                  .minValue = 0.0f, .maxValue = (float) (maxSteps - 1), .isInteger = true,
                                  .quantity = Quantity::Count },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            std::vector<ParameterDescriptor> parameters = {
                ParameterDescriptor { .id = "seq.steps.length",
                                       .minValue = 1.0f, .maxValue = (float) maxSteps, .defaultValue = 8.0f,
                                       .displayName = "Length", .isInteger = true, .quantity = Quantity::Count,
                                       .step = 1.0f, .isStructural = true },
            };

            for (int i = 0; i < maxSteps; ++i)
            {
                parameters.push_back (ParameterDescriptor {
                    .id = "seq.steps.step." + juce::String (i),
                    .minValue = -1.0f,
                    .maxValue = 1.0f,
                    .defaultValue = 0.0f,
                    .displayName = "Step " + juce::String (i + 1),
                    .quantity = Quantity::Bipolar,
                    .polarity = Polarity::Bipolar,
                    .isStructural = true });
            }

            return parameters;
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "seq.steps.length")
                storedLength = juce::jlimit (1, maxSteps, (int) std::lround (value));
            else if (parameterId.startsWith ("seq.steps.step."))
            {
                const auto index = parameterId.fromLastOccurrenceOf (".", false, false).getIntValue();
                if (index >= 0 && index < maxSteps)
                    stepValues[(size_t) index] = juce::jlimit (-1.0f, 1.0f, value);
            }
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto tickIn = std::fabs (inputs[0]) > 0.0f;
            const auto resetIn = std::fabs (inputs[1]) > 0.0f;

            if (resetIn)
                currentIndex = 0;

            auto triggered = false;

            if (tickIn)
            {
                currentIndex = (currentIndex + 1) % storedLength;
                triggered = true;
            }

            // wiki/plans/PropsAndMacroRedesign.md Batch D: the old
            // Unipolar/Bipolar "Range" selector was a redundant 2-line
            // remap the generic adapt.map/util.bipolarToUnipolar already
            // cover — step values are always bipolar now, matching how
            // they're always hand-edited (-1..1).
            const auto raw = stepValues[(size_t) juce::jlimit (0, maxSteps - 1, currentIndex)];

            outputs[0] = raw;
            outputs[1] = std::fabs (raw) > 1.0e-6f ? 1.0f : 0.0f;
            outputs[2] = triggered ? 1.0f : 0.0f;
            outputs[3] = (float) currentIndex;
        }

    private:
        int currentIndex = 0;
        int storedLength = 8;
        std::array<float, maxSteps> stepValues {};
    };
}
