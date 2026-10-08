#include "GraphEditController.h"
#include <optional>
#include "PluginProcessor.h"
#include "bazalt/engine/graph/CanConnect.h"
#include "bazalt/engine/graph/MultiplicityResolver.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/PortGroups.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/InstanceMixNode.h"
#include "bazalt/engine/nodes/InstanceOriginNode.h"
#include "bazalt/engine/nodes/InstanceSwarmPopulationNode.h"
#include "bazalt/engine/patch/PatchSerializer.h"
#include <optional>
#include <algorithm>
#include <array>
#include <map>

namespace bazalt
{
    namespace
    {
        // Message-thread-only, metadata-only lookup (mirrors
        // NodeFactory::describeAll()'s own "construct one throwaway
        // instance purely to read its metadata" pattern) — never touched
        // by the audio thread. Applies the REAL node instance's own
        // parameters to that throwaway before reading its ports, exactly
        // like GraphCompiler::compile()'s own node-construction loop
        // already does (setParameter() for every entry in
        // instance.parameters, before ever calling getOutputPorts()/
        // getInputPorts()) — this used to skip that step entirely, reading
        // a config-driven node's (util.macro/util.constant) ports at their
        // bare-default construction state (always Control-typed,
        // TypedValueNodeBase.h's own storedType default) no matter what
        // the actually-placed instance's own "type" parameter said. Direct,
        // reproducible feedback: "trigger macro cannot be plugged into
        // trigger. same for bool." — this function's own stale Control
        // guess is exactly why: Control->Event silently misfired the
        // Threshold auto-adapter instead of a direct wire, and
        // Control->Boolean has no adapter at all (CanConnect.cpp has one
        // for the reverse, Boolean->Control, not this direction), so that
        // one hit a flat, confusing reject. GraphCompiler's own later,
        // authoritative canConnect pass (GraphCompiler.cpp) already reads
        // post-setParameter ports correctly and was never the problem —
        // this earlier, pre-GraphCompiler gate (the one that decides
        // whether to auto-insert an adapter chain at all) was the one
        // still guessing.
        void applyInstanceParameters (bazalt::engine::Node& node, const bazalt::engine::NodeInstance& instance)
        {
            for (const auto& [paramId, value] : instance.parameters)
                node.setParameter (paramId, value);
        }

        const bazalt::engine::PortDescriptor* findOutputPort (const bazalt::engine::NodeFactory& factory,
                                                                const bazalt::engine::NodeInstance& instance, const juce::String& portId,
                                                                std::vector<bazalt::engine::PortDescriptor>& storage)
        {
            auto node = factory.create (instance.type);
            if (node == nullptr)
                return nullptr;

            applyInstanceParameters (*node, instance);

            storage = node->getOutputPorts();
            for (const auto& port : storage)
                if (port.id == portId)
                    return &port;
            return nullptr;
        }

        // DomainRedesign.md Batch 1: MultiplicityResolver::split() always
        // points globalGraph's own output designation at the GRAPH's real
        // designated output — meaningful when that output resolved Scalar
        // (split.hasGlobalDomain true, the ordinary case), but dangling
        // when it resolved Poly instead (the origin's own voice-sum is the
        // real audible output; globalGraph still gets compiled whenever it
        // has OTHER, unrelated Scalar content, purely so GraphCompiler's
        // validation runs on it — see recompileAndPublish()'s own comment).
        // GraphCompiler requires a real node+port to compile against
        // regardless of whether anything downstream ever reads it, so this
        // retargets it to literally any real output port on any node
        // globalGraph actually contains — never read by the driver in this
        // case, exactly like DomainSplitter's own "point at the allocator's
        // gate" fallback for an unbridged voiceGraph.
        void ensureGlobalGraphHasAValidOutput (bazalt::engine::NodeGraph& globalGraph, const bazalt::engine::NodeFactory& factory)
        {
            if (globalGraph.findNode (globalGraph.getOutputNodeId()) != nullptr)
                return; // the real output IS a member — nothing to do

            for (const auto& node : globalGraph.getNodes())
            {
                auto instance = factory.create (node.type);
                if (instance == nullptr)
                    continue;

                const auto ports = instance->getOutputPorts();
                if (! ports.empty())
                {
                    globalGraph.setOutput (node.id, ports[0].id);
                    return;
                }
            }

            // Every node globalGraph contains is a pure sink (no output port
            // at all, e.g. a lone, disconnected view.scope) — a real but
            // extremely narrow gap: nothing here can give GraphCompiler a
            // target, so this graph's own validation can't run this edit.
            // Left as the resolver's own (now-dangling) designation; the
            // compile below will fail with GraphCompiler's own clear
            // "output node not found" message rather than silently
            // succeeding against the wrong thing.
        }

        // wiki/plans/UtilMacro.md Batch 2: which numbered host slot (0-31)
        // a "util.macro" node claims, or -1 if it's the unclaimed sentinel
        // (MacroNode.h's own doc comment on why -1, not a real slot
        // number, is the declared default) or the parameter is somehow
        // missing entirely — both treated identically to "unclaimed".
        int macroSlotOf (const bazalt::engine::NodeInstance& node)
        {
            const auto it = node.parameters.find ("util.macro.slot");
            if (it == node.parameters.end())
                return -1;
            const auto slot = (int) std::lround (it->second);
            return (slot >= 0 && slot < 32) ? slot : -1;
        }

        // Real, found-live hazard (wiki/plans/UtilMacro.md's own Finding
        // B): checked BEFORE any compile work below, so a collision is
        // rejected with a clear message naming both nodes, rather than
        // MultiplicityResolver/GraphCompiler running first and failing
        // confusingly somewhere unrelated. An unclaimed macro (slot
        // outside [0,31]) is exempt — the ordinary state for a brand-new,
        // not-yet-slotted node between its own addNode and
        // setParameterValue(slot, N) commands.
        juce::String macroSlotCollisionError (const bazalt::engine::NodeGraph& graphToCheck)
        {
            std::unordered_map<int, juce::String> nodeIdBySlot;
            for (const auto& node : graphToCheck.getNodes())
            {
                if (node.type != "util.macro")
                    continue;
                const auto slot = macroSlotOf (node);
                if (slot < 0)
                    continue;
                if (const auto existing = nodeIdBySlot.find (slot); existing != nodeIdBySlot.end())
                    return "Macro slot " + juce::String (slot) + " is claimed by two nodes ('" + existing->second
                           + "', '" + node.id + "') - reassign one to a different slot before this graph will compile";
                nodeIdBySlot[slot] = node.id;
            }
            return {};
        }

        // wiki/plans/UtilMacro.md Batch 2: the whole replacement for
        // PluginProcessor's old hardcoded setDefaultMacroMappings() — the
        // MacroMapping table is now a pure function of the graph's own
        // util.macro nodes, rebuilt fresh on every successful compile,
        // never hand-curated. Each claimed node becomes its OWN mapping
        // target (MacroNode.h has the full reasoning for why
        // "util.macro.value", 0..1, is the right target rather than the
        // old {arbitrary foreign node, arbitrary foreign param} pair) —
        // MacroParameters::applyToPlans() itself needs no changes at all
        // to support this, it was always generic over the target address.
        std::vector<bazalt::engine::MacroMapping> deriveMacroMappings (const bazalt::engine::NodeGraph& graphToScan)
        {
            std::vector<bazalt::engine::MacroMapping> mappings;
            for (const auto& node : graphToScan.getNodes())
            {
                if (node.type != "util.macro")
                    continue;
                const auto slot = macroSlotOf (node);
                if (slot < 0)
                    continue;
                mappings.push_back ({ slot, node.id, "util.macro.value", 0.0f, 1.0f });
            }
            return mappings;
        }

        /** A port's range for seeding an auto-inserted adapt.map (input side
            from the feeder, output side from the destination): its own
            declared bounds, else its polarity (a Modulation port that
            declares no bounds is still -1..1 or 0..1 by definition, an audio
            port ±1), else nothing — the node's own default stands. */
        std::optional<std::pair<float, float>> seedRangeForMapInput (const bazalt::engine::PortDescriptor& feeder)
        {
            if (feeder.minValue.has_value() && feeder.maxValue.has_value())
                return std::make_pair (*feeder.minValue, *feeder.maxValue);
            // An audio port is a waveform: ±1 (wiki/plans/DataAndWavetable.md D1).
            if (feeder.quantity == bazalt::engine::Quantity::Bipolar || feeder.type == bazalt::engine::SignalType::Audio)
                return std::make_pair (-1.0f, 1.0f);
            if (feeder.quantity == bazalt::engine::Quantity::Unipolar)
                return std::make_pair (0.0f, 1.0f);
            return std::nullopt;
        }

        const bazalt::engine::PortDescriptor* findInputPort (const bazalt::engine::NodeFactory& factory,
                                                               const bazalt::engine::NodeInstance& instance, const juce::String& portId,
                                                               std::vector<bazalt::engine::PortDescriptor>& storage)
        {
            auto node = factory.create (instance.type);
            if (node == nullptr)
                return nullptr;

            applyInstanceParameters (*node, instance);

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

        /** Every node of `g` as GraphCompiler would see it once its
            polymorphic ports are resolved: parameters applied, growable
            groups sized from the connections, and the same fixed-point pass
            as GraphCompiler.cpp's "Resolve polymorphic port types" (a
            type-following node takes on what is wired into it, one hop per
            pass). Message thread only — a throwaway set of nodes, never a plan.
        */
        std::map<juce::String, std::unique_ptr<bazalt::engine::Node>> resolveGraphPorts (const bazalt::engine::NodeGraph& g,
                                                                                          const bazalt::engine::NodeFactory& factory)
        {
            std::map<juce::String, std::unique_ptr<bazalt::engine::Node>> nodes;
            for (const auto& instance : g.getNodes())
            {
                auto node = factory.create (instance.type);
                if (node == nullptr)
                    continue;

                std::vector<juce::String> incoming;
                for (const auto& connection : g.getConnections())
                    if (connection.toNodeId == instance.id)
                        incoming.push_back (connection.toPortId);
                if (const auto groupCount = bazalt::engine::requiredPortGroupCount (*node, incoming); groupCount >= 0)
                    node->setGroupPortCount (groupCount);

                applyInstanceParameters (*node, instance);
                nodes[instance.id] = std::move (node);
            }

            const auto signature = [] (const bazalt::engine::Node& node)
            {
                std::vector<std::pair<int, int>> result;
                for (const auto& port : node.getInputPorts())
                    result.emplace_back ((int) port.type, (int) port.quantity);
                for (const auto& port : node.getOutputPorts())
                    result.emplace_back ((int) port.type, (int) port.quantity);
                return result;
            };

            for (size_t pass = 0; pass < nodes.size(); ++pass)
            {
                bool anyChanged = false;
                for (const auto& connection : g.getConnections())
                {
                    const auto from = nodes.find (connection.fromNodeId);
                    const auto to = nodes.find (connection.toNodeId);
                    if (from == nodes.end() || to == nodes.end() || ! to->second->hasPolymorphicPorts())
                        continue;

                    const auto sourcePorts = from->second->getOutputPorts();
                    const auto source = std::find_if (sourcePorts.begin(), sourcePorts.end(),
                                                      [&] (const auto& port) { return port.id == connection.fromPortId; });
                    if (source == sourcePorts.end())
                        continue;

                    const auto before = signature (*to->second);
                    to->second->resolveIncomingPort (connection.toPortId, *source);
                    anyChanged = anyChanged || signature (*to->second) != before;
                }
                if (! anyChanged)
                    break;
            }
            return nodes;
        }

        // wiki/plans/DomainRedesign.md Batch 2: which numbered
        // BazaltAudioProcessor::OriginBundle slot each of THIS compile's
        // origins should occupy — computed purely by reading the
        // processor's CURRENT (still-live) bundle state, never mutating it;
        // recompileAndPublish() only commits this mapping once every
        // compile below has actually succeeded (CLAUDE.md rule 5). An
        // origin id already occupying an ACTIVE bundle keeps that same
        // slot (so its VoiceManager — which voices are Active/Idle, real
        // and valuable state — survives an edit that doesn't remove the
        // origin, the same way a single voice's own DSP state already
        // survives via GraphCompiler's previousPlan reuse); every other
        // origin takes the first still-unclaimed slot. Never returns an
        // origin unplaced: MultiplicityResolver::split() already rejects
        // more than maxOrigins origins before this ever runs, and this
        // array has exactly maxOrigins slots.
        std::array<juce::String, BazaltAudioProcessor::maxOrigins> assignOriginBundleSlots (
            const std::vector<bazalt::engine::MultiplicityOrigin>& origins, const BazaltAudioProcessor& processor)
        {
            std::array<juce::String, BazaltAudioProcessor::maxOrigins> slotForOrigin {};
            std::array<bool, BazaltAudioProcessor::maxOrigins> claimed {};

            for (const auto& origin : origins)
            {
                for (int i = 0; i < BazaltAudioProcessor::maxOrigins; ++i)
                {
                    if (! claimed[(size_t) i] && processor.isOriginBundleActive (i)
                        && processor.getOriginBundleOriginId (i) == origin.originId)
                    {
                        slotForOrigin[(size_t) i] = origin.originId;
                        claimed[(size_t) i] = true;
                        break;
                    }
                }
            }

            for (const auto& origin : origins)
            {
                auto alreadyPlaced = false;
                for (int i = 0; i < BazaltAudioProcessor::maxOrigins; ++i)
                    if (slotForOrigin[(size_t) i] == origin.originId) { alreadyPlaced = true; break; }
                if (alreadyPlaced)
                    continue;

                for (int i = 0; i < BazaltAudioProcessor::maxOrigins; ++i)
                {
                    if (! claimed[(size_t) i])
                    {
                        slotForOrigin[(size_t) i] = origin.originId;
                        claimed[(size_t) i] = true;
                        break;
                    }
                }
            }

            return slotForOrigin;
        }

        int bundleSlotOf (const std::array<juce::String, BazaltAudioProcessor::maxOrigins>& slotForOrigin, const juce::String& originId)
        {
            if (originId.isEmpty())
                return -1;

            for (int i = 0; i < BazaltAudioProcessor::maxOrigins; ++i)
                if (slotForOrigin[(size_t) i] == originId)
                    return i;

            return -1;
        }

        // wiki/plans/DomainRedesign.md Batch 4: marks every port of every
        // node in `subgraph` with the same (kind, originId) — a throwaway,
        // message-thread-only instance per node (mirrors
        // NodeFactory::describeAll()'s own "construct one throwaway
        // instance purely to read its metadata" pattern), so a growable
        // group's ports beyond its default count aren't enumerated here;
        // graphStore.ts's own reader falls back to any listed port of the
        // same node id, since ordinary nodes resolve uniformly across all
        // their ports anyway (§2.4 — only the two boundary node types
        // don't, and instance.sum's own "in" port is overridden separately,
        // right after this runs).
        void markAllPorts (std::unordered_map<juce::String, std::unordered_map<juce::String, GraphEditController::PortMultiplicityInfo>>& out,
                            const bazalt::engine::NodeFactory& factory, const bazalt::engine::NodeGraph& subgraph,
                            const juce::String& kind, const juce::String& originId)
        {
            for (const auto& node : subgraph.getNodes())
            {
                auto instance = factory.create (node.type);
                if (instance == nullptr)
                    continue;

                auto& portsForNode = out[node.id];
                for (const auto& port : instance->getInputPorts())
                    portsForNode[port.id] = { kind, originId };
                for (const auto& port : instance->getOutputPorts())
                    portsForNode[port.id] = { kind, originId };
            }
        }
    }

    GraphEditController::GraphEditController (BazaltAudioProcessor& processorToUse)
        : processor (processorToUse),
          // 0.x arc, 2026-09-29: a plain master-out-only graph, not the M22
          // Init Patch - a fresh instance (no saved state) now opens on an
          // empty canvas with just a real output to build onto, per the
          // user's own explicit instruction (a fully-wired subtractive synth
          // was in the way of a graph meant to be edited from empty).
          // buildInitPatchGraph() and buildVoiceProofGraph() both stay
          // registered and are still built explicitly by their own tests.
          graph (bazalt::engine::buildMasterOutOnlyGraph())
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
        const juce::String& toNodeId, const juce::String& toPortId,
        const juce::String& choice)
    {
        const auto* fromNode = graph.findNode (fromNodeId);
        if (fromNode == nullptr)
            return { false, "No such node: " + fromNodeId };

        const auto* toNode = graph.findNode (toNodeId);
        if (toNode == nullptr)
            return { false, "No such node: " + toNodeId };

        auto& factory = processor.getNodeFactory();
        std::vector<bazalt::engine::PortDescriptor> fromStorage, toStorage;
        const auto* fromPort = findOutputPort (factory, *fromNode, fromPortId, fromStorage);
        if (fromPort == nullptr)
            return { false, "Node '" + fromNodeId + "' has no output port '" + fromPortId + "'" };

        const auto* toPort = findInputPort (factory, *toNode, toPortId, toStorage);
        if (toPort == nullptr)
            return { false, "Node '" + toNodeId + "' has no input port '" + toPortId + "'" };

        // A type-following node (math.add/multiply, logic.select/compare,
        // adapt.sampleHold, the viewers, deco.reroute — hasPolymorphicPorts())
        // has no fixed port type: what it reports is decided by what's wired
        // to it, and its default descriptor read above is a guess (a Reroute
        // says "Audio", an Add "Dimensionless"). Resolve both ends the way
        // GraphCompiler will, with this cable already in place, so a real
        // mismatch through one (a Time-valued Add into a Frequency cutoff)
        // still gets its Map instead of the compiler's flat refusal.
        auto isPolymorphic = [&factory] (const bazalt::engine::NodeInstance& instance)
        {
            const auto node = factory.create (instance.type);
            return node != nullptr && node->hasPolymorphicPorts();
        };

        if (isPolymorphic (*fromNode) || isPolymorphic (*toNode))
        {
            auto proposed = graph;
            replaceExistingInputConnection (proposed, toNodeId, toPortId);
            proposed.addConnection ({ fromNodeId, fromPortId, toNodeId, toPortId });
            const auto resolved = resolveGraphPorts (proposed, factory);

            const auto pick = [] (std::vector<bazalt::engine::PortDescriptor>& storage, const juce::String& portId)
                -> const bazalt::engine::PortDescriptor*
            {
                for (const auto& port : storage)
                    if (port.id == portId)
                        return &port;
                return nullptr;
            };

            if (const auto from = resolved.find (fromNodeId); from != resolved.end())
            {
                fromStorage = from->second->getOutputPorts();
                fromPort = pick (fromStorage, fromPortId);
            }
            if (const auto to = resolved.find (toNodeId); to != resolved.end())
            {
                toStorage = to->second->getInputPorts();
                toPort = pick (toStorage, toPortId);
            }
            if (fromPort == nullptr || toPort == nullptr)
                return connect (fromNodeId, fromPortId, toNodeId, toPortId);
        }

        const auto connectivity = bazalt::engine::canConnect (*fromPort, *toPort);

        if (connectivity.outcome == bazalt::engine::ConnectionOutcome::Reject)
            return { false, connectivity.reason };

        if (connectivity.outcome == bazalt::engine::ConnectionOutcome::Ok)
            return connect (fromNodeId, fromPortId, toNodeId, toPortId);

        // NeedsAdapters. A 1- or 2-step, single-input chain can be spliced
        // in generically (adapt.map/adapt.threshold, and — since the real stereo cable redesign made it a
        // genuine 1-in-1-out node — mix.downmix too now; ADR-0019's later
        // waves — Envelope Follower, Sample & Hold, Note gate/value — may
        // need the 2-step path this loop already supports). Every step is a
        // real, ordinary, visible node added to the graph below — never a
        // hidden/implicit conversion inside the wire itself (ADR-0019's
        // whole premise).
        if (connectivity.adapterChain.empty() || connectivity.adapterChain.size() > 2)
            return { false, "No auto-insertable adapter for this connection: " + connectivity.reason };

        // A lossy reduction is the user's call (wiki/plans/StereoChannels.md
        // §3): ask first, then set the first adapter's enum mode parameter to
        // the option they picked.
        std::optional<std::pair<juce::String, float>> choiceParameter;
        if (! connectivity.choices.empty())
        {
            if (std::find (connectivity.choices.begin(), connectivity.choices.end(), choice) == connectivity.choices.end())
            {
                CommandResult needsChoice { false, connectivity.reason };
                for (const auto& option : connectivity.choices)
                    needsChoice.choices.add (option);
                return needsChoice;
            }

            if (const auto adapter = factory.create (connectivity.adapterChain.front().typeId))
                for (const auto& parameter : adapter->getParameters())
                    for (size_t option = 0; option < parameter.enumOptions.size(); ++option)
                        if (parameter.kind == bazalt::engine::ValueKind::Enum && parameter.enumOptions[option].id == choice)
                            choiceParameter = std::make_pair (parameter.id, (float) option);
        }

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

                // Seeding (SIGNAL_TYPES.md §5's "Seeding" column) — a generic
                // min/max-seeded adapter names its parameters
                // "<typeId>.min"/"<typeId>.max"; adapt.map is the exception —
                // it seeds from BOTH ends at once (its own four-range
                // inMin/inMax/outMin/outMax shape). Its INPUT range comes
                // from whatever actually feeds it: the original source for a
                // one-step chain, the previous adapter's own output in a
                // two-step one (mix.downmix -> adapt.map: ±1). Every
                // other step still seeds itself independently from the
                // ORIGINAL endpoints' own ranges (fromPort/toPort).
                if (step.typeId == "adapt.map")
                {
                    std::vector<bazalt::engine::PortDescriptor> feederStorage;
                    const bazalt::engine::PortDescriptor* feeder = fromPort;
                    if (i > 0)
                        if (auto previous = factory.create (connectivity.adapterChain[(size_t) (i - 1)].typeId))
                            for (const auto& port : (feederStorage = previous->getOutputPorts()))
                                if (port.id == connectivity.adapterChain[(size_t) (i - 1)].outputPortId)
                                    feeder = &port;

                    if (const auto range = seedRangeForMapInput (*feeder))
                    {
                        instance.parameters["adapt.map.inMin"] = range->first;
                        instance.parameters["adapt.map.inMax"] = range->second;
                    }
                    // The same reading for the destination: its bounds, else
                    // its polarity, else ±1 for an audio port.
                    if (const auto range = seedRangeForMapInput (*toPort))
                    {
                        instance.parameters["adapt.map.outMin"] = range->first;
                        instance.parameters["adapt.map.outMax"] = range->second;
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

                if (i == 0 && choiceParameter.has_value())
                    instance.parameters[choiceParameter->first] = choiceParameter->second;

                g.addNode (std::move (instance));
                g.addConnection ({ currentFromNodeId, currentFromPortId, adapterId, step.inputPortId });

                currentFromNodeId = adapterId;
                currentFromPortId = step.outputPortId;
            }

            replaceExistingInputConnection (g, toNodeId, toPortId);
            g.addConnection ({ currentFromNodeId, currentFromPortId, toNodeId, toPortId });
        });
    }

    GraphEditController::CommandResult GraphEditController::createMacro (
        const juce::String& macroNodeId, float x, float y,
        int slot, float min, float max, bool isInteger, int quantity,
        const juce::String& unit, float value, int type)
    {
        if (macroNodeId.isEmpty())
            return { false, "Node id must not be empty" };

        if (graph.findNode (macroNodeId) != nullptr)
            return { false, "Node id already exists: " + macroNodeId };

        if (slot < -1 || slot > 31)
            return { false, "Macro slot must be -1 (unclaimed) or in [0, 31]" };

        return applyBatch ([&] (bazalt::engine::NodeGraph& g)
        {
            bazalt::engine::NodeInstance instance;
            instance.id = macroNodeId;
            instance.type = "util.macro";
            instance.position = { x, y };
            instance.parameters = {
                { "util.macro.slot", (float) slot },
                { "util.macro.type", (float) type },
                { "util.macro.min", min },
                { "util.macro.max", max },
                { "util.macro.isInteger", isInteger ? 1.0f : 0.0f },
                { "util.macro.quantity", (float) quantity },
                { "util.macro.value", value },
            };
            if (unit.isNotEmpty())
                instance.properties["util.macro.unit"] = unit;

            g.addNode (std::move (instance));
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

    bool GraphEditController::isLiveEditable (const juce::String& nodeId, const juce::String& parameterId) const
    {
        const auto* instance = graph.findNode (nodeId);
        if (instance == nullptr)
            return false;
        const auto node = processor.getNodeFactory().create (instance->type);
        if (node == nullptr)
            return false;
        for (const auto& port : node->getInputPorts())
            if (port.id == parameterId)
                return port.hasFallbackWhenUnconnected;
        for (const auto& parameter : node->getParameters())
            if (parameter.id == parameterId)
                return ! parameter.isStructural;
        return false;
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

        // A decoration's text, size or colour changes nothing a plan sees —
        // no recompile while someone types a comment.
        if (processor.getNodeFactory().isDecoration (node->type))
        {
            node->properties[propertyKey] = std::move (value);
            return { true, {} };
        }

        const auto previousGraph = graph;
        node->properties[propertyKey] = std::move (value);

        auto result = recompileAndPublish();
        if (! result.success)
            graph = previousGraph;

        return result;
    }

    GraphEditController::CommandResult GraphEditController::addImage (const juce::String& nodeId, float x, float y,
                                                                       const juce::String& mimeType, const juce::String& base64,
                                                                       float width, float height)
    {
        if (graph.findNode (nodeId) != nullptr)
            return { false, "Node id already exists: " + nodeId };
        if (! mimeType.startsWith ("image/") || base64.isEmpty())
            return { false, "Not an image" };

        const auto assetId = bazalt::engine::NodeGraph::contentIdFor (base64);
        const bazalt::engine::PatchAsset asset { mimeType, base64 };

        bazalt::engine::NodeInstance instance;
        instance.id = nodeId;
        instance.type = "deco.image";
        instance.position = { x, y };
        instance.properties["asset"] = assetId;
        instance.properties["width"] = width;
        instance.properties["height"] = height;

        // Measured as the patch would be — the same image used again costs nothing.
        auto proposed = graph;
        proposed.setAsset (assetId, asset);
        proposed.addNode (instance);
        const auto total = proposed.referencedAssetBytes();
        if (total > maxPatchAssetBytes)
            return { false, "Image refused: the patch's images would take " + juce::String ((double) total / (1024.0 * 1024.0), 1)
                                 + " MB, over the " + juce::String ((int) (maxPatchAssetBytes / (1024 * 1024))) + " MB limit" };

        return applyBatch ([&] (bazalt::engine::NodeGraph& g)
        {
            g.setAsset (assetId, asset);
            g.addNode (instance);
        });
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

        // wiki/plans/UtilMacro.md Batch 2: checked first, before any real
        // compile work — see macroSlotCollisionError()'s own comment.
        if (const auto collisionError = macroSlotCollisionError (graph); collisionError.isNotEmpty())
            return { false, collisionError };

        // Decorations never reach the resolver or a plan (wiki/plans/Decorations.md).
        auto compiled = bazalt::engine::withoutDecorations (graph, processor.getNodeFactory());

        // A wired Listen overrides Master Out (wiki/ROADMAP.md stage 0): the
        // compiled graph's designated output becomes whatever feeds it. The
        // edited graph keeps its own output, so removing the Listen goes
        // straight back to Master Out. At most one Listen exists (the UI
        // replaces any other); if several do, the first wired one wins.
        for (const auto& node : compiled.getNodes())
        {
            if (node.type != "view.listen")
                continue;
            const auto& connections = compiled.getConnections();
            const auto feed = std::find_if (connections.begin(), connections.end(),
                                            [&node] (const bazalt::engine::Connection& c) { return c.toNodeId == node.id && c.toPortId == "in"; });
            if (feed != connections.end())
            {
                const auto fromNodeId = feed->fromNodeId;
                const auto fromPortId = feed->fromPortId;
                compiled.setOutput (fromNodeId, fromPortId);
                break;
            }
        }

        auto split = bazalt::engine::MultiplicityResolver::split (compiled);
        if (! split.success)
            return { false, split.errorMessage };

        // A graph with a real instance.sum but no instance.allocate.voice
        // anywhere (every one currently unwired, since a wired one with no
        // Poly source is rejected by the resolver itself) has nothing to
        // run per-voice at all — treated exactly like monoOnly for the
        // runtime's purposes (one always-on global plan, no voice plans).
        const auto effectiveMonoOnly = split.monoOnly || split.origins.empty();

        // 09-28-InstanceAllocator arc: computed once here, uniformly, for
        // whichever of the branches below split actually took, rather than
        // duplicating "which nodes ended up where" per branch. A plain
        // debugging aid (getNodeDomains(), the UI's DomainDot). Kept LOCAL
        // until a real publish succeeds, deliberately not written straight
        // into the `nodeDomains` member here: `split` succeeding only means
        // the GRAPH partitions cleanly, not that GraphCompiler can actually
        // compile either half - several return paths below still fail
        // after this point, and CLAUDE.md rule 5's rollback contract means
        // the member must keep reflecting the last graph that ACTUALLY
        // published, not one that was merely attempted. A node the
        // resolver duplicated into one or more origins' voiceGraph AND
        // globalGraph (DomainRedesign.md §10.2 step 6 — no longer mutually
        // exclusive, unlike DomainSplitter) labels "voice": that's the more
        // actionable fact to surface on the UI dot. Every active origin's
        // own voiceGraph contributes, not just one — a node can only ever
        // be a real member of exactly one origin's voiceGraph in practice
        // (the origin-mismatch check already rejects the only way it could
        // be two), so this never actually overwrites one origin's label
        // with another's.
        std::unordered_map<juce::String, juce::String> newNodeDomains;
        if (effectiveMonoOnly)
        {
            for (const auto& node : split.globalGraph.getNodes())
                newNodeDomains[node.id] = "mono";
        }
        else
        {
            for (const auto& node : split.globalGraph.getNodes())
                newNodeDomains[node.id] = "global";
            for (const auto& origin : split.origins)
                for (const auto& node : origin.voiceGraph.getNodes())
                    newNodeDomains[node.id] = "voice";
        }

        const bazalt::engine::NodePrepareInfo prepareInfo { sampleRate, blockSize };
        auto& factory = processor.getNodeFactory();

        // M21 (DOMAINS.md §7): a graph with no active origin has no poly
        // region, so the whole graph compiles ONCE and is published as the one
        // global plan, which the processor runs every block. No voice plans are
        // touched: every origin bundle is deactivated, and the next non-mono
        // edit recompiles and republishes every one of them fresh.
        if (effectiveMonoOnly)
        {
            const auto* previousPlan = processor.getGlobalPlanSwapper().peekCurrentPlan();

            auto monoCompile = bazalt::engine::GraphCompiler::compile (
                split.globalGraph, factory, prepareInfo, nextGeneration(), previousPlan);

            if (! monoCompile.success)
                return { false, monoCompile.errorMessage };

            auto monoPlan = std::make_unique<bazalt::engine::ExecutionPlan> (std::move (monoCompile.plan));
            auto newStereoOutputs = monoPlan->stereoOutputPortsByNode;

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
            processor.setOutputOriginBundleIndex (-1);
            processor.setMonoOnly (true); // after the plan is live, so the audio thread never sees the flag first
            processor.commitOriginBundleAssignments ({}); // every slot empty — no origin exists
            nodeDomains = std::move (newNodeDomains);

            std::unordered_map<juce::String, std::unordered_map<juce::String, PortMultiplicityInfo>> newPortMultiplicity;
            markAllPorts (newPortMultiplicity, factory, split.globalGraph, "scalar", {});
            portMultiplicity = std::move (newPortMultiplicity);
            originBundleIndexByNodeId.clear();
            stereoOutputs = std::move (newStereoOutputs);

            processor.setMacroMappings (deriveMacroMappings (graph));

            return { true, {} };
        }

        // wiki/plans/DomainRedesign.md Batch 2: which numbered bundle slot
        // each of this compile's origins will occupy — computed up front,
        // read-only, so every compile below can reuse the RIGHT slot's
        // previously-published plans for state-pool continuity, and so the
        // eventual commit (only once everything below succeeds) is a
        // single, simple pass.
        const auto slotForOrigin = assignOriginBundleSlots (split.origins, processor);

        // origin voice plans, keyed by bundle slot (only the slots this
        // compile actually uses are populated) — kept as raw owning arrays
        // rather than one flat vector so a failure partway through never
        // needs to guess which entries belong to which origin.
        std::array<std::array<std::unique_ptr<bazalt::engine::ExecutionPlan>, BazaltAudioProcessor::numVoices>, BazaltAudioProcessor::maxOrigins> newVoicePlansBySlot;

        // wiki/plans/DomainRedesign.md Batch 4: this origin's own
        // "instance.allocate.voice.maxInstances", read off the REAL
        // compiled node (whatever the graph's own NodeInstance::parameters
        // actually applied, default included) — captured per-slot here
        // and enforced (VoiceManager::setMaxActiveVoices) only once every
        // compile below has succeeded, at the same commit point everything
        // else in this function already waits for.
        std::array<int, BazaltAudioProcessor::maxOrigins> maxInstancesBySlot {};

        // Domain Extensions batch: -1 (default) for every slot unless a
        // swarmPopulation origin is found there below — see
        // BazaltAudioProcessor::OriginBundle::swarmPopulationSize's own doc
        // comment for what this actually changes in renderOriginVoiceRange.
        std::array<int, BazaltAudioProcessor::maxOrigins> swarmPopulationSizeBySlot {};
        swarmPopulationSizeBySlot.fill (-1);

        for (const auto& origin : split.origins)
        {
            const auto slot = bundleSlotOf (slotForOrigin, origin.originId);
            jassert (slot >= 0); // assignOriginBundleSlots() always places every origin it's given

            // Real, found-live bug (direct feedback: "when I disconnect
            // scale quantize from Voice it sometimes gets stuck on gate
            // being 1"): InstanceVoiceNode's own gate/pitch/velocity state
            // only ever changes on a Note-block start/stop edge
            // (consumeNoteBlock() -> noteOn()/noteOff(), InstanceVoiceNode.h).
            // Once nothing feeds "spawn" at all, consumeNoteBlock() is
            // never called again, so a gate that was true the instant the
            // wire disappeared stays true forever — and M17's own
            // state-pool reuse then carries that exact stuck object
            // forward across every LATER recompile too, since removing an
            // unrelated incoming connection never changes this node's own
            // (id, type, parameters). Computed once per origin (a static
            // fact about origin.voiceGraph's own connection list, the same
            // for every voice lane's compiled copy below), not per lane.
            // Checks both "spawn" (Voice/Swarm-transient's own input id) and
            // "trigger" (Trigger's own, Domain Extensions Batch 4) — the two
            // event-driven origin types' own spawn-source port ids.
            const auto spawnConnected = std::any_of (
                origin.voiceGraph.getConnections().begin(), origin.voiceGraph.getConnections().end(),
                [&origin] (const bazalt::engine::Connection& c)
                {
                    return c.toNodeId == origin.originId
                           && (c.toPortId == "spawn" || c.toPortId == "trigger");
                });

            for (int i = 0; i < BazaltAudioProcessor::numVoices; ++i)
            {
                // M17 state pool: this voice's currently-published plan (if
                // any) is passed as `previousPlan` so GraphCompiler can reuse
                // unchanged nodes' DSP state — peekCurrentPlan(), not
                // getCurrentPlanForAudioThread() (see PlanSwapper.h's own
                // comment on why that distinction matters here). Reads from
                // the SAME slot this origin already occupies (or a freshly
                // empty one for a brand-new origin) — never a different
                // origin's own plans.
                const auto* previousPlan = processor.getOriginVoicePlanSwapper (slot, i).peekCurrentPlan();

                auto compileResult = bazalt::engine::GraphCompiler::compile (
                    origin.voiceGraph, factory, prepareInfo, nextGeneration(), previousPlan);

                if (! compileResult.success)
                    return { false, compileResult.errorMessage };

                newVoicePlansBySlot[(size_t) slot][(size_t) i] =
                    std::make_unique<bazalt::engine::ExecutionPlan> (std::move (compileResult.plan));

                // One summed output per instance.sum reducing this origin:
                // the first is the plan's own final output, every further
                // one is read straight off its feeding port's buffer.
                {
                    auto& plan = *newVoicePlansBySlot[(size_t) slot][(size_t) i];
                    plan.sumOutputs[0] = { plan.finalOutputBufferIndex, plan.finalOutputBufferIndexRight };
                    plan.numSumOutputs = 1;
                    for (size_t s = 1; s < origin.sums.size(); ++s)
                    {
                        const auto& sum = origin.sums[s];
                        const auto left = plan.findOutputBufferIndex (sum.fromNodeId, sum.fromPortId);
                        if (left < 0)
                            return { false, "Voice Sum '" + sum.sumNodeId + "' is fed from inside a feedback loop — "
                                                "take its input from outside the loop" };
                        plan.sumOutputs[s] = { left, plan.findRightOutputBufferIndex (sum.fromNodeId, sum.fromPortId) };
                        plan.numSumOutputs = (int) s + 1;
                    }
                }

                // Domain Extensions batch: InstanceOriginNode*, not the
                // concrete InstanceVoiceNode* this used to be — Swarm-
                // transient/Trigger need this same maxInstances capture +
                // stuck-gate cleanup, not just Voice. getMaxInstances()/
                // getGate()/releaseInstance() are all real InstanceOriginNode
                // interface methods now.
                if (auto* allocator = dynamic_cast<bazalt::engine::nodes::InstanceOriginNode*> (
                        newVoicePlansBySlot[(size_t) slot][(size_t) i]->getNodeById (origin.originId)))
                {
                    if (i == 0)
                        maxInstancesBySlot[(size_t) slot] = allocator->getMaxInstances();

                    // Only when genuinely stuck (still gated, with no way
                    // left to ever hear a stop edge) — never spam a
                    // spurious release on every recompile of an ordinary,
                    // never-wired-up allocator, which would incorrectly
                    // read as a real transition to PluginProcessor's own
                    // internal-trigger detection (InstanceVoiceNode.h's
                    // consumeSpawnEventsThisBlock() doc comment). spawnConnected
                    // (above) already checks both "spawn" and "trigger".
                    if (! spawnConnected && allocator->getGate())
                        allocator->releaseInstance();
                }
                // Domain Extensions batch: a swarm-population origin has no
                // noteOn()-driven instanceIndex at all (nothing ever spawns
                // it — "fixed count, always live") — this is the one and
                // only place anything tells each of the N physically-
                // compiled copies which slot it is, needed so
                // populationSize/seed can differ each copy's own
                // gate/random/position deterministically. See
                // InstanceSwarmPopulationNode.h's own doc comment.
                else if (auto* swarm = dynamic_cast<bazalt::engine::nodes::InstanceSwarmPopulationNode*> (
                             newVoicePlansBySlot[(size_t) slot][(size_t) i]->getNodeById (origin.originId)))
                {
                    swarm->setInstanceSlot (i);
                    if (i == 0)
                        swarmPopulationSizeBySlot[(size_t) slot] = swarm->getPopulationSize();
                }
            }
        }

        std::unique_ptr<bazalt::engine::ExecutionPlan> newGlobalPlan;
        // Compile globalGraph whenever it has real content — NOT only when
        // split.hasGlobalDomain is true. Those are different questions
        // under this resolver (DomainRedesign.md §10.2 step 6): hasGlobalDomain
        // says whether the AUDIO PATH needs the global plan's own output to
        // override some origin's raw voice sum (unchanged meaning, still
        // gates processor.setHasGlobalDomain() below); it says nothing about
        // whether globalGraph is EMPTY. When the designated output itself
        // resolved Poly (hasGlobalDomain false) there can still be real,
        // unrelated Scalar content elsewhere in the graph (an origin's own
        // trigger source never leaks in here — the resolver's own
        // globalMembers rule excludes it — but an ordinary orphan node, or
        // one mid-construction, does) that still needs to actually compile
        // — skipping it here used to mean GraphCompiler's own validation
        // (an invalid port, a growable-group overflow, ...) never ran on
        // it at all, silently accepting a malformed edit. It's compiled and
        // published regardless (harmless: the audio thread never runs it
        // while hasGlobalDomain is false, exactly as before this fix), so
        // that malformed content still fails the compile and rolls back.
        // wiki/plans/StereoChannels.md: each origin's instance.sum carries as
        // many channels as that origin's voices produce, known only now that
        // the voice plans are compiled.
        for (const auto& origin : split.origins)
        {
            const auto slot = bundleSlotOf (slotForOrigin, origin.originId);
            if (slot < 0)
                continue;
            const auto& voicePlan = newVoicePlansBySlot[(size_t) slot][0];
            if (voicePlan == nullptr)
                continue;
            for (size_t s = 0; s < origin.sums.size(); ++s)
                if (auto* sumNode = split.globalGraph.findNode (origin.sums[s].sumNodeId))
                    sumNode->parameters[bazalt::engine::nodes::InstanceMixNode::channelsParameterId] =
                        voicePlan->sumOutputs[s].right >= 0 ? 2.0f : 1.0f;
        }

        if (! split.globalGraph.getNodes().empty())
        {
            // Real, found-live bug (direct feedback: "moving a node's
            // position restarts the whole sound"): unlike the per-voice
            // compile above, this call never passed `previousPlan` at all -
            // every global-domain node (everything from instance.sum
            // onward: pan, output, any post-mix effect) got a completely
            // fresh Node instance on EVERY recompile, discarding whatever
            // state it held, on every single edit including a plain move.
            // The per-voice loop above already gets this right; this branch
            // just never had its own matching peekCurrentPlan() call.
            const auto* previousGlobalPlan = processor.getGlobalPlanSwapper().peekCurrentPlan();

            ensureGlobalGraphHasAValidOutput (split.globalGraph, factory);

            auto globalCompileResult = bazalt::engine::GraphCompiler::compile (
                split.globalGraph, factory, prepareInfo, nextGeneration(), previousGlobalPlan);

            if (! globalCompileResult.success)
                return { false, globalCompileResult.errorMessage };

            newGlobalPlan = std::make_unique<bazalt::engine::ExecutionPlan> (std::move (globalCompileResult.plan));

            // One shared global plan can now carry several origins'
            // instance.sum nodes at once (DomainRedesign.md §4's multiple
            // independent voice regions) — each hand-off is keyed by that
            // origin's own stable bundle slot, the same index the audio
            // thread already has for that origin's own scratch buffer.
            for (const auto& origin : split.origins)
            {
                const auto slot = bundleSlotOf (slotForOrigin, origin.originId);
                if (slot < 0)
                    continue;

                for (size_t s = 0; s < origin.sums.size(); ++s)
                    newGlobalPlan->externalInputNodeIds[(size_t) slot][s] = origin.sums[s].sumNodeId;
            }
        }

        // ADR-0029: re-attach live preview taps to the new plans before any of
        // them is published - until then the audio thread cannot see them, so
        // this cannot race. Without it every edit silently detached every
        // preview (a tap pointer lives on a plan, and every edit builds new ones).
        {
            std::vector<bazalt::engine::ExecutionPlan*> voicePlanPointers;
            voicePlanPointers.reserve (split.origins.size() * (size_t) BazaltAudioProcessor::numVoices);

            for (const auto& origin : split.origins)
            {
                const auto slot = bundleSlotOf (slotForOrigin, origin.originId);
                for (auto& plan : newVoicePlansBySlot[(size_t) slot])
                    voicePlanPointers.push_back (plan.get());
            }

            processor.applyPreviewSubscriptions (voicePlanPointers, newGlobalPlan.get());
        }

        // A node lives in one origin's voice plans (identical across its
        // voices) or in the global plan; collect before the plans are handed off.
        std::unordered_map<juce::String, std::vector<juce::String>> newStereoOutputs;
        for (const auto& origin : split.origins)
        {
            const auto slot = bundleSlotOf (slotForOrigin, origin.originId);
            if (slot < 0)
                continue;
            if (const auto& plan = newVoicePlansBySlot[(size_t) slot][0])
                for (const auto& [nodeId, ports] : plan->stereoOutputPortsByNode)
                    newStereoOutputs[nodeId] = ports;
        }
        if (newGlobalPlan)
            for (const auto& [nodeId, ports] : newGlobalPlan->stereoOutputPortsByNode)
                newStereoOutputs[nodeId] = ports;

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
        for (const auto& origin : split.origins)
        {
            const auto slot = bundleSlotOf (slotForOrigin, origin.originId);

            for (int i = 0; i < BazaltAudioProcessor::numVoices; ++i)
            {
                auto& swapper = processor.getOriginVoicePlanSwapper (slot, i);
                swapper.reclaim();
                const auto published = swapper.publish (std::move (newVoicePlansBySlot[(size_t) slot][(size_t) i]));
                jassert (published);
                juce::ignoreUnused (published);
            }
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
        processor.setOutputOriginBundleIndex (hasGlobalDomain ? -1 : bundleSlotOf (slotForOrigin, split.outputOriginId));
        processor.setMonoOnly (false); // after the voice (and global) plans are live
        processor.commitOriginBundleAssignments (slotForOrigin); // after everything above is live
        nodeDomains = std::move (newNodeDomains);

        // wiki/plans/DomainRedesign.md Batch 4: the instance-count badge's
        // "maxCount" half, actually enforced now (VoiceManager::
        // setMaxActiveVoices) rather than merely read back for display.
        for (const auto& origin : split.origins)
        {
            const auto slot = bundleSlotOf (slotForOrigin, origin.originId);
            if (slot >= 0)
                processor.setOriginMaxVoices (slot, maxInstancesBySlot[(size_t) slot]);
        }

        // Domain Extensions batch: ALL maxOrigins slots, unconditionally -
        // not just this compile's occupied ones (unlike the maxInstances
        // loop just above). A slot whose origin was just deleted, or
        // changed from swarmPopulation to Voice/something else, MUST have
        // this reset back to -1 here, or a stale >= 0 value would keep
        // forcing phantom always-on voices into what's now an ordinary
        // demand-driven origin - a real correctness bug, not just a cosmetic
        // staleness the way a stale maxInstances ceiling would be.
        for (int slot = 0; slot < BazaltAudioProcessor::maxOrigins; ++slot)
            processor.setSwarmPopulationSize (slot, swarmPopulationSizeBySlot[(size_t) slot]);

        // DomainDot's real replacement — per-port, not per-node (§2.4's two
        // boundary node types have real mixed per-port shapes).
        std::unordered_map<juce::String, std::unordered_map<juce::String, PortMultiplicityInfo>> newPortMultiplicity;
        std::unordered_map<juce::String, int> newOriginBundleIndexByNodeId;

        markAllPorts (newPortMultiplicity, factory, split.globalGraph, "scalar", {});
        for (const auto& origin : split.origins)
        {
            markAllPorts (newPortMultiplicity, factory, origin.voiceGraph, "poly", origin.originId);
            const auto slot = bundleSlotOf (slotForOrigin, origin.originId);
            if (slot >= 0)
                newOriginBundleIndexByNodeId[origin.originId] = slot;

            // instance.sum's own "in" port is the one fixed exception: the
            // NODE lives in globalGraph (marked scalar above), but this ONE
            // port specifically reduces a real Poly signal.
            for (const auto& sum : origin.sums)
                newPortMultiplicity[sum.sumNodeId]["in"] = { "poly", origin.originId };
        }

        portMultiplicity = std::move (newPortMultiplicity);
        originBundleIndexByNodeId = std::move (newOriginBundleIndexByNodeId);
        stereoOutputs = std::move (newStereoOutputs);

        processor.setMacroMappings (deriveMacroMappings (graph));

        return { true, {} };
    }

    bazalt::engine::NodeGraph GraphEditController::getGraphForSaving() const
    {
        auto saved = graph;
        for (const auto& node : graph.getNodes())
            if (node.type == "view.listen")
                saved.removeNode (node.id);
        return saved;
    }

    GraphEditController::CommandResult GraphEditController::exportSnapshotToFile (const juce::File& file) const
    {
        const auto json = bazalt::engine::serializePatchToJson (
            bazalt::engine::PatchDocument::fromNodeGraph (getGraphForSaving()), true); // pretty-printed - a human/AI reads this file directly

        const auto parentDir = file.getParentDirectory();
        if (! parentDir.exists() && ! parentDir.createDirectory())
            return { false, "Could not create export directory: " + parentDir.getFullPathName() };

        if (! file.replaceWithText (json))
            return { false, "Could not write export file: " + file.getFullPathName() };

        return { true, {} };
    }
}
