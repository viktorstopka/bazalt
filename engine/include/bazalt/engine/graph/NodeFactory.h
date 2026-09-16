#pragma once

#include "bazalt/engine/graph/Node.h"
#include <functional>
#include <memory>
#include <unordered_map>

namespace bazalt::engine
{
    using NodeCreateFn = std::function<std::unique_ptr<Node>()>;

    /** Maps a NodeGraph's stable `type` strings to concrete Node factories.
        Registration happens once (e.g. render-cli's or a test's startup);
        GraphCompiler only ever calls create(), never mutates this.
    */
    class NodeFactory
    {
    public:
        void registerType (const juce::String& typeId, NodeCreateFn createFn)
        {
            creators[typeId] = std::move (createFn);
        }

        std::unique_ptr<Node> create (const juce::String& typeId) const
        {
            const auto it = creators.find (typeId);
            return it == creators.end() ? nullptr : it->second();
        }

        bool isRegistered (const juce::String& typeId) const
        {
            return creators.find (typeId) != creators.end();
        }

    private:
        std::unordered_map<juce::String, NodeCreateFn> creators;
    };
}
