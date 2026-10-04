#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "view.listen". One Audio input, no outputs — a
        legitimate dead-end the compiler schedules and runs like any other
        node (GraphCompiler doesn't prune unreachable-from-output nodes, so
        no special compiler support is needed for a sink). NODE_EDITOR.md
        §6.4/§6.6's actual audition behaviour (routing this point to
        monitoring output, temporary-Listen-on-Ctrl/Cmd-click) is real
        engine+UI work for whichever milestone builds that interaction
        (M12) — this node just needs to exist and be a valid, harmless
        connection target for M7's command bridge to create.
    */
    class ListenNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 0;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Listen"; }
        juce::String getCategory() const override { return "View"; }
        juce::String getIcon() const override { return "ear"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { perChannel ({ "in", SignalType::Audio }) };
        }

        void processSample (const float*, float*) noexcept override {}
    };
}
