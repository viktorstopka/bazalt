// wiki/plans/DataAndWavetable.md §2: nodes removed or merged by the stage 1
// sweep load from older patches as their replacements.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/CurveData.h"
#include "bazalt/engine/patch/PatchSerializer.h"
#include <algorithm>

using namespace bazalt::engine;

namespace
{
    const NodeInstance* findNode (const PatchDocument& doc, const juce::String& id)
    {
        const auto it = std::find_if (doc.nodes.begin(), doc.nodes.end(), [&] (const auto& n) { return n.id == id; });
        return it == doc.nodes.end() ? nullptr : &*it;
    }

    bool hasConnection (const PatchDocument& doc, const juce::String& from, const juce::String& fromPort,
                        const juce::String& to, const juce::String& toPort)
    {
        return std::any_of (doc.connections.begin(), doc.connections.end(), [&] (const auto& c)
                            { return c.fromNodeId == from && c.fromPortId == fromPort && c.toNodeId == to && c.toPortId == toPort; });
    }
}
