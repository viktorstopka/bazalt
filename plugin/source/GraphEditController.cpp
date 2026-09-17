#include "GraphEditController.h"
#include "PluginProcessor.h"
#include "bazalt/engine/graph/DomainSplitter.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"

namespace bazalt
{
    GraphEditController::GraphEditController (BazaltAudioProcessor& processorToUse)
        : processor (processorToUse),
          graph (bazalt::engine::buildVoiceProofGraph())
    {
    }

    void GraphEditController::prepare (double sampleRateIn, int blockSizeIn)
    {
        sampleRate = sampleRateIn;
        blockSize = blockSizeIn;
        isPrepared = true;

        const auto result = recompileAndPublish();

        // The built-in default graph (or whatever a patch load already
        // installed before prepareToPlay ran) must always compile — if it
        // doesn't, that's a real engine/data bug, not a runtime condition
        // to recover from silently.
        jassert (result.success);
        juce::ignoreUnused (result);
    }

    GraphEditController::CommandResult GraphEditController::addNode (const juce::String& typeId,
                                                                      const juce::String& nodeId,
                                                                      float x, float y)
    {
        if (nodeId.isEmpty())
            return { false, "Node id must not be empty" };

        if (! processor.getNodeFactory().isRegistered (typeId))
            return { false, "Unknown node type: " + typeId };

        if (graph.findNode (nodeId) != nullptr)
            return { false, "Node id already exists: " + nodeId };

        const auto previousGraph = graph;

        bazalt::engine::NodeInstance instance;
        instance.id = nodeId;
        instance.type = typeId;
        instance.position = { x, y };
        graph.addNode (std::move (instance));

        auto result = recompileAndPublish();
        if (! result.success)
            graph = previousGraph;

        return result;
    }

    GraphEditController::CommandResult GraphEditController::deleteNode (const juce::String& nodeId)
    {
        if (graph.findNode (nodeId) == nullptr)
            return { false, "No such node: " + nodeId };

        const auto previousGraph = graph;
        graph.removeNode (nodeId);

        auto result = recompileAndPublish();
        if (! result.success)
            graph = previousGraph;

        return result;
    }

    GraphEditController::CommandResult GraphEditController::connect (const juce::String& fromNodeId,
                                                                      const juce::String& fromPortId,
                                                                      const juce::String& toNodeId,
                                                                      const juce::String& toPortId)
    {
        if (graph.findNode (fromNodeId) == nullptr)
            return { false, "No such node: " + fromNodeId };

        if (graph.findNode (toNodeId) == nullptr)
            return { false, "No such node: " + toNodeId };

        const auto previousGraph = graph;
        graph.addConnection ({ fromNodeId, fromPortId, toNodeId, toPortId });

        auto result = recompileAndPublish();
        if (! result.success)
            graph = previousGraph;

        return result;
    }

    GraphEditController::CommandResult GraphEditController::disconnect (const juce::String& fromNodeId,
                                                                         const juce::String& fromPortId,
                                                                         const juce::String& toNodeId,
                                                                         const juce::String& toPortId)
    {
        const auto previousGraph = graph;
        graph.removeConnection (fromNodeId, fromPortId, toNodeId, toPortId);

        auto result = recompileAndPublish();
        if (! result.success)
            graph = previousGraph;

        return result;
    }

    GraphEditController::CommandResult GraphEditController::setParameterValue (const juce::String& nodeId,
                                                                                const juce::String& parameterId,
                                                                                float value)
    {
        auto* node = graph.findNode (nodeId);
        if (node == nullptr)
            return { false, "No such node: " + nodeId };

        const auto previousGraph = graph;
        node->parameters[parameterId] = value;

        auto result = recompileAndPublish();
        if (! result.success)
            graph = previousGraph;

        return result;
    }

    GraphEditController::CommandResult GraphEditController::setGraph (bazalt::engine::NodeGraph newGraph)
    {
        const auto previousGraph = graph;
        graph = std::move (newGraph);

        auto result = recompileAndPublish();
        if (! result.success)
            graph = previousGraph;

        return result;
    }

    GraphEditController::CommandResult GraphEditController::recompileAndPublish()
    {
        if (! isPrepared)
            return { true, {} }; // nothing to compile yet — prepare() does the first compile

        auto split = bazalt::engine::DomainSplitter::split (graph);
        if (! split.success)
            return { false, split.errorMessage };

        const bazalt::engine::NodePrepareInfo prepareInfo { sampleRate, blockSize };
        auto& factory = processor.getNodeFactory();

        std::vector<std::unique_ptr<bazalt::engine::ExecutionPlan>> newVoicePlans;
        newVoicePlans.reserve ((size_t) BazaltAudioProcessor::numVoices);

        for (int i = 0; i < BazaltAudioProcessor::numVoices; ++i)
        {
            auto compileResult = bazalt::engine::GraphCompiler::compile (
                split.voiceGraph, factory, prepareInfo, nextGeneration());

            if (! compileResult.success)
                return { false, compileResult.errorMessage };

            newVoicePlans.push_back (
                std::make_unique<bazalt::engine::ExecutionPlan> (std::move (compileResult.plan)));
        }

        std::unique_ptr<bazalt::engine::ExecutionPlan> newGlobalPlan;
        if (split.hasGlobalDomain)
        {
            auto globalCompileResult = bazalt::engine::GraphCompiler::compile (
                split.globalGraph, factory, prepareInfo, nextGeneration());

            if (! globalCompileResult.success)
                return { false, globalCompileResult.errorMessage };

            newGlobalPlan = std::make_unique<bazalt::engine::ExecutionPlan> (std::move (globalCompileResult.plan));
            newGlobalPlan->externalInputNodeId = split.voiceSumNodeId;
        }

        // Every compile succeeded — publish. Never partially publish on a
        // failure path above; this is the point past which the edit is
        // committed. PlanSwapper's 4-slot pool is normally kept clear by
        // the plugin's ~50ms reclaim timer (PluginProcessor::timerCallback),
        // but that timer doesn't run in a headless test (no message loop),
        // and even in a real host several edits within one 50ms window
        // (e.g. a UI dragging a slider, each tick its own setParameterValue
        // command) could outrun it. Reclaiming right before every publish
        // makes success here independent of that timer's schedule — with
        // one edit in flight at a time, reclaim() always frees at least one
        // of the 4 slots, so this isn't a busy/blocking loop in practice.
        for (int i = 0; i < BazaltAudioProcessor::numVoices; ++i)
        {
            auto& swapper = processor.getVoicePlanSwapper (i);
            swapper.reclaim();
            const auto published = swapper.publish (std::move (newVoicePlans[(size_t) i]));
            jassert (published);
            juce::ignoreUnused (published);
        }

        if (newGlobalPlan)
        {
            processor.getGlobalPlanSwapper().reclaim();
            const auto published = processor.getGlobalPlanSwapper().publish (std::move (newGlobalPlan));
            jassert (published);
            juce::ignoreUnused (published);
        }

        hasGlobalDomain = split.hasGlobalDomain;
        processor.setHasGlobalDomain (hasGlobalDomain);

        return { true, {} };
    }
}
