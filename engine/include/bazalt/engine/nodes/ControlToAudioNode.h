#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>

namespace bazalt::engine::nodes
{
    /** Stable type id: "adapt.controlToAudio" (wiki/plans/ControlToAudioBridge.md).
        The mechanical half of the reverse bridge — closes the "open symmetric
        question for later" `wiki/plans/AudioControlBridge.md` §6 explicitly
        deferred ("No reverse Control -> Audio bridge... not scoped here").

        Reads an ordinary Control signal and hands it out as an Audio signal,
        same mechanical/opinion-free spirit as `adapt.map`/`adapt.normalise`/
        `adapt.audioToControl` — never a creative DSP effect, never guesses a
        destination's own range (there isn't one: `Audio` ports carry no
        quantity/range metadata at all).

        `in` is polymorphic on quantity, same mechanism `MapNode.h` already
        uses (`hasPolymorphicPorts()`/`resolveIncomingPort()`) — resolves to
        whatever the connected source declares. A `canConnect` (`CanConnect.cpp`)
        `Control -> Audio` connection auto-inserts this alone when the source is
        already `Unipolar`/`Bipolar`; when the source is a real quantity (e.g.
        `Frequency`), this is step two of a 2-step chain, after `adapt.normalise`
        (seeded from the SOURCE's own range) — mirrors
        `adapt.audioToControl` -> `adapt.map`'s own two-step shape, just with
        the real-quantity step on the source side this time instead of the
        destination side.

        `out` is mono only (no `.channels` override needed — every other mono
        Audio output in this codebase, e.g. `OscillatorNode.h`'s `out`, leaves
        it at the default) — a Control source has no stereo concept to begin
        with, so there's nothing to spread; the existing mono -> stereo free
        broadcast handles a destination that actually wants stereo.
    */
    class ControlToAudioNode : public Node
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

        juce::String getTitle() const override { return "To Audio"; }
        juce::String getCategory() const override { return "Adapters"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in",
                                       .type = SignalType::Control,
                                       .quantity = resolvedQuantity,
                                       .polarity = resolvedQuantity == Quantity::Bipolar ? Polarity::Bipolar : Polarity::Unipolar,
                                       .polymorphism = PortPolymorphism::Quantity } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Audio, .isPrimaryOutput = true } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            // Bipolar (-1..1) passes straight through; Unipolar (0..1, the
            // default until something resolves it otherwise) expands to the
            // full audio swing via *2-1 - a 0..1 modulation source has no
            // natural "centre" in audio terms, so using its full excursion
            // rather than only the positive half is the more useful,
            // less-surprising default for what's always a fixed ±1 audio
            // destination (unlike adapt.map, whose destination range is
            // arbitrary and whose Unipolar case is deliberately left as-is).
            const auto normalised = resolvedQuantity == Quantity::Bipolar
                                         ? inputs[0]
                                         : inputs[0] * 2.0f - 1.0f;
            outputs[0] = std::clamp (normalised, -1.0f, 1.0f);
        }

    private:
        Quantity resolvedQuantity = Quantity::Unipolar;
    };
}
