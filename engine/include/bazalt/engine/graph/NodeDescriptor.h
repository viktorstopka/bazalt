#pragma once

#include "bazalt/engine/graph/Node.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace bazalt::engine
{
    /** Everything the UI needs to render an Add-menu entry and build a
        node's body, sourced entirely from a Node's metadata methods —
        never its DSP implementation (NODE_EDITOR.md §3). Mock (UI-only)
        descriptors for demonstration nodes that don't exist in the engine
        use this identical shape, just built by hand in TS instead of via
        NodeFactory::describeAll() below — one schema, two producers.
    */
    struct NodeDescriptor
    {
        juce::String typeId;
        juce::String title;
        juce::String category;
        juce::String icon;
        NodeLayoutVariant layoutVariant = NodeLayoutVariant::Standard;
        std::vector<PortDescriptor> inputs;
        std::vector<PortDescriptor> outputs;
        std::vector<ParameterDescriptor> parameters;
    };

    /** Builds a NodeDescriptor from a live Node instance's metadata calls
        — message-thread only (constructs a throwaway Node to introspect),
        same cost class as one GraphCompiler::compile() node instantiation,
        never called from the audio thread.
    */
    inline NodeDescriptor describeNode (const juce::String& typeId, const Node& node)
    {
        NodeDescriptor descriptor;
        descriptor.typeId = typeId;
        descriptor.title = node.getTitle().isNotEmpty() ? node.getTitle() : typeId;
        descriptor.category = node.getCategory();
        descriptor.icon = node.getIcon();
        descriptor.layoutVariant = node.getLayoutVariant();
        descriptor.inputs = node.getInputPorts();
        descriptor.outputs = node.getOutputPorts();
        descriptor.parameters = node.getParameters();
        return descriptor;
    }
}
