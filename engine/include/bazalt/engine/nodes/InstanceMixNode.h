#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cstring>

namespace bazalt::engine::nodes
{
    /** Stable type id: "instance.sum" (M17, renamed from "instance.mix" in
        wiki/plans/DomainRedesign.md Batch 1b — "Mix" was genuinely
        overloaded with the unrelated, real `mix.sum`/`math.add` node;
        "Sum" names what this literally does and pairs with the
        "reduce"/"collapse" language the rest of the redesign uses for the
        Poly->Scalar direction. C++ class name kept as `InstanceMixNode`
        deliberately — a pure rename of the registered type id and title,
        not a rewrite; renaming the class too would just be busywork with
        no behavioural difference). `DOMAINS.md`'s Voice Mix concept —
        supersedes the downstream half of "util.voiceSum"
        (`RECONCILIATION.md` 3.1/3.5). Mechanically identical to
        `VoiceSumNode`'s `setExternalBlock()` hand-off (same reasoning:
        the real per-instance sum is computed outside the compiled graph,
        by whoever drives every instance's own plan and this one global
        plan — `PluginProcessor` — and handed in immediately before this
        plan's own `process()` call, same thread, no synchronization
        needed) — the real addition is the `mode` parameter, which tells
        the *driver* whether to sum or average the per-instance
        contributions before handing them in (this node doesn't average
        anything itself; by the time a block reaches it, combining has
        already happened, exactly like `VoiceSumNode`).

        Under wiki/plans/DomainRedesign.md's Multiplicity model (Batch 1):
        this is one of exactly two nodes whose ports are fixed by
        declaration rather than resolved dynamically — its "in" port is
        REQUIRED to resolve Poly (MultiplicityResolver rejects a Scalar
        source as "nothing to reduce"), and at most one instance.sum is
        supported per ORIGIN, not per graph any more — two instance.sum
        nodes reducing two DIFFERENT origins is legitimate (the redesign's
        own multiple-independent-voice-regions feature). Silence detection
        (measuring the signal reaching this node and freeing an instance
        once it's been quiet long enough) is implemented generically in
        `VoiceManager`/`PluginProcessor` against whatever a voice's own
        designated output is — it does not require reading this node at
        all, so it works whether or not a real graph even has an
        `instance.sum` node in it (this node's own
        threshold/hold-time parameters are for UI/schema completeness and
        future per-graph configurability; the M17 detector uses its own
        sensible constants — see `VoiceManager.h`).
    */
    class InstanceMixNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Voice Sum"; }
        juce::String getCategory() const override { return "Domain"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            PortDescriptor port { "in", SignalType::Audio };
            port.channels = Channels::Inherited;
            return { port };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            PortDescriptor port { "out", SignalType::Audio };
            port.channels = stereo ? Channels::Stereo : Channels::Mono;
            return { port };
        }

        int getNumOutputChannels() const noexcept override { return stereo ? 2 : 1; }

        /** wiki/plans/StereoChannels.md: whether the voices this node sums are
            stereo. Not a user parameter (absent from getParameters()) — the
            driver (GraphEditController) sets it on the global graph's copy
            of this node from the compiled voice plans' own output width, so
            a per-voice stereo chain (a per-voice pan, a stereo resonator)
            reaches the global domain intact instead of losing its right
            channel. */
        static constexpr const char* channelsParameterId = "instance.sum.channels";

        bool supportsPerSample() const noexcept override { return false; } // domain seam, same as VoiceSumNode

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "instance.sum.mode",
                                            .minValue = 0.0f,
                                            .maxValue = 1.0f,
                                            .defaultValue = 0.0f,
                                            .displayName = "Mode",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "sum", "Sum" }, { "average", "Average" } },
                                            .isStructural = true },
                     ParameterDescriptor { .id = "instance.sum.silenceThresholdDb",
                                            .minValue = -120.0f,
                                            .maxValue = -20.0f,
                                            .defaultValue = -80.0f,
                                            .unit = "dB",
                                            .displayName = "Silence Threshold" },
                     ParameterDescriptor { .id = "instance.sum.silenceHoldTimeMs",
                                            .minValue = 0.0f,
                                            .maxValue = 5000.0f,
                                            .defaultValue = 200.0f,
                                            .unit = "ms",
                                            .displayName = "Silence Hold Time" } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "instance.sum.mode")
                mode = value < 0.5f ? Mode::Sum : Mode::Average;
            else if (parameterId == "instance.sum.silenceThresholdDb")
                silenceThresholdDb = value;
            else if (parameterId == "instance.sum.silenceHoldTimeMs")
                silenceHoldTimeMs = value;
            else if (parameterId == channelsParameterId)
                stereo = value > 1.5f;
        }

        enum class Mode
        {
            Sum,
            Average
        };

        Mode getMode() const noexcept { return mode; }
        float getSilenceThresholdDb() const noexcept { return silenceThresholdDb; }
        float getSilenceHoldTimeMs() const noexcept { return silenceHoldTimeMs; }

        /** Same contract as `VoiceSumNode::setExternalBlock()` — see class
            comment. `samples` must remain valid only for the duration of
            the immediately-following `processBlock()` call.
        */
        void setExternalBlock (const float* samples, int numSamples, const float* rightSamples = nullptr) noexcept
        {
            externalSamples = samples;
            externalRightSamples = rightSamples != nullptr ? rightSamples : samples;
            externalNumSamples = numSamples;
        }

        void processBlock (const float* const*, float* const* outputs, int numSamples) noexcept override
        {
            const auto valid = externalSamples != nullptr && numSamples == externalNumSamples;
            for (int channel = 0; channel < (stereo ? 2 : 1); ++channel)
            {
                const auto* source = channel == 0 ? externalSamples : externalRightSamples;
                if (valid)
                    std::memcpy (outputs[channel], source, (size_t) numSamples * sizeof (float));
                else
                    std::memset (outputs[channel], 0, (size_t) numSamples * sizeof (float));
            }
        }

        void processSample (const float*, float* outputs) noexcept override
        {
            outputs[0] = 0.0f; // never legally reached — supportsPerSample() is false
            if (stereo)
                outputs[1] = 0.0f;
        }

    private:
        Mode mode = Mode::Sum;
        float silenceThresholdDb = -80.0f;
        float silenceHoldTimeMs = 200.0f;
        bool stereo = false;
        const float* externalSamples = nullptr;
        const float* externalRightSamples = nullptr;
        int externalNumSamples = 0;
    };
}
