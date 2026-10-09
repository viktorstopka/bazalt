#pragma once

#include "bazalt/engine/nodes/GrowableGroupNode.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** The Boolean/Event glue around the gates (2026-10-04, direct
        instruction: "Event Group ... and other logic necessary with logic work
        around bool and triggers"). Events follow the codebase's convention:
        "non-zero this sample = fired", the value being the event's strength
        (LogicToggleNode.h). Booleans are 0/1, true is `> 0.5`.

        Together with the existing nodes this closes the set:
          Bool  -> Bool   logic.not, logic.and/or/xor (+ Invert)
          Event -> Event  logic.eventGroup (merge), clock.divide (every Nth)
          Bool  -> Event  logic.edge
          Event -> Bool   logic.toggle (flip), logic.latch (set/reset),
                          adapt.gateLength (true for a time)
          Bool  -> any    logic.select (also gates an Event: whenFalse unwired)
    */

    /** Stable type id: "logic.eventGroup" — Event Group. Growable Event inputs
        `in.0…in.N` (2..16, PortGroups.h, the same mechanism as math.add);
        fires whenever ANY input fires. Several firing on the same sample
        merge into one event carrying the strongest value — an event stream
        has no way to say "two at once". */
    class LogicEventGroupNode : public GrowableGroupNode
    {
    public:
        static constexpr int minInputs = 2;
        static constexpr int maxInputs = 16;

        LogicEventGroupNode() noexcept : GrowableGroupNode (minInputs, maxInputs) {}

        int getNumInputPorts() const noexcept override { return groupCount; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return "Event Group"; }
        juce::String getCategory() const override { return "Logic"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            std::vector<PortDescriptor> ports;
            ports.reserve ((size_t) groupCount);
            const auto group = groupFor ("in.");
            for (int i = 0; i < groupCount; ++i)
                ports.push_back (PortDescriptor { .id = "in." + juce::String (i), .type = SignalType::Event,
                                                  .label = "In " + juce::String (i + 1), .group = group });
            return ports;
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Event, .label = "Out", .isPrimaryOutput = true } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            auto strongest = 0.0f;
            for (int i = 0; i < groupCount; ++i)
                if (! std::isnan (inputs[i]) && std::fabs (inputs[i]) > std::fabs (strongest))
                    strongest = inputs[i];
            outputs[0] = strongest;
        }
    };

    /** Stable type id: "logic.edge" — Edge. Boolean in, Event out: fires on
        the sample the input turns true (Rising), turns false (Falling), or
        either (Both). The bridge from a state to a moment — "when the gate
        opens", "when the comparison stops holding". */
    class LogicEdgeNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return "Edge"; }
        juce::String getCategory() const override { return "Logic"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Signal, .label = "In", .kind = ValueKind::Bool, .quantity = Quantity::Boolean } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Event, .label = "Out", .isPrimaryOutput = true } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "logic.edge.mode", .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 0.0f,
                                           .displayName = "Edge", .isInteger = true, .kind = ValueKind::Enum,
                                           .enumOptions = { { "rising", "Rising" }, { "falling", "Falling" }, { "both", "Both" } } } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "logic.edge.mode")
                mode = juce::jlimit (0, 2, (int) std::lround (value));
        }

        void reset() override { wasTrue = false; }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto isTrue = inputs[0] > 0.5f;
            const auto rose = isTrue && ! wasTrue;
            const auto fell = ! isTrue && wasTrue;
            wasTrue = isTrue;
            outputs[0] = ((mode != 1 && rose) || (mode != 0 && fell)) ? 1.0f : 0.0f;
        }

    private:
        int mode = 0; // 0 Rising, 1 Falling, 2 Both
        bool wasTrue = false;
    };

    /** Stable type id: "logic.latch" — Latch (a set/reset flip-flop). Event
        `set` makes the output true and keeps it there; Event `reset` makes it
        false. Unlike logic.toggle (each event flips), repeated sets are
        harmless — "arm on this, disarm on that". Reset wins when both fire on
        the same sample (the safe default: an explicit stop is never lost). */
    class LogicLatchNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 2; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return "Latch"; }
        juce::String getCategory() const override { return "Logic"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "set", .type = SignalType::Event, .label = "Set" },
                     PortDescriptor { .id = "reset", .type = SignalType::Event, .label = "Reset" } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true, .kind = ValueKind::Bool, .quantity = Quantity::Boolean } };
        }

        void reset() override { state = false; }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            if (std::fabs (inputs[0]) > 0.0f)
                state = true;
            if (std::fabs (inputs[1]) > 0.0f)
                state = false;
            outputs[0] = state ? 1.0f : 0.0f;
        }

    private:
        bool state = false;
    };
}
