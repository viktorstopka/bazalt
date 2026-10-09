#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "math.pitchToFrequency". Direct feedback: `canConnect`
        was auto-inserting `math.map` — a plain LINEAR interpolation
        between two ranges — for a `Pitch -> Frequency` connection (e.g.
        pitch-tracking a filter's cutoff, `CanConnect.cpp`'s own long-standing
        example). Pitch-to-Hz is exponential (each semitone is ×2^(1/12)), so
        that auto-adapter was quietly producing the wrong frequency for
        anything but the exact two endpoints a linear seed happens to land
        on — a real, previously undiscovered correctness gap in a documented
        "MVP, linear-only for now" limitation
        (`archive_docs/decisions/0019-adapter-table.md`'s M20 amendment),
        not a new bug. This node is the actual, mathematically correct
        conversion; `CanConnect.cpp`'s own `connectControl()` now prefers it
        over the generic remap specifically for this one quantity pair.

        Standard equal-tempered reference: A4 (pitch 69) = 440Hz.
    */
    class PitchToFrequencyNode : public Node
    {
    public:
        static constexpr float referencePitch = 69.0f;   // A4
        static constexpr float referenceHz = 440.0f;
        static constexpr float defaultPitch = 60.0f;      // middle C - this codebase's usual anchor default
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Pitch to Frequency"; }
        juce::String getCategory() const override { return "Math"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "pitch", .type = SignalType::Signal, .label = "Pitch",
                                       .unit = "st", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = defaultPitch,
                                       .hasFallbackWhenUnconnected = true, .quantity = Quantity::Pitch } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "frequency", .type = SignalType::Signal, .label = "Frequency",
                                       .isPrimaryOutput = true, .unit = "Hz", .quantity = Quantity::Frequency,
                                       .curve = Curve::Logarithmic } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "pitch")
                storedPitch = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto pitch = std::isnan (inputs[0]) ? storedPitch : inputs[0];
            outputs[0] = referenceHz * std::pow (2.0f, (pitch - referencePitch) / 12.0f);
        }

    private:
        float storedPitch = defaultPitch;
    };

    /** Stable type id: "math.frequencyToPitch" — the inverse of
        `PitchToFrequencyNode` above; see that class's own doc comment for
        why this exists (the linear-remap correctness gap it closes).
        Frequencies at or below zero have no defined pitch — clamped to a
        small positive epsilon rather than producing -inf/NaN, so a
        momentary zero-crossing glitch upstream can't poison this node's
        output.
    */
    class FrequencyToPitchNode : public Node
    {
    public:
        static constexpr float referencePitch = 69.0f;
        static constexpr float referenceHz = 440.0f;
        static constexpr float defaultFrequency = referenceHz;
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Frequency to Pitch"; }
        juce::String getCategory() const override { return "Math"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "frequency", .type = SignalType::Signal, .label = "Frequency",
                                       .unit = "Hz", .minValue = 0.01f, .maxValue = 20000.0f, .defaultValue = defaultFrequency,
                                       .isLogScale = true, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency,
                                       .curve = Curve::Logarithmic } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "pitch", .type = SignalType::Signal, .label = "Pitch",
                                       .isPrimaryOutput = true, .unit = "st", .quantity = Quantity::Pitch } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "frequency")
                storedFrequency = std::max (0.000001f, value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto frequency = std::max (0.000001f, std::isnan (inputs[0]) ? storedFrequency : inputs[0]);
            outputs[0] = referencePitch + 12.0f * std::log2 (frequency / referenceHz);
        }

    private:
        float storedFrequency = defaultFrequency;
    };
}
