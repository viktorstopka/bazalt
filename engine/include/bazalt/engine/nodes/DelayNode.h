#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/nodes/InheritingPortsNode.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "time.delay". One Audio input, one Audio output,
        plus a Control input for the delay length (samples) — a plain
        circular-buffer delay line. Generic primitive; the Karplus-Strong
        proof graph's feedback loop closes through this (ARCHITECTURE.md
        §3.4) — the buffer itself is the pitch period.

        Buffer is sized to maxDelaySamples in prepare() (allocates — fine,
        that's compile-time, off the audio thread); the active delay length
        is clamped to that ceiling, changeable without reallocating.

        Delay length used to be a plain ParameterDescriptor (no cable could
        ever drive it). It's now a real Control-type input port instead
        (PortDescriptor::hasFallbackWhenUnconnected — see that field's own
        comment, and GraphCompiler.cpp's matching one, for the NaN-sentinel
        mechanism this relies on) so it can be modulated like any other
        signal — direct feedback: "modulating simple things such as delay
        time is the whole point of such [modular] software." setParameter()
        still exists and still drives `delaySamples` exactly as before;
        that's what processSample() falls back to whenever nothing's
        actually wired into the port, so every existing caller that sets
        this via setParameter() (compile-time graph parameters or a
        post-compile call on the live node — ProofGraphs.h, render-cli,
        ProofGraphRenderTests.cpp) keeps working completely unchanged.

        **It delays anything that flows** (2026-10-12): a signal, or an Event —
        "this trigger once more, 30 ms later" (a paw lifting off, a second
        contact). The input takes on what is wired to it. A signal is read
        between samples (linear), so a modulated delay glides instead of
        stepping; an Event is moved by whole samples, never smeared. Up to
        96000 samples (2 s at 48 kHz).
    */
    class DelayNode : public InheritingPortsNode
    {
    public:
        static constexpr int numInputs = 2;
        static constexpr int numOutputs = 1;
        static constexpr int defaultMaxDelaySamples = 96000;

        explicit DelayNode (int maxDelaySamplesIn = defaultMaxDelaySamples) noexcept
            : InheritingPortsNode (Quantity::Audio), maxDelaySamples (maxDelaySamplesIn) {}

        void prepare (const NodePrepareInfo&) override
        {
            buffer.assign ((size_t) maxDelaySamples + 1, 0.0f);
            writeIndex = 0;
        }

        void reset() override
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writeIndex = 0;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Delay"; }
        juce::String getCategory() const override { return "Time"; }

        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            if (toPortId == "in")
                offer (0, source, true);
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ .id = "in", .type = resolvedType, .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::SignalAndQuantity }),
                ValueTypes::timeSamplesPort ("time.delay.samples", "By", maxDelaySamples, 200.0f),
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { perChannel ({ .id = "out", .type = resolvedType, .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::SignalAndQuantity }) };
        }

        std::vector<ParameterDescriptor> getParameters() const override { return {}; }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "time.delay.samples")
                delaySamples = juce::jlimit (1.0f, (float) maxDelaySamples, value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            // NaN means the port is unconnected (GraphCompiler.cpp's
            // hasFallbackWhenUnconnected sentinel) — fall back to whatever
            // setParameter() last set; a real connection overrides it,
            // re-clamped per sample since a live modulator isn't bound by
            // setParameter()'s own clamp.
            const auto modulation = inputs[1];
            auto active = std::isnan (modulation) ? delaySamples : juce::jlimit (1.0f, (float) maxDelaySamples, modulation);
            if (resolvedType == SignalType::Event)
                active = std::round (active); // a moment is moved, never smeared

            const auto size = (int) buffer.size();
            const auto whole = (int) active;
            const auto fraction = active - (float) whole;
            auto readIndex = writeIndex - whole;
            if (readIndex < 0)
                readIndex += size;
            auto older = readIndex - 1;
            if (older < 0)
                older += size;

            const auto a = buffer[(size_t) readIndex];
            outputs[0] = fraction > 0.0f ? a + (buffer[(size_t) older] - a) * fraction : a;

            buffer[(size_t) writeIndex] = inputs[0];
            writeIndex = (writeIndex + 1) % size;
        }

    private:
        int maxDelaySamples;
        float delaySamples = 200.0f;
        std::vector<float> buffer;
        int writeIndex = 0;
    };
}
