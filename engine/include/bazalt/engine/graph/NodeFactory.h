#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/NodeDescriptor.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

        /** A decoration (wiki/plans/Decorations.md): a node that exists on the
            canvas only — headers, comments, boxes, images. GraphCompiler skips
            it entirely, so it costs nothing at runtime. */
        void registerDecoration (const juce::String& typeId, NodeCreateFn createFn)
        {
            registerType (typeId, std::move (createFn));
            decorationTypes.insert (typeId);
        }

        bool isDecoration (const juce::String& typeId) const { return decorationTypes.count (typeId) > 0; }

        std::unique_ptr<Node> create (const juce::String& typeId) const
        {
            const auto it = creators.find (typeId);
            return it == creators.end() ? nullptr : it->second();
        }

        bool isRegistered (const juce::String& typeId) const
        {
            return creators.find (typeId) != creators.end();
        }

        /** Every registered type's descriptor (NODE_EDITOR.md §3), for the
            UI's Add menu — message-thread only, constructs one throwaway
            instance per type purely to read its metadata, never touched by
            the audio thread. Order matches registration order.
        */
        std::vector<NodeDescriptor> describeAll() const
        {
            std::vector<NodeDescriptor> descriptors;
            descriptors.reserve (creators.size());

            for (const auto& [typeId, createFn] : creators)
                descriptors.push_back (describeNode (typeId, *createFn()));

            return descriptors;
        }

    private:
        std::unordered_map<juce::String, NodeCreateFn> creators;
        std::unordered_set<juce::String> decorationTypes;
    };
}
