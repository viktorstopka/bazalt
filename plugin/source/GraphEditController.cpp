#include "GraphEditController.h"
#include "bazalt/engine/nodes/LifePopulationNode.h"
#include <optional>
#include "PluginProcessor.h"
#include "bazalt/engine/graph/CanConnect.h"
#include "bazalt/engine/graph/MultiplicityResolver.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/PortGroups.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/Unwrap.h"
#include "bazalt/engine/graph/InstanceExpansion.h"
#include "bazalt/engine/nodes/InstanceMixNode.h"
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

        // wiki/plans/UtilMacro.md Batch 2: which numbered host slot (0-31)
        // a "util.macro" node claims, or -1 if it's the unclaimed sentinel
        // (MacroNode.h's own doc comment on why -1, not a real slot
        // number, is the declared default) or the parameter is somehow
        // missing entirely — both treated identically to "unclaimed".
        // A Trigger macro is a moment, not a position: nothing to remember.
        bool isTriggerMacro (const bazalt::engine::NodeInstance& node)
        {
            const auto type = node.parameters.find ("util.macro.type");
            return type != node.parameters.end() && type->second >= 1.5f;
        }

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
            if (feeder.quantity == bazalt::engine::Quantity::Bipolar || feeder.quantity == bazalt::engine::Quantity::Audio)
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
                if (step.typeId == "math.map")
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
                        instance.parameters["math.map.inMin"] = range->first;
                        instance.parameters["math.map.inMax"] = range->second;
                    }
                    // The same reading for the destination: its bounds, else
                    // its polarity, else ±1 for an audio port.
                    if (const auto range = seedRangeForMapInput (*toPort))
                    {
                        instance.parameters["math.map.outMin"] = range->first;
                        instance.parameters["math.map.outMax"] = range->second;
                    }
                    // …and its meaning, when the destination has a real one, so
                    // the Map's output can go on through Add / Multiply.
                    if (const auto q = (int) toPort->quantity; q > 0 && q <= 9 && toPort->quantity != bazalt::engine::Quantity::Unipolar
                                                                    && toPort->quantity != bazalt::engine::Quantity::Bipolar)
                        instance.parameters["math.map.quantity"] = (float) q;
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

    GraphEditController::CommandResult GraphEditController::setContent (const juce::String& nodeId, juce::var content)
    {
        auto* node = graph.findNode (nodeId);
        if (node == nullptr)
            return { false, "No such node: " + nodeId };

        const auto previousGraph = graph;
        node->content = std::move (content);

        auto result = recompileAndPublish();
        if (! result.success)
            graph = previousGraph;
        return result;
    }

    GraphEditController::CommandResult GraphEditController::setContentLive (const juce::String& nodeId, const juce::var& content)
    {
        if (graph.findNode (nodeId) == nullptr)
            return { false, "No such node: " + nodeId };

        auto apply = [&] (const bazalt::engine::ExecutionPlan* plan)
        {
            if (plan != nullptr)
                if (auto* running = plan->getNodeById (nodeId))
                    running->setContent (content);
        };
        apply (processor.getGlobalPlanSwapper().peekCurrentPlan());
        return { true, {} };
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

        // A whole patch replaces the graph: nothing of the previous patch's
        // running state carries over, even where node ids happen to match
        // (every patch numbers its nodes the same way, so they often do).
        freshStateOnNextCompile = true;
        auto result = recompileAndPublish();
        freshStateOnNextCompile = false;
        if (! result.success)
        {
            graph = previousGraph;
            return result;
        }

        // A patch's macros open where they were saved (getGraphForSaving).
        for (const auto& node : graph.getNodes())
            if (const auto slot = macroSlotOf (node); slot >= 0 && slot < MacroParameters::numMacros && ! isTriggerMacro (node))
                if (const auto stored = node.parameters.find ("util.macro.default"); stored != node.parameters.end())
                    processor.getMacroParameter (slot).setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, stored->second));

        return result;
    }

    GraphEditController::CommandResult GraphEditController::unwrapNode (const juce::String& nodeId)
    {
        auto expanded = graph;
        const auto unwrapped = bazalt::engine::unwrapNode (expanded, nodeId);
        if (! unwrapped.success)
            return { false, unwrapped.error };
        return applyBatch ([&expanded] (bazalt::engine::NodeGraph& g) { g = std::move (expanded); });
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
        // run per instance at all — treated exactly like monoOnly (one plan,
        // no instance groups).
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

        return publishPlan (effectiveMonoOnly ? split.globalGraph : compiled, split, effectiveMonoOnly, std::move (newNodeDomains));
    }

    // The instance axis (wiki/plans/InstanceAxis.md): the whole patch compiles
    // into one plan; each allocator is a group of it, driven by its bundle's
    // VoiceManager (BazaltAudioProcessor::renderRange). A graph with no
    // allocator compiles as is: the same plan with no groups.
    GraphEditController::CommandResult GraphEditController::publishPlan (const bazalt::engine::NodeGraph& compiled,
                                                                          const bazalt::engine::MultiplicityResult& split,
                                                                          bool monoOnly,
                                                                          std::unordered_map<juce::String, juce::String> newNodeDomains)
    {
        auto& factory = processor.getNodeFactory();
        const bazalt::engine::NodePrepareInfo prepareInfo { sampleRate, blockSize };
        const auto slotForOrigin = monoOnly ? std::array<juce::String, BazaltAudioProcessor::maxOrigins> {}
                                            : assignOriginBundleSlots (split.origins, processor);
        // Each Population runs as many instances as its Size — read off a
        // scratch node with the patch's parameters.
        const auto instanceCountOf = [&factory, &compiled] (const juce::String& originId)
        {
            const auto* instance = compiled.findNode (originId);
            auto node = instance != nullptr ? factory.create (instance->type) : nullptr;
            if (node == nullptr)
                return 1;
            for (const auto& [id, value] : instance->parameters)
                node->setParameter (id, value);
            const auto* population = dynamic_cast<const bazalt::engine::nodes::LifePopulationNode*> (node.get());
            return population != nullptr ? juce::jlimit (1, BazaltAudioProcessor::maxInstances, population->getSize()) : 1;
        };
        const auto layout = monoOnly ? bazalt::engine::InstanceLayout {}
                                     : bazalt::engine::computeInstanceLayout (split, instanceCountOf);

        const auto* previousPlan = freshStateOnNextCompile ? nullptr : processor.getGlobalPlanSwapper().peekCurrentPlan();
        auto result = bazalt::engine::compileWithInstances (compiled, layout, factory, prepareInfo, nextGeneration(), previousPlan);
        if (! result.success)
            return { false, result.errorMessage };
        auto plan = std::make_unique<bazalt::engine::ExecutionPlan> (std::move (result.plan));

        std::array<int, BazaltAudioProcessor::maxOrigins> maxInstancesBySlot {};
        std::array<bazalt::engine::VoiceManager::StealPolicy, BazaltAudioProcessor::maxOrigins> stealPolicyBySlot {};
        stealPolicyBySlot.fill (bazalt::engine::VoiceManager::StealPolicy::Oldest);

        for (auto& runtime : plan->instanceGroups)
        {
            const auto slot = bundleSlotOf (slotForOrigin, runtime->originId);
            runtime->bundleSlot = slot;
            runtime->birthSink = processor.getBirthSink();
            if (slot < 0 || runtime->originNodes.empty())
                continue;
            if (const auto* population = dynamic_cast<const bazalt::engine::nodes::LifePopulationNode*> (runtime->originNodes[0]))
            {
                using Policy = bazalt::engine::VoiceManager::StealPolicy;
                using WhenFull = bazalt::engine::nodes::LifePopulationNode::WhenFull;
                const auto whenFull = population->getWhenFull();
                maxInstancesBySlot[(size_t) slot] = population->getSize();

                // Held members nothing can release any more (their note source
                // was just unwired) are released, never left sounding — on
                // the audio thread, at this plan's first block (a kept member
                // may still be running in the old plan).
                const auto& connections = compiled.getConnections();
                const auto wired = std::any_of (connections.begin(), connections.end(), [&runtime] (const bazalt::engine::Connection& c)
                                                { return c.toNodeId == runtime->originId
                                                         && (c.toPortId == "notes" || c.toPortId == "born" || c.toPortId == "release"); });
                if (! wired && population->getLives() == bazalt::engine::nodes::LifePopulationNode::Lives::Hold)
                    for (auto* node : runtime->originNodes)
                        if (auto* member = dynamic_cast<bazalt::engine::nodes::LifePopulationNode*> (node))
                            plan->pendingBindings.push_back ([member] { member->releaseInstance(); });
                plan->bindingsApplied.value.store (plan->pendingBindings.empty(), std::memory_order_relaxed);
                stealPolicyBySlot[(size_t) slot] = whenFull == WhenFull::Ignore          ? Policy::Ignore
                                                 : whenFull == WhenFull::StealQuietest ? Policy::Quietest
                                                                                       : Policy::Oldest;
            }
        }

        std::unordered_map<juce::String, std::vector<juce::String>> newStereoOutputs;
        for (const auto& [nodeId, ports] : plan->stereoOutputPortsByNode)
            newStereoOutputs[bazalt::engine::baseNodeId (nodeId)] = ports;

        // ADR-0029: re-attach live preview taps to the new plan while nothing
        // else can see it (a tap pointer lives on a plan).
        processor.applyPreviewSubscriptions (plan.get());

        auto& swapper = processor.getGlobalPlanSwapper();
        swapper.reclaim();
        const auto published = swapper.publish (std::move (plan));
        jassert (published);
        juce::ignoreUnused (published);

        processor.commitOriginBundleAssignments (slotForOrigin); // after the plan is live
        // What the resolver pulled into a voice region only to feed it (an LFO,
        // a constant) runs once: it is shown as shared, not as voices.
        for (auto& [nodeId, domain] : newNodeDomains)
            if (domain == "voice" && layout.groupOf (nodeId) < 0)
                domain = "global";
        nodeDomains = std::move (newNodeDomains);

        for (const auto& origin : split.origins)
            if (const auto slot = bundleSlotOf (slotForOrigin, origin.originId); slot >= 0)
            {
                processor.setOriginMaxVoices (slot, maxInstancesBySlot[(size_t) slot]);
                processor.setOriginStealPolicy (slot, stealPolicyBySlot[(size_t) slot]);
            }

        std::unordered_map<juce::String, std::unordered_map<juce::String, PortMultiplicityInfo>> newPortMultiplicity;
        std::unordered_map<juce::String, int> newOriginBundleIndexByNodeId;
        markAllPorts (newPortMultiplicity, factory, split.globalGraph, "scalar", {});
        for (const auto& origin : monoOnly ? std::vector<bazalt::engine::MultiplicityOrigin> {} : split.origins)
        {
            // Poly (drawn as a stack of cards) only for what really runs per
            // instance.
            bazalt::engine::NodeGraph perInstance, once;
            for (const auto& node : origin.voiceGraph.getNodes())
                (layout.groupOf (node.id) >= 0 ? perInstance : once).addNode (node);
            markAllPorts (newPortMultiplicity, factory, once, "scalar", {});
            markAllPorts (newPortMultiplicity, factory, perInstance, "poly", origin.originId);
            if (const auto slot = bundleSlotOf (slotForOrigin, origin.originId); slot >= 0)
                newOriginBundleIndexByNodeId[origin.originId] = slot;
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
        {
            if (node.type == "view.listen")
                saved.removeNode (node.id);

            // Every macro remembers where its knob is, so the patch opens the
            // way it was saved (setGraph puts it back).
            if (const auto slot = macroSlotOf (node); slot >= 0 && slot < MacroParameters::numMacros && ! isTriggerMacro (node))
                if (auto* stored = const_cast<bazalt::engine::NodeInstance*> (saved.findNode (node.id)))
                    stored->parameters["util.macro.default"] = static_cast<juce::AudioProcessorParameter&> (processor.getMacroParameter (slot)).getValue();
        }
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
