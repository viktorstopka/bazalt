#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "mix.gain". Two inputs (Audio, Control), one Audio
        output = audio * gain. The "gated amp" in the M2 voice proof graph
        (ARCHITECTURE.md §3.4's example path) and Init Patch's own ampVCA.

        wiki/NODES_Gaps.md's `jargon-naming` + `modulation-only-port`
        findings (both confirmed, fixed together): the catalog
        (`wiki/NODES.md`) already calls this node "Gain" — only this file's
        `getTitle()` still said "VCA", a 1970s voltage-control term this
        software has no reason to use. And unlike its sibling `mix.sum`'s
        own `level.N` ports, "gain" declared no `hasFallbackWhenUnconnected`/
        `defaultValue` at all — leaving it unpatched multiplied by NaN
        instead of "just as loud as before". Both fixed the same way every
        other modulatable knob-port in this codebase already is: a real
        declared default (1.0, unity — matching `mix.sum.level.N`'s own
        identity value) plus the NaN-sentinel fallback in `processSample()`.

        Port id stays "gain" (CLAUDE.md rule 3 — never rename a shipped port
        id; this node is already wired into the real Init Patch's `ampVCA`).
        The parameter id used to store/restore that fallback value is the
        same string, "gain", matching the established convention (every
        other fallback-carrying port uses its own id as its parameter id).
    */
    class GainNode : public Node
    {
    public:
        static constexpr float defaultGain = 1.0f;
        static constexpr int numInputs = 2;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Gain"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ "audio", SignalType::Audio }),
                PortDescriptor { .id = "gain", .type = SignalType::Control, .label = "Gain",
                                  .minValue = 0.0f, .maxValue = 4.0f, .defaultValue = defaultGain,
                                  .isLogScale = true, .hasFallbackWhenUnconnected = true,
                                  .quantity = Quantity::Gain, .curve = Curve::Logarithmic },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { perChannel ({ "out", SignalType::Audio }) };
        }

        /** M20 step 8: a default Meter on the Gain node's own output — the plan's
            own example of an obvious real-node home for a preview kind. */
        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::Meter, .portId = "out" } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "gain")
                storedGain = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto gain = std::isnan (inputs[1]) ? storedGain : inputs[1];
            outputs[0] = inputs[0] * gain;
        }

    private:
        float storedGain = defaultGain;
    };
}
