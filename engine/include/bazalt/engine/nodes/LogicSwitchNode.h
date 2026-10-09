#pragma once

#include "bazalt/engine/nodes/GrowableGroupNode.h"
#include <array>
#include <cmath>
#include <limits>

namespace bazalt::engine::nodes
{
    /** Stable type id: "logic.switch" — Switch (wiki/plans/DataAndWavetable.md
        D8): pick one of N inputs, mainly for A/B comparison — two filters, two
        reverbs, three versions of a chain, heard one at a time. Growable
        inputs `in.0…in.N` (2..16, PortGroups.h); `logic.switch.active` picks
        one by index (an editable value, so the node itself is the A/B
        button), and every **Next** event steps to the following input,
        wrapping. A change crossfades over ~10 ms so it never clicks. `index`
        reports which input is playing.

        The inputs share one type and quantity, taken from the lowest-numbered
        wired one (the same rule as math.add); stereo runs per channel.
    */
    class LogicSwitchNode : public GrowableGroupNode
    {
    public:
        static constexpr int minInputs = 2;
        static constexpr int maxInputs = 16;
        static constexpr float fadeSeconds = 0.010f;

        LogicSwitchNode() noexcept : GrowableGroupNode (minInputs, maxInputs) {}

        void prepare (const NodePrepareInfo& info) override
        {
            fadeStep = 1.0f / juce::jmax (1.0f, fadeSeconds * (float) info.sampleRate);
            reset();
        }

        void reset() override
        {
            current = previous = clampIndex (storedActive);
            fade = 1.0f;
        }

        int getNumInputPorts() const noexcept override { return groupCount + 1; }
        int getNumOutputPorts() const noexcept override { return 2; }

        juce::String getTitle() const override { return "Switch"; }
        juce::String getCategory() const override { return "Logic"; }

        bool hasPolymorphicPorts() const noexcept override { return true; }

        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            const auto isPlainValue = source.type == SignalType::Audio || source.type == SignalType::Control
                                       || source.type == SignalType::Boolean;
            const auto priority = parsePortGroupIndex (toPortId, "in.");
            if (! isPlainValue || priority < 0 || priority > bestPriority)
                return;
            bestPriority = priority;
            resolvedType = source.type;
            resolvedQuantity = source.quantity;
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            std::vector<PortDescriptor> ports;
            ports.reserve ((size_t) groupCount + 1);
            const auto group = groupFor ("in.");
            for (int i = 0; i < groupCount; ++i)
                ports.push_back (PortDescriptor { .id = "in." + juce::String (i), .type = resolvedType,
                                                  .label = juce::String::charToString ((juce::juce_wchar) ('A' + i)),
                                                  .quantity = resolvedQuantity, .group = group, .channels = Channels::Inherited,
                                                  .polymorphism = PortPolymorphism::SignalAndQuantity });
            ports.push_back (PortDescriptor { .id = "next", .type = SignalType::Event, .label = "Next" });
            return ports;
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = resolvedType, .label = "Out", .isPrimaryOutput = true,
                                 .quantity = resolvedQuantity, .channels = Channels::Inherited,
                                 .polymorphism = PortPolymorphism::SignalAndQuantity },
                PortDescriptor { .id = "index", .type = SignalType::Control, .label = "Index",
                                 .minValue = 0.0f, .maxValue = (float) (maxInputs - 1), .kind = ValueKind::Int,
                                 .quantity = Quantity::Count },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "logic.switch.active", .minValue = 0.0f, .maxValue = (float) (maxInputs - 1),
                                           .defaultValue = 0.0f, .displayName = "Active", .isInteger = true,
                                           .quantity = Quantity::Count } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId != "logic.switch.active")
                return;
            storedActive = (int) std::lround (value);
            select (clampIndex (storedActive));
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto next = inputs[groupCount];
            if (! std::isnan (next) && next != 0.0f)
                select ((current + 1) % groupCount);

            const auto read = [&] (int i) { return std::isnan (inputs[i]) ? 0.0f : inputs[i]; };
            if (fade < 1.0f)
            {
                fade = juce::jmin (1.0f, fade + fadeStep);
                outputs[0] = read (previous) * (1.0f - fade) + read (current) * fade;
            }
            else
            {
                outputs[0] = read (current);
            }
            outputs[1] = (float) current;
        }

    private:
        int clampIndex (int index) const noexcept { return juce::jlimit (0, groupCount - 1, index); }

        void select (int index) noexcept
        {
            if (index == current)
                return;
            // A switch mid-fade starts the new fade from what is playing now.
            previous = fade < 0.5f ? previous : current;
            current = index;
            fade = 0.0f;
        }

        SignalType resolvedType = SignalType::Audio;
        Quantity resolvedQuantity = Quantity::Dimensionless;
        int bestPriority = std::numeric_limits<int>::max();
        int storedActive = 0;
        int current = 0, previous = 0;
        float fade = 1.0f, fadeStep = 1.0f;
    };
}
