#include "GraphEditController.h"
#include "PluginProcessor.h"
#include "bazalt/engine/graph/CanConnect.h"
#include "bazalt/engine/graph/DomainSplitter.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/PortGroups.h"
#include "bazalt/engine/graph/ProofGraphs.h"

namespace bazalt
{
    namespace
    {
        // Message-thread-only, metadata-only lookup (mirrors
        // NodeFactory::describeAll()'s own "construct one throwaway
        // instance purely to read its metadata" pattern) — never touched
        // by the audio thread.
        const bazalt::engine::PortDescriptor* findOutputPort (const bazalt::engine::NodeFactory& factory,
                                                                const juce::String& typeId, const juce::String& portId,
                                                                std::vector<bazalt::engine::PortDescriptor>& storage)
        {
            auto node = factory.create (typeId);
            if (node == nullptr)
                return nullptr;

            storage = node->getOutputPorts();
            for (const auto& port : storage)
                if (port.id == portId)
                    return &port;
            return nullptr;
        }

        const bazalt::engine::PortDescriptor* findInputPort (const bazalt::engine::NodeFactory& factory,
                                                               const juce::String& typeId, const juce::String& portId,
                                                               std::vector<bazalt::engine::PortDescriptor>& storage)
        {
            auto node = factory.create (typeId);
            if (node == nullptr)
                return nullptr;

            storage = node->getInputPorts();

            // A growable-group member beyond the default count (in.5 on a
            // math.add that starts with in.0/in.1) only exists once it's
            // wired — GraphCompiler sizes the group from the connections —
            // so size this throwaway node for the port being asked about.
            if (const auto groupIndex = bazalt::engine::portGroupIndexOf (storage, portId); groupIndex >= 0)
            {
                node->setGroupPortCount (groupIndex + 1);
                storage = node->getInputPorts();
            }

            for (const auto& port : storage)
                if (port.id == portId)
                    return &port;
            return nullptr;
        }

        juce::String uniqueAdapterNodeId (const bazalt::engine::NodeGraph& graph, const juce::String& base)
        {
            if (graph.findNode (base) == nullptr)
                return base;

            for (int i = 2;; ++i)
            {
                const auto candidate = base + juce::String (i);
                if (graph.findNode (candidate) == nullptr)
                    return candidate;
            }
        }

        // wiki/NODES_Gaps.md's `occupied-port-rejects` (0.4): dropping a new
        // cable onto an input that already has one now REPLACES the old
        // connection instead of the whole command failing outright with
        // GraphCompiler's "Input port already connected" — an input port
        // accepts exactly one source by design (that compiler check still
        // exists and still protects against this ever slipping through, e.g.
        // a malformed graphRestoreSnapshot), so there is at most one existing
        // connection to remove. Copies the matched connection's own fields
        // out to locals before calling removeConnection(): passing
        // `existing.fromNodeId`/`fromPortId` straight through would bind
        // removeConnection()'s parameters to a live reference INTO the same
        // vector its own std::remove_if is about to shuffle — reading that
        // reference mid-erase would be comparing against whatever value got
        // moved into that slot, not the original one being removed.
        void replaceExistingInputConnection (bazalt::engine::NodeGraph& g, const juce::String& toNodeId, const juce::String& toPortId)
        {
            for (const auto& existing : g.getConnections())
            {
                if (existing.toNodeId == toNodeId && existing.toPortId == toPortId)
                {
                    const auto fromNodeId = existing.fromNodeId;
                    const auto fromPortId = existing.fromPortId;
                    g.removeConnection (fromNodeId, fromPortId, toNodeId, toPortId);
                    return;
                }
            }
        }
    }

    GraphEditController::GraphEditController (BazaltAudioProcessor& processorToUse)
        : processor (processorToUse),
          // M22: the Init Patch, not the M2 proof graph - a fresh instance
          // (no saved state) now opens already playing a real subtractive
          // synth, since there's no other reachable way to play it yet (no
          // preset system exists, ADR-0021/M29). buildVoiceProofGraph()
          // stays registered and its own tests still build it explicitly.
          graph (bazalt::engine::buildInitPatchGraph())
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
        replaceExistingInputConnection (graph, toNodeId, toPortId);
        graph.addConnection ({ fromNodeId, fromPortId, toNodeId, toPortId });

        auto result = recompileAndPublish();
        if (! result.success)
            graph = previousGraph;

        return result;
    }

    GraphEditController::CommandResult GraphEditController::connectWithAutoAdapt (
        const juce::String& fromNodeId, const juce::String& fromPortId,
        const juce::String& toNodeId, const juce::String& toPortId)
    {
        const auto* fromNode = graph.findNode (fromNodeId);
        if (fromNode == nullptr)
            return { false, "No such node: " + fromNodeId };

        const auto* toNode = graph.findNode (toNodeId);
        if (toNode == nullptr)
            return { false, "No such node: " + toNodeId };

        auto& factory = processor.getNodeFactory();
        std::vector<bazalt::engine::PortDescriptor> fromStorage, toStorage;
        const auto* fromPort = findOutputPort (factory, fromNode->type, fromPortId, fromStorage);
        if (fromPort == nullptr)
            return { false, "Node '" + fromNodeId + "' has no output port '" + fromPortId + "'" };

        const auto* toPort = findInputPort (factory, toNode->type, toPortId, toStorage);
        if (toPort == nullptr)
            return { false, "Node '" + toNodeId + "' has no input port '" + toPortId + "'" };

        // A polymorphic-port node (util.reroute) has no fixed port type to
        // pre-check against: what its ports report is decided by what's wired
        // to them, which only GraphCompiler's resolution pass knows (Node.h,
        // hasPolymorphicPorts()). The default descriptor read above says
        // "Audio", so canConnect() against it would reject a Control cable
        // into a Reroute outright — and did, until this check existed.
        // GraphCompiler is the authority on validity anyway (its own canConnect
        // pass runs on the resolved types and rejects a real mismatch with the
        // same reason text), so for these endpoints go straight to connect().
        // The cost is that no adapter is auto-inserted across a Reroute; the
        // user gets the compiler's rejection instead of a silent conversion.
        auto isPolymorphic = [&factory] (const bazalt::engine::NodeInstance& instance)
        {
            const auto node = factory.create (instance.type);
            return node != nullptr && node->hasPolymorphicPorts();
        };

        if (isPolymorphic (*fromNode) || isPolymorphic (*toNode))
            return connect (fromNodeId, fromPortId, toNodeId, toPortId);

        const auto connectivity = bazalt::engine::canConnect (*fromPort, *toPort);

        if (connectivity.outcome == bazalt::engine::ConnectionOutcome::Reject)
            return { false, connectivity.reason };

        if (connectivity.outcome == bazalt::engine::ConnectionOutcome::Ok)
            return connect (fromNodeId, fromPortId, toNodeId, toPortId);

        // NeedsAdapters. A 1- or 2-step, single-input chain can be spliced
        // in generically (adapt.map/adapt.normalise/adapt.threshold/
        // adapt.remap, and — since the real stereo cable redesign made it a
        // genuine 1-in-1-out node — mix.downmix too now; ADR-0019's later
        // waves — Envelope Follower, Sample & Hold, Note gate/value — may
        // need the 2-step path this loop already supports). Every step is a
        // real, ordinary, visible node added to the graph below — never a
        // hidden/implicit conversion inside the wire itself (ADR-0019's
        // whole premise).
        if (connectivity.adapterChain.empty() || connectivity.adapterChain.size() > 2)
            return { false, "No auto-insertable adapter for this connection: " + connectivity.reason };

        const auto numSteps = (int) connectivity.adapterChain.size();

        return applyBatch ([&] (bazalt::engine::NodeGraph& g)
        {
            auto currentFromNodeId = fromNodeId;
            auto currentFromPortId = fromPortId;

            for (int i = 0; i < numSteps; ++i)
            {
                const auto& step = connectivity.adapterChain[(size_t) i];
                const auto idSuffix = numSteps == 1 ? juce::String ("_adapter") : ("_adapter" + juce::String (i + 1));
                const auto adapterId = uniqueAdapterNodeId (graph, fromNodeId + "_" + toNodeId + idSuffix);

                // Position: a single adapter keeps the original midpoint; a
                // two-step chain spaces its nodes at 1/3 and 2/3 along the
                // source->destination line instead of stacking both on top
                // of each other.
                const auto t = numSteps == 1 ? 0.5f : (i == 0 ? (1.0f / 3.0f) : (2.0f / 3.0f));
                const auto posX = fromNode->position.x + (toNode->position.x - fromNode->position.x) * t;
                const auto posY = fromNode->position.y + (toNode->position.y - fromNode->position.y) * t;

                bazalt::engine::NodeInstance instance;
                instance.id = adapterId;
                instance.type = step.typeId;
                instance.position = { posX, posY };

                // Seeding (SIGNAL_TYPES.md §5's "Seeding" column) — by
                // convention every min/max-seeded adapter this milestone
                // ships names its parameters "<typeId>.min"/"<typeId>.max"
                // (adapt.map, adapt.normalise both do); a future adapter
                // that doesn't follow this convention needs its own branch
                // here, not a silent wrong guess. adapt.remap is exactly
                // that case — it seeds from BOTH ends at once (its own
                // four-parameter inMin/inMax/outMin/outMax shape, not a
                // single min/max pair), since seedFromSourceRange and
                // seedFromDestinationRange are both set on its step
                // (CanConnect.cpp). Every other step still seeds itself
                // independently from the ORIGINAL endpoints' own ranges
                // (fromPort/toPort), not from whatever the previous step in
                // the chain happens to be.
                if (step.typeId == "adapt.remap")
                {
                    if (fromPort->minValue.has_value() && fromPort->maxValue.has_value())
                    {
                        instance.parameters["adapt.remap.inMin"] = *fromPort->minValue;
                        instance.parameters["adapt.remap.inMax"] = *fromPort->maxValue;
                    }
                    if (toPort->minValue.has_value() && toPort->maxValue.has_value())
                    {
                        instance.parameters["adapt.remap.outMin"] = *toPort->minValue;
                        instance.parameters["adapt.remap.outMax"] = *toPort->maxValue;
                    }
                }
                else if (step.seedFromDestinationRange && toPort->minValue.has_value() && toPort->maxValue.has_value())
                {
                    instance.parameters[step.typeId + ".min"] = *toPort->minValue;
                    instance.parameters[step.typeId + ".max"] = *toPort->maxValue;
                }
                else if (step.seedFromSourceRange && fromPort->minValue.has_value() && fromPort->maxValue.has_value())
                {
                    instance.parameters[step.typeId + ".min"] = *fromPort->minValue;
                    instance.parameters[step.typeId + ".max"] = *fromPort->maxValue;
                }

                g.addNode (std::move (instance));
                g.addConnection ({ currentFromNodeId, currentFromPortId, adapterId, step.inputPortId });

                currentFromNodeId = adapterId;
                currentFromPortId = step.outputPortId;
            }

            replaceExistingInputConnection (g, toNodeId, toPortId);
            g.addConnection ({ currentFromNodeId, currentFromPortId, toNodeId, toPortId });
        });
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

    GraphEditController::CommandResult GraphEditController::setOutput (const juce::String& nodeId, const juce::String& portId)
    {
        if (graph.findNode (nodeId) == nullptr)
            return { false, "No such node: " + nodeId };

        const auto previousGraph = graph;
        graph.setOutput (nodeId, portId);

        auto result = recompileAndPublish();
        if (! result.success)
            graph = previousGraph;

        return result;
    }

    GraphEditController::CommandResult GraphEditController::moveNode (const juce::String& nodeId, float x, float y)
    {
        auto* node = graph.findNode (nodeId);
        if (node == nullptr)
            return { false, "No such node: " + nodeId };

        const auto previousGraph = graph;
        node->position = { x, y };

        auto result = recompileAndPublish();
        if (! result.success)
            graph = previousGraph;

        return result;
    }

    GraphEditController::CommandResult GraphEditController::setProperty (const juce::String& nodeId,
                                                                           const juce::String& propertyKey,
                                                                           juce::var value)
    {
        auto* node = graph.findNode (nodeId);
        if (node == nullptr)
            return { false, "No such node: " + nodeId };

        const auto previousGraph = graph;
        node->properties[propertyKey] = std::move (value);

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

    GraphEditController::CommandResult GraphEditController::applyBatch (
        const std::function<void (bazalt::engine::NodeGraph&)>& mutate)
    {
        const auto previousGraph = graph;
        mutate (graph);

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

        // 09-28-InstanceAllocator arc: computed once here, uniformly, for
        // whichever of the three branches below split actually took, rather
        // than duplicating "which nodes ended up where" per branch. A plain
        // debugging aid (getNodeDomains(), the UI's DomainDot). Kept LOCAL
        // until a real publish succeeds, deliberately not written straight
        // into the `nodeDomains` member here: `split` succeeding only means
        // the GRAPH partitions cleanly, not that GraphCompiler can actually
        // compile either half - several return paths below still fail
        // after this point, and CLAUDE.md rule 5's rollback contract means
        // the member must keep reflecting the last graph that ACTUALLY
        // published, not one that was merely attempted.
        std::unordered_map<juce::String, juce::String> newNodeDomains;
        const juce::String voiceLabel = split.monoOnly ? "mono" : "voice";
        for (const auto& node : split.voiceGraph.getNodes())
            newNodeDomains[node.id] = voiceLabel;
        if (split.hasGlobalDomain)
            for (const auto& node : split.globalGraph.getNodes())
                newNodeDomains[node.id] = "global";

        const bazalt::engine::NodePrepareInfo prepareInfo { sampleRate, blockSize };
        auto& factory = processor.getNodeFactory();

        // M21 (DOMAINS.md §7): a graph with no instance.voice has no poly
        // region, so the whole graph compiles ONCE and is published as the one
        // global plan, which the processor runs every block. No voice plans are
        // touched: they are never run while the mono flag is set, and the next
        // non-mono edit recompiles and republishes every one of them.
        if (split.monoOnly)
        {
            const auto* previousPlan = processor.getGlobalPlanSwapper().peekCurrentPlan();

            auto monoCompile = bazalt::engine::GraphCompiler::compile (
                split.voiceGraph, factory, prepareInfo, nextGeneration(), previousPlan);

            if (! monoCompile.success)
                return { false, monoCompile.errorMessage };

            auto monoPlan = std::make_unique<bazalt::engine::ExecutionPlan> (std::move (monoCompile.plan));

            // ADR-0029: re-attach live preview taps to the new plan while
            // nothing else can see it (a tap pointer lives on a plan).
            processor.applyPreviewSubscriptions ({}, monoPlan.get());

            auto& swapper = processor.getGlobalPlanSwapper();
            swapper.reclaim();
            const auto published = swapper.publish (std::move (monoPlan));
            jassert (published);
            juce::ignoreUnused (published);

            hasGlobalDomain = false;
            processor.setHasGlobalDomain (false);
            processor.setMonoOnly (true); // after the plan is live, so the audio thread never sees the flag first
            nodeDomains = std::move (newNodeDomains);

            return { true, {} };
        }

        std::vector<std::unique_ptr<bazalt::engine::ExecutionPlan>> newVoicePlans;
        newVoicePlans.reserve ((size_t) BazaltAudioProcessor::numVoices);

        for (int i = 0; i < BazaltAudioProcessor::numVoices; ++i)
        {
            // M17 state pool: this voice's currently-published plan (if
            // any) is passed as `previousPlan` so GraphCompiler can reuse
            // unchanged nodes' DSP state — peekCurrentPlan(), not
            // getCurrentPlanForAudioThread() (see PlanSwapper.h's own
            // comment on why that distinction matters here).
            const auto* previousPlan = processor.getVoicePlanSwapper (i).peekCurrentPlan();

            auto compileResult = bazalt::engine::GraphCompiler::compile (
                split.voiceGraph, factory, prepareInfo, nextGeneration(), previousPlan);

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
            newGlobalPlan->externalInputNodeId = split.instanceMixNodeId;
        }

        // ADR-0029: re-attach live preview taps to the new plans before any of
        // them is published - until then the audio thread cannot see them, so
        // this cannot race. Without it every edit silently detached every
        // preview (a tap pointer lives on a plan, and every edit builds new ones).
        {
            std::vector<bazalt::engine::ExecutionPlan*> voicePlanPointers;
            voicePlanPointers.reserve (newVoicePlans.size());
            for (auto& plan : newVoicePlans)
                voicePlanPointers.push_back (plan.get());

            processor.applyPreviewSubscriptions (voicePlanPointers, newGlobalPlan.get());
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
        processor.setMonoOnly (false); // after the voice (and global) plans are live
        nodeDomains = std::move (newNodeDomains);

        return { true, {} };
    }
}
