#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "clock.counter" (wiki/NODES.md's `clock.*` row, the
        Clock+Seq batch). The catalog's own framing: "the generic sequencer
        engine — with `note.select`, an arpeggiator; with `data.lookup`, a
        step sequencer." Advances an index on every incoming tick, per
        `mode`.

        **`length`/`step` are real ports here** (catalog puts them under
        `clock.counter`'s **In:**, unlike `seq.steps`' structural `length` —
        that's a real, deliberate difference between the two nodes, not an
        inconsistency to "fix"), so both can be live-modulated; no per-index
        array is stored (unlike `seq.steps`), so a live-changing `length`
        never risks an out-of-bounds read — every mode's arithmetic is
        plain-integer, `juce::jlimit`-guarded.

        **`random` mode's `wrapped`** output never fires — there's no
        meaningful "reached the end and came back to start" for an
        independent uniform draw each tick, so this node doesn't invent one.
        Every other mode fires `wrapped` exactly once per lap: `up`/`down`
        when crossing the length boundary, `pingPong` at each bounce off
        either end.

        **`seed`** (structural, matching `random.stepped`/`random.drift`'s
        convention) makes `random` mode reproducible — not named in the
        catalog's own port list for this node, same reasoning `clock.pulse`
        already gives for its own added `seed`.
    */
    class ClockCounterNode : public Node
    {
    public:
        static constexpr int numInputs = 4;  // tick, reset, length, step
        static constexpr int numOutputs = 3; // index, normalised, wrapped

        enum class Mode { Up, Down, PingPong, Random };

        void reset() override
        {
            position = 0;
            pingDirection = 1;
            random = juce::Random ((juce::int64) seed);
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Counter"; }
        juce::String getCategory() const override { return "Clock"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "tick", .type = SignalType::Event, .label = "Tick" },
                PortDescriptor { .id = "reset", .type = SignalType::Event, .label = "Reset" },
                PortDescriptor { .id = "clock.counter.length", .type = SignalType::Control, .label = "Length",
                                  .minValue = 1.0f, .maxValue = 1024.0f, .defaultValue = 8.0f, .isInteger = true,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Count, .step = 1.0f },
                PortDescriptor { .id = "clock.counter.step", .type = SignalType::Control, .label = "Step",
                                  .minValue = 1.0f, .maxValue = 64.0f, .defaultValue = 1.0f, .isInteger = true,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Count, .step = 1.0f },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "index", .type = SignalType::Control, .label = "Index", .isPrimaryOutput = true,
                                  .minValue = 0.0f, .isInteger = true, .quantity = Quantity::Count },
                PortDescriptor { .id = "normalised", .type = SignalType::Control, .label = "Normalised",
                                  .minValue = 0.0f, .maxValue = 1.0f, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "wrapped", .type = SignalType::Event, .label = "Wrapped" },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "clock.counter.mode",
                                       .minValue = 0.0f, .maxValue = 3.0f, .defaultValue = 0.0f,
                                       .displayName = "Mode", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "up", "Up" }, { "down", "Down" },
                                                         { "pingPong", "Ping-Pong" }, { "random", "Random" } },
                                       .isStructural = true },
                ParameterDescriptor { .id = "clock.counter.seed",
                                       .minValue = 0.0f, .maxValue = 999999.0f, .defaultValue = 1.0f,
                                       .displayName = "Seed", .isInteger = true, .quantity = Quantity::Count,
                                       .step = 1.0f, .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "clock.counter.length")
                storedLength = juce::jmax (1, (int) std::lround (value));
            else if (parameterId == "clock.counter.step")
                storedStep = juce::jmax (1, (int) std::lround (value));
            else if (parameterId == "clock.counter.mode")
                mode = (Mode) juce::jlimit (0, 3, (int) std::lround (value));
            else if (parameterId == "clock.counter.seed")
            {
                seed = (int) std::lround (value);
                random = juce::Random ((juce::int64) seed);
            }
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto tickIn = std::fabs (inputs[0]) > 0.0f;
            const auto resetIn = std::fabs (inputs[1]) > 0.0f;
            const auto length = std::isnan (inputs[2]) ? storedLength : juce::jmax (1, (int) std::lround (inputs[2]));
            const auto step = std::isnan (inputs[3]) ? storedStep : juce::jmax (1, (int) std::lround (inputs[3]));

            if (resetIn)
            {
                position = 0;
                pingDirection = 1;
            }

            auto wrapped = false;

            if (tickIn)
            {
                switch (mode)
                {
                    case Mode::Up:
                    {
                        auto next = position + step;
                        if (next >= length)
                        {
                            wrapped = true;
                            next = length > 0 ? next % length : 0;
                        }
                        position = next;
                        break;
                    }
                    case Mode::Down:
                    {
                        auto next = position - step;
                        if (next < 0)
                        {
                            wrapped = true;
                            next = length > 0 ? ((next % length) + length) % length : 0;
                        }
                        position = next;
                        break;
                    }
                    case Mode::PingPong:
                    {
                        auto next = position + pingDirection * step;
                        if (next >= length)
                        {
                            next = juce::jlimit (0, length - 1, 2 * (length - 1) - next);
                            pingDirection = -1;
                            wrapped = true;
                        }
                        else if (next < 0)
                        {
                            next = juce::jlimit (0, length - 1, -next);
                            pingDirection = 1;
                            wrapped = true;
                        }
                        position = next;
                        break;
                    }
                    case Mode::Random:
                        position = length > 0 ? random.nextInt (length) : 0;
                        // No meaningful "wrap" for an independent random draw - see class comment.
                        break;
                }
            }

            outputs[0] = (float) position;
            outputs[1] = length > 1 ? (float) position / (float) (length - 1) : 0.0f;
            outputs[2] = wrapped ? 1.0f : 0.0f;
        }

    private:
        int position = 0;
        int pingDirection = 1;
        int storedLength = 8;
        int storedStep = 1;
        Mode mode = Mode::Up;
        int seed = 1;
        juce::Random random { (juce::int64) 1 };
    };
}
