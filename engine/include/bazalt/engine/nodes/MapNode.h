#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>

namespace bazalt::engine::nodes
{
    /** Stable type id: "adapt.map". Remaps a normalised 0..1 Modulation
        input onto a real-unit Value output — what connecting Modulation to
        Value auto-inserts (NODE_EDITOR.md §5), seeded from the target
        port's own min/max metadata by `GraphEditController::
        connectWithAutoAdapt` at insertion time (compare `NormaliseNode.h`'s
        sibling comment, which correctly attributes this to
        `CanConnect.h`/`AdapterStep` — this node just implements the remap
        once min/max are set).

        "in" is declared `Quantity::Unipolar` by default (docs/CLEANUP.md
        Priority 1 #4) to match what `NormaliseNode`'s own output declares —
        without it, `canConnect`'s "same-or-Dimensionless quantity is Ok"
        rule let a raw Frequency/Pitch value feed this port directly with no
        adapter inserted at all, silently clamped as if it were already
        0-1. `in` is polymorphic on quantity (`PortPolymorphism::Quantity`,
        same mechanism `adapt.sampleHold` already uses), Unipolar only until
        something real resolves it: a genuinely `Bipolar` source (e.g.
        `random.stepped`/`random.drift`, or `adapt.audioToControl` -
        wiki/plans/AudioControlBridge.md, the connection that first caught
        this) adopts `Bipolar` instead, and `processSample()` rescales
        accordingly. Before this, `CanConnect.cpp`'s own "Unipolar/Bipolar ->
        real quantity via Map" rule (M16) was silently dead code for the
        Bipolar half specifically: the auto-inserted `adapt.map` node's own
        "in" port rejected a Bipolar source outright at
        `GraphCompiler`'s real re-validation, a real, latent, previously
        never-hit gap - nothing ever actually wired a Bipolar source
        through this exact path end to end until AudioControlBridge did.
    */
    class MapNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        bool hasPolymorphicPorts() const noexcept override { return true; }

        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            if (toPortId == "in" && source.type == SignalType::Control)
                resolvedQuantity = source.quantity;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Map"; }
        juce::String getCategory() const override { return "Adapters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in",
                                       .type = SignalType::Control,
                                       .minValue = 0.0f,
                                       .maxValue = 1.0f,
                                       .quantity = resolvedQuantity,
                                       .polarity = resolvedQuantity == Quantity::Bipolar ? Polarity::Bipolar : Polarity::Unipolar,
                                       .polymorphism = PortPolymorphism::Quantity } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .isPrimaryOutput = true } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { { "adapt.map.min", -100000.0f, 100000.0f, 0.0f, 1.0f, "", "Min" },
                     { "adapt.map.max", -100000.0f, 100000.0f, 1.0f, 1.0f, "", "Max" } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "adapt.map.min")
                minValue = value;
            else if (parameterId == "adapt.map.max")
                maxValue = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            // Bipolar (-1..1) rescales into 0..1 first; Unipolar (already
            // 0..1, the default until something resolves it otherwise) is
            // used as-is, exactly as before this node became polymorphic.
            const auto normalized = resolvedQuantity == Quantity::Bipolar
                                         ? std::clamp ((inputs[0] + 1.0f) * 0.5f, 0.0f, 1.0f)
                                         : std::clamp (inputs[0], 0.0f, 1.0f);
            outputs[0] = minValue + normalized * (maxValue - minValue);
        }

    private:
        float minValue = 0.0f;
        float maxValue = 1.0f;
        Quantity resolvedQuantity = Quantity::Unipolar;
    };
}
