#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "excite.burst". One Audio output: a decaying burst
        of white noise on trigger, silence otherwise. The Karplus-Strong
        proof graph's "pluck" excitation (ARCHITECTURE.md §3.4) — fed into
        the feedback loop's Mix node from outside the per-sample region.

        wiki/NODES_Gaps.md's `hardcoded-trigger` finding (confirmed): this
        node used to have ZERO input ports — the only way to start a burst
        was a direct C++ poke (`trigger(int)`) from whoever compiled it into
        a graph, with no Event-typed port a patch could ever wire into. Now
        has a real `trigger : Event` input (the catalog's own spec), so a
        clock, a threshold detector, or anything else that produces Events
        can start it. `trigger(int)` stays as a direct poke for
        tests/tools/ProofGraphs.h that still want exact sample-accurate
        control (render-cli's Karplus-Strong path pokes it this way, and
        still can) — a real `trigger` Event connection calls the SAME
        internal start logic from processSample() below, never a second
        implementation.

        `duration` is a real, modulatable port (catalog: 0.1–2000ms, default
        30ms) rather than only the poke's own explicit argument — sampled
        once at the moment `trigger` fires (a burst's length not changing
        mid-flight matches every other one-shot excitation node in this
        catalog). `tone`/`shape` (the catalog's remaining two ports) aren't
        built yet — this node is still 🚧 partial catalog compliance, not
        the full spec, tracked in wiki/NODES.md.
    */
    class NoiseBurstNode : public Node
    {
    public:
        static constexpr float defaultDurationMs = 30.0f;
        static constexpr int numInputs = 2; // trigger, duration
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }

        void reset() override { remainingSamples = 0; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Noise Burst"; }
        juce::String getCategory() const override { return "Excite"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "trigger", .type = SignalType::Event, .label = "Trigger" },
                ValueTypes::timeMsPort ("excite.burst.duration", "Duration", defaultDurationMs, 2000.0f),
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { "out", SignalType::Audio } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "excite.burst.duration")
                storedDurationMs = juce::jmax (0.0f, value);
        }

        /** Starts a burst lasting durationSamples, amplitude decaying
            linearly to zero over that span. Direct C++ poke — see class
            comment; a real `trigger` Event connection calls this
            internally via processSample() below, never a second
            implementation.
        */
        void trigger (int durationSamples) noexcept
        {
            remainingSamples = durationSamples;
            totalSamples = juce::jmax (1, durationSamples);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            // "non-zero this sample = fired" (LogicToggleNode.h's
            // documented Event convention, reused verbatim) — an
            // unconnected trigger input reads a plain 0, never firing.
            if (std::fabs (inputs[0]) > 0.0f)
            {
                const auto durationMs = std::isnan (inputs[1]) ? storedDurationMs : juce::jmax (0.0f, inputs[1]);
                trigger ((int) std::lround ((double) durationMs * 0.001 * sampleRate));
            }

            if (remainingSamples <= 0)
            {
                outputs[0] = 0.0f;
                return;
            }

            const auto envelope = (float) remainingSamples / (float) totalSamples;
            outputs[0] = (random.nextFloat() * 2.0f - 1.0f) * envelope;
            --remainingSamples;
        }

    private:
        juce::Random random;
        double sampleRate = 44100.0;
        float storedDurationMs = defaultDurationMs;
        int remainingSamples = 0;
        int totalSamples = 1;
    };
}
