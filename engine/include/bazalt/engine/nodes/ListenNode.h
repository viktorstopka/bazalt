#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "view.listen". One Audio input, no outputs — a
        legitimate dead-end the compiler schedules and runs like any other
        node (GraphCompiler doesn't prune unreachable-from-output nodes, so
        no special compiler support is needed for a sink). The audition
        itself is not this node's job: while one is wired,
        GraphEditController compiles with the graph's output redirected to
        whatever feeds it, and leaves it out of every saved patch
        (wiki/ROADMAP.md stage 0).
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
            return { perChannel ({ .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio }) };
        }

        void processSample (const float*, float*) noexcept override {}
    };
}
