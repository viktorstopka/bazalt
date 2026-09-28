#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "io.output". One Audio input, one Audio output,
        unity pass-through — the "Master Out" node in the design reference.
        Deliberately just a conventional, visually-meaningful anchor point
        in the graph, not a compiler special case: `NodeGraph::setOutput()`
        designates this node's own primary output port exactly as it would
        any other node's, so the existing final-output mechanism needs no
        changes to support it.

        The output port is real (the compiler genuinely needs it) but marked
        `.hidden = true` — a terminal "Master Out" node showing its own
        further output as something you could drag a NEW cable from reads as
        a real UX bug, not a quirk to leave alone (caught live: "the master
        out still has an output"). See `PortDescriptor::hidden`'s own
        comment for the full reasoning on why this is a UI-only hint, not an
        engine restriction.

        Real stereo cable redesign (`wiki/NODES.System.md` §9): `"in"`/
        `"out"` keep their exact shipped ids (CLAUDE.md rule 3 — this is the
        node behind the graph's real, already-saved Master Out) and are now
        real `Channels::Stereo` ports — one cable each, not a left/right
        port pair. This supersedes Milestone 0.2's own point-fix, which
        briefly gave this node a second `"right"`/`"outRight"` port pair
        instead (CLAUDE.md rule 3 is itself suspended for now, on the user's
        own instruction — see CLAUDE.md rule 3's own note — so reverting
        those two added-then-removed ids isn't a rule violation). A graph
        that only ever wires a mono source into `"in"` behaves exactly as
        before (mono->stereo is a free broadcast, `CanConnect.cpp`), so
        nothing pre-existing changes behaviour.

        `getNumInputChannels()`/`getNumOutputChannels()` must be overridden
        (see `Node.h`'s own comment) since the default would otherwise
        report 1 (one descriptor each side), not the real 2 flat channels
        `processBlock`'s scratch loop needs to actually reach the right
        channel.
    */
    class OutputNode : public Node
    {
    public:
        static constexpr int numInputs = 1;  // in (Stereo)
        static constexpr int numOutputs = 1; // out (Stereo)

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }
        int getNumInputChannels() const noexcept override { return 2; }
        int getNumOutputChannels() const noexcept override { return 2; }

        juce::String getTitle() const override { return "Master Out"; }
        juce::String getCategory() const override { return "Utility"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "in", .type = SignalType::Audio, .label = "In", .channels = Channels::Stereo },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = SignalType::Audio, .label = "Out", .isPrimaryOutput = true, .hidden = true, .channels = Channels::Stereo },
            };
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            outputs[0] = inputs[0];
            outputs[1] = inputs[1];
        }
    };
}
