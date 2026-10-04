#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** The canvas-only decorations (wiki/plans/Decorations.md): deco.header
        (big text), deco.comment (a paragraph), deco.box (a resizable,
        purely visual rectangle) and deco.image (a picture, a "decal").
        Registered with NodeFactory::registerDecoration, so GraphCompiler
        never instantiates them for a plan: no ports, no parameters, no
        processing, no cost. Everything they show lives in the node's
        properties ("text", "width", "height", "colour", "asset"), edited
        through graphSetProperty and saved with the patch like any node.
        This class only exists so the Add menu has a descriptor to show. */
    class DecorationNode : public Node
    {
    public:
        DecorationNode (juce::String titleToUse, juce::String iconToUse)
            : title (std::move (titleToUse)), icon (std::move (iconToUse)) {}

        juce::String getTitle() const override { return title; }
        juce::String getCategory() const override { return "Decorations"; }
        juce::String getIcon() const override { return icon; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Decoration; }
        void processSample (const float*, float*) noexcept override {}

    private:
        juce::String title, icon;
    };
}
