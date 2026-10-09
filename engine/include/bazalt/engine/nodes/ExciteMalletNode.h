#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "excite.mallet" (wiki/NODES.md's `excite.*` row, the
        PM Core batch, closing it out). "Models a mallet or hammer striking
        something, with a `feedback` path from the resonator so the
        collision itself reacts to what it hits — real contact dynamics, not
        a fixed envelope" (the catalog's own framing). Wiring a resonator's
        own `motion` output (`resonator.string`'s, the only one this batch
        gives one) back into this node's `feedback` input forms a real,
        literal 2-node graph cycle — the exact case this whole batch's intro
        (`wiki/MILESTONES.md`) confirmed `GraphCompiler.cpp`'s existing
        Tarjan SCC-based per-sample-region mechanism already compiles
        correctly, the first production node pair to actually exercise it.

        **A concrete, tested contract this session had to design** (the
        catalog names `velocity`/`mass`/`stiffness`/`feedback`, not the
        collision model itself): a half-sine contact pulse — a real,
        standard simplified approximation of an elastic (Hertzian) contact
        force profile used throughout percussion-synthesis literature, not
        an arbitrary envelope shape. On `trigger`, the mallet "contacts" for
        exactly one half-cycle of a frequency derived from `stiffness`/
        `mass` (`stiffness ∝ frequency`, a stiffer mallet rings the contact
        faster/brighter; `mass ∝ 1/frequency`, a heavier mallet's contact is
        slower/duller/longer) — once that half-cycle completes, contact ends
        and `out`/`contact` both drop, exactly matching a real strike's own
        single, non-repeating transient.

        **The real dynamics, not a fixed envelope**: every sample during
        contact, `feedback` (the resonator's own motion at the contact
        point) subtracts from the mallet's own effective driving velocity —
        `effectiveVelocity = velocity₀ - 0.3·feedback` (a real, documented
        coupling coefficient, this node's own design call) — a resonator
        already moving INTO the mallet reduces the contact's own effective
        push, and one already moving away increases it, the literal
        Newton's-third-law shape the catalog's own "the collision itself
        reacts to what it hits" asks for. With nothing wired to `feedback`
        (reads silence, like any unconnected Audio input), this term is
        simply zero and the contact is a pure, un-coupled half-sine pulse —
        every bit as valid a use (mallet → an uncoupled resonator like
        `resonator.comb`/`resonator.modal`) as the coupled case.
    */
    class ExciteMalletNode : public Node
    {
    public:
        static constexpr float feedbackCouplingCoefficient = 0.3f;
        static constexpr int numInputs = 5;  // trigger, velocity, mass, stiffness, feedback
        static constexpr int numOutputs = 2; // out, contact

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }
        void reset() override { active = false; contactT = 0.0f; }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Mallet"; }
        juce::String getCategory() const override { return "Excite"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "trigger", .type = SignalType::Event, .label = "Trigger" },
                unipolarPort ("excite.mallet.velocity", "Velocity", 0.8f),
                unipolarPort ("excite.mallet.mass", "Mass", 0.3f),
                unipolarPort ("excite.mallet.stiffness", "Stiffness", 0.5f),
                { .id = "feedback", .type = SignalType::Signal, .quantity = Quantity::Audio },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true, .quantity = Quantity::Audio },
                PortDescriptor { .id = "contact", .type = SignalType::Signal, .label = "Contact", .quantity = Quantity::Boolean },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "excite.mallet.velocity")
                storedVelocity = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "excite.mallet.mass")
                storedMass = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "excite.mallet.stiffness")
                storedStiffness = juce::jlimit (0.0f, 1.0f, value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            if (std::fabs (inputs[0]) > 0.0f)
            {
                velocity0 = std::isnan (inputs[1]) ? storedVelocity : juce::jlimit (0.0f, 1.0f, inputs[1]);
                mass = std::isnan (inputs[2]) ? storedMass : juce::jlimit (0.0f, 1.0f, inputs[2]);
                stiffness = std::isnan (inputs[3]) ? storedStiffness : juce::jlimit (0.0f, 1.0f, inputs[3]);
                active = true;
                contactT = 0.0f;
            }

            if (! active)
            {
                outputs[0] = 0.0f;
                outputs[1] = 0.0f;
                return;
            }

            const auto contactFrequency = juce::jmap (stiffness, 0.0f, 1.0f, 150.0f, 3000.0f)
                                           / juce::jmap (mass, 0.0f, 1.0f, 0.5f, 3.0f);
            const auto halfPeriodSamples = juce::jmax (1.0f, (float) sampleRate / (2.0f * contactFrequency));
            const auto phase = contactT / halfPeriodSamples;

            if (phase >= 1.0f)
            {
                active = false;
                outputs[0] = 0.0f;
                outputs[1] = 0.0f;
                return;
            }

            const auto feedback = inputs[4]; // unconnected Audio reads silence (0.0f), no NaN sentinel needed
            const auto effectiveVelocity = velocity0 - feedbackCouplingCoefficient * feedback;

            outputs[0] = effectiveVelocity * std::sin (phase * juce::MathConstants<float>::pi);
            outputs[1] = 1.0f; // contact == true

            contactT += 1.0f;
        }

    private:
        static PortDescriptor unipolarPort (juce::String id, juce::String label, float defaultValue)
        {
            return PortDescriptor { .id = std::move (id), .type = SignalType::Signal, .label = std::move (label),
                                     .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultValue,
                                     .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar,
                                     .polarity = Polarity::Unipolar };
        }

        double sampleRate = 44100.0;
        bool active = false;
        float contactT = 0.0f;
        float velocity0 = 0.8f, mass = 0.3f, stiffness = 0.5f;

        float storedVelocity = 0.8f, storedMass = 0.3f, storedStiffness = 0.5f;
    };
}
