#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "io.control" (NODE_CATALOG.md's `io.*` row): one
        MIDI controller as a Control signal — "the mono-domain half of MIDI,
        kept out of `Note` deliberately" (SIGNAL_TYPES.md §9). One Unipolar
        0..1 output, `value`.

        `source` picks what it follows: a CC by number (`cc`, used only for
        that source), the mod wheel (CC 1), channel pressure, pitch bend or
        the sustain pedal (CC 64). CCs and pressure are the raw 0..127 scaled
        to 0..1; pitch bend is -1..1 mapped onto 0..1 so its centre is 0.5,
        keeping the output honestly Unipolar as the catalog declares it.

        `smoothing` (default 20 ms) is an exponential lag that turns the
        stepped CC values into a click-free control signal; 0 follows the
        value exactly. It's the one-pole time constant, derived from the
        prepared sample rate (CLAUDE.md rule 6), and the very first value is
        adopted outright rather than sweeping up from 0.

        `channel` (Omni, 1-16) is schema-only, like `io.noteIn`'s: HostInputs
        holds one omni controller state (the latest value on any channel),
        exactly as M18 recorded it for `io.noteIn` — recorded so the
        descriptor round-trips honestly, not yet filtering.

        Domain: a mono source; see IoAudioInNode.h. Values update at MIDI
        event boundaries in the voice domain, and once per block in the
        global domain.
    */
    class IoControlNode : public Node
    {
    public:
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate; }

        void reset() override
        {
            smoothed = target;
            initialised = false;
        }

        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "MIDI Control"; }
        juce::String getCategory() const override { return "IO"; } // not "I/O" - "/" is the Add menu's category-nesting delimiter (09-29-AddMenu.3)

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "value", .type = SignalType::Signal, .label = "Value", .isPrimaryOutput = true,
                                       .minValue = 0.0f, .maxValue = 1.0f, .quantity = Quantity::Unipolar } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "io.control.source",
                                       .minValue = 0.0f,
                                       .maxValue = 4.0f,
                                       .defaultValue = 1.0f, // mod wheel: the most common first thing to map
                                       .displayName = "Source",
                                       .isInteger = true,
                                       .kind = ValueKind::Enum,
                                       .enumOptions = { { "cc", "CC" }, { "modWheel", "Mod Wheel" }, { "pressure", "Pressure" },
                                                         { "pitchBend", "Pitch Bend" }, { "sustain", "Sustain" } },
                                       .isStructural = true },
                ParameterDescriptor { .id = "io.control.cc",
                                       .minValue = 0.0f,
                                       .maxValue = 127.0f,
                                       .defaultValue = 74.0f,
                                       .displayName = "CC Number",
                                       .isInteger = true,
                                       .kind = ValueKind::Int,
                                       .step = 1.0f,
                                       .isStructural = true },
                ParameterDescriptor { .id = "io.control.channel",
                                       .minValue = 0.0f,
                                       .maxValue = 16.0f,
                                       .defaultValue = 0.0f,
                                       .displayName = "Channel",
                                       .isInteger = true,
                                       .kind = ValueKind::Int,
                                       .step = 1.0f,
                                       .isStructural = true },
                ValueTypes::timeMsParameter ("io.control.smoothing", "Smoothing", 20.0f, 500.0f),
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "io.control.source")
                source = (Source) juce::jlimit (0, 4, (int) std::lround (value));
            else if (parameterId == "io.control.cc")
                ccNumber = juce::jlimit (0, HostInputs::numControllers - 1, (int) std::lround (value));
            else if (parameterId == "io.control.smoothing")
                smoothingSeconds = juce::jmax (0.0f, value) * 0.001f;
            // "io.control.channel": schema-only, see class comment.
        }

        bool wantsHostInputs() const noexcept override { return true; }

        void setHostInputs (const HostInputs& inputs) noexcept override
        {
            switch (source)
            {
                case Source::ModWheel:  target = inputs.controllers[1]; break;
                case Source::Pressure:  target = inputs.channelPressure; break;
                case Source::PitchBend: target = (inputs.pitchBend + 1.0f) * 0.5f; break;
                case Source::Sustain:   target = inputs.controllers[64]; break;
                case Source::Cc:
                default:                target = inputs.controllers[(size_t) ccNumber]; break;
            }

            target = juce::jlimit (0.0f, 1.0f, target);

            if (! initialised)
            {
                smoothed = target; // first value is adopted, not swept up from 0
                initialised = true;
            }
        }

        void processSample (const float*, float* outputs) noexcept override
        {
            if (smoothingSeconds <= 0.0f)
            {
                smoothed = target;
            }
            else
            {
                if (smoothingSeconds != cachedSmoothing)
                {
                    cachedSmoothing = smoothingSeconds;
                    cachedCoefficient = (float) std::exp (-1.0 / ((double) smoothingSeconds * sampleRate));
                }
                smoothed = target + (smoothed - target) * cachedCoefficient;
            }

            outputs[0] = smoothed;
        }

    private:
        enum class Source
        {
            Cc,
            ModWheel,
            Pressure,
            PitchBend,
            Sustain
        };

        Source source = Source::ModWheel; // matches `io.control.source`'s declared default (1)
        int ccNumber = 74;                // matches `io.control.cc`'s declared default
        double sampleRate = 44100.0;
        float smoothingSeconds = 0.020f;  // matches `io.control.smoothing`'s declared default (20 ms)
        float target = 0.0f;
        float smoothed = 0.0f;
        bool initialised = false;
        float cachedSmoothing = -1.0f;
        float cachedCoefficient = 0.0f;
    };
}
