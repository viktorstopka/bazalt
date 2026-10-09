#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/CanConnect.h"
#include "bazalt/engine/graph/PortGroups.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace bazalt::engine
{
    namespace
    {
        struct PortKey
        {
            int slot;
            int port;

            bool operator== (const PortKey& other) const noexcept { return slot == other.slot && port == other.port; }
        };

        struct PortKeyHash
        {
            size_t operator() (const PortKey& key) const noexcept
            {
                return (std::hash<int>() (key.slot) * 31) ^ std::hash<int>() (key.port);
            }
        };

        // Where a (nodeSlot, portIndex) output currently lives, once its
        // owning step has been scheduled — either an ordinary block buffer
        // or (only while still inside its own region step) a region scalar.
        using OutputLocationMap = std::unordered_map<PortKey, ExecutionPlan::InputRef, PortKeyHash>;

        // M18 (ADR-0024): a Note-typed connection, tracked separately from
        // incomingSource/resolveInput — it's routed through
        // ExecutionPlan::noteBuffers instead of the ordinary float
        // blockBuffers/regionScalars path.
        struct NoteConnection
        {
            int fromSlot, fromPort, toSlot, toPort;
        };

        //==============================================================================
        // Real stereo cable redesign: a single Audio port marked
        // `Channels::Stereo` occupies TWO consecutive "flat" buffer/scalar
        // slots instead of one — everything else (Note/Control/Boolean/
        // Event/Data ports, and every Mono/Inherited Audio port) still
        // occupies exactly one, unchanged. `Node::processSample`/
        // `processBlock`'s flat `inputs`/`outputs` arrays are addressed by
        // this same flat index, so a node that declares a stereo port simply
        // reads/writes two adjacent array slots for it — no interface change
        // needed (`ExecutionPlan::process()` already iterates however many
        // entries GraphCompiler put in `BlockStep::inputs`/
        // `outputBufferIndices`, never `node->getNumInputPorts()` directly).
        //
        // This is the DECLARED width, used only for the per-node physical
        // port-count check below. The width a port actually carries in a
        // compile — an Inherited port on a lane-able node follows its source
        // (wiki/plans/StereoChannels.md) — is compile()'s own widthOf().
        int channelCountOf (const PortDescriptor& port) noexcept
        {
            return (port.type == SignalType::Signal && port.channels == Channels::Stereo) ? 2 : 1;
        }

        // One entry per descriptor: the flat index its first channel starts
        // at (its second channel, if any, is always startIndex+1 — declared
        // stereo ports are never split apart from each other).
        std::vector<int> computeFlatStarts (const std::vector<PortDescriptor>& ports)
        {
            std::vector<int> starts (ports.size());
            int running = 0;
            for (size_t i = 0; i < ports.size(); ++i)
            {
                starts[i] = running;
                running += channelCountOf (ports[i]);
            }
            return starts;
        }

        int totalFlatCount (const std::vector<PortDescriptor>& ports)
        {
            int total = 0;
            for (const auto& p : ports)
                total += channelCountOf (p);
            return total;
        }

        //==============================================================================
        // Tarjan's SCC algorithm on the node-level dependency graph
        // (successors[v] = nodes v directly feeds). Emits SCCs in reverse
        // topological order relative to data flow; caller reverses.
        void tarjanDfs (int v,
                         const std::vector<std::vector<int>>& successors,
                         std::vector<int>& index,
                         std::vector<int>& lowlink,
                         std::vector<bool>& onStack,
                         std::vector<int>& stack,
                         int& nextIndex,
                         std::vector<std::vector<int>>& sccsOut)
        {
            index[(size_t) v] = lowlink[(size_t) v] = nextIndex++;
            stack.push_back (v);
            onStack[(size_t) v] = true;

            for (int w : successors[(size_t) v])
            {
                if (index[(size_t) w] == -1)
                {
                    tarjanDfs (w, successors, index, lowlink, onStack, stack, nextIndex, sccsOut);
                    lowlink[(size_t) v] = std::min (lowlink[(size_t) v], lowlink[(size_t) w]);
                }
                else if (onStack[(size_t) w])
                {
                    lowlink[(size_t) v] = std::min (lowlink[(size_t) v], index[(size_t) w]);
                }
            }

            if (lowlink[(size_t) v] == index[(size_t) v])
            {
                std::vector<int> scc;
                int w;
                do
                {
                    w = stack.back();
                    stack.pop_back();
                    onStack[(size_t) w] = false;
                    scc.push_back (w);
                } while (w != v);

                sccsOut.push_back (std::move (scc));
            }
        }

        std::vector<std::vector<int>> computeStronglyConnectedComponents (int numNodes,
                                                                           const std::vector<std::vector<int>>& successors)
        {
            std::vector<int> index ((size_t) numNodes, -1);
            std::vector<int> lowlink ((size_t) numNodes, -1);
            std::vector<bool> onStack ((size_t) numNodes, false);
            std::vector<int> stack;
            int nextIndex = 0;
            std::vector<std::vector<int>> sccs;

            for (int v = 0; v < numNodes; ++v)
                if (index[(size_t) v] == -1)
                    tarjanDfs (v, successors, index, lowlink, onStack, stack, nextIndex, sccs);

            std::reverse (sccs.begin(), sccs.end());
            return sccs;
        }

        // Topological order of a region's internal nodes, treating any edge
        // to an already-on-stack node as a back edge and simply not
        // following it (standard DFS postorder-reversal restricted to the
        // region). The one edge that closes the cycle ends up "backwards"
        // in the result, which is exactly what makes the ExecutionPlan's
        // stale-scalar-read trick produce the correct one-sample delay.
        std::vector<int> computeRegionInternalOrder (const std::vector<int>& sccNodes,
                                                      const std::vector<std::vector<int>>& successors)
        {
            std::unordered_set<int> inScc (sccNodes.begin(), sccNodes.end());
            std::unordered_map<int, int> state; // 0/absent = unvisited, 1 = on stack, 2 = done
            std::vector<int> postOrder;

            std::function<void (int)> dfs = [&] (int v)
            {
                state[v] = 1;

                for (int w : successors[(size_t) v])
                {
                    if (inScc.find (w) == inScc.end())
                        continue; // edge leaves the region — not this function's concern

                    const auto it = state.find (w);
                    if (it == state.end())
                        dfs (w);
                    // it->second == 1 (on stack): back edge, skip — this is
                    // the cycle-closing edge.
                    // it->second == 2 (done): already reachable via another
                    // path; nothing to do.
                }

                state[v] = 2;
                postOrder.push_back (v);
            };

            for (int v : sccNodes)
                if (state.find (v) == state.end())
                    dfs (v);

            std::reverse (postOrder.begin(), postOrder.end());
            return postOrder;
        }
    }

    namespace
    {
        // Port-id -> index lookup for one node's inputs or outputs, built
        // once per slot at compile time from its descriptors — never
        // touched by the audio thread, same cost class as everything else
        // this function does before publishing a plan.
        struct PortIdIndex
        {
            std::unordered_map<juce::String, int> inputIndexById;
            std::unordered_map<juce::String, int> outputIndexById;
        };

        PortIdIndex buildPortIdIndex (const Node& node)
        {
            PortIdIndex index;
            const auto inputs = node.getInputPorts();
            const auto outputs = node.getOutputPorts();

            for (int i = 0; i < (int) inputs.size(); ++i)
                index.inputIndexById[inputs[(size_t) i].id] = i;
            for (int i = 0; i < (int) outputs.size(); ++i)
                index.outputIndexById[outputs[(size_t) i].id] = i;

            return index;
        }
    }

    namespace
    {
        // The (SignalType, Quantity) of every port of one node, inputs then
        // outputs — what polymorphic resolution can change, and so what "did
        // this node change" means for the fixed-point loop.
        std::vector<std::pair<int, int>> portSignature (const std::vector<PortDescriptor>& inputs, const std::vector<PortDescriptor>& outputs)
        {
            std::vector<std::pair<int, int>> signature;
            signature.reserve (inputs.size() + outputs.size());
            for (const auto& port : inputs)
                signature.emplace_back ((int) port.type, (int) port.quantity);
            for (const auto& port : outputs)
                signature.emplace_back ((int) port.type, (int) port.quantity);
            return signature;
        }

        /** True when `before` -> `after` changes only values the audio thread
            can apply to a running node with setParameter(): a port's
            in-node fallback value, or a non-structural parameter (the same
            things a macro already modulates live). The same set of keys is
            required — a key appearing or disappearing means "back to a
            default" the node would have to be rebuilt to honour — and any
            structural parameter (a mode count, a table size, a waveform
            table) keeps the old fresh-node behaviour. */
        bool onlyLiveApplicableChanges (const Node* node,
                                        const std::unordered_map<juce::String, float>& before,
                                        const std::unordered_map<juce::String, float>& after)
        {
            if (node == nullptr || before.size() != after.size())
                return false;

            const auto parameters = node->getParameters();
            const auto inputs = node->getInputPorts();

            for (const auto& [id, value] : after)
            {
                const auto previous = before.find (id);
                if (previous == before.end())
                    return false;
                if (previous->second == value)
                    continue;

                const auto isFallbackPort = std::any_of (inputs.begin(), inputs.end(), [&id] (const PortDescriptor& port)
                                                         { return port.id == id && port.hasFallbackWhenUnconnected; });
                const auto isLiveParameter = std::any_of (parameters.begin(), parameters.end(), [&id] (const ParameterDescriptor& p)
                                                          { return p.id == id && ! p.isStructural; });
                if (! isFallbackPort && ! isLiveParameter)
                    return false;
            }
            return true;
        }
    }

    NodeGraph withoutDecorations (const NodeGraph& graph, const NodeFactory& factory)
    {
        NodeGraph stripped;
        for (const auto& node : graph.getNodes())
            if (! factory.isDecoration (node.type))
                stripped.addNode (node);
        for (const auto& connection : graph.getConnections())
            stripped.addConnection (connection);
        stripped.setOutput (graph.getOutputNodeId(), graph.getOutputPortId());
        return stripped;
    }

    CompileResult GraphCompiler::compile (const NodeGraph& graph,
                                           const NodeFactory& factory,
                                           const NodePrepareInfo& prepareInfo,
                                           uint64_t generation,
                                           const ExecutionPlan* previousPlan)
    {
        // Decorations are canvas-only (wiki/plans/Decorations.md): never
        // instantiated, never scheduled.
        if (std::any_of (graph.getNodes().begin(), graph.getNodes().end(),
                         [&factory] (const NodeInstance& n) { return factory.isDecoration (n.type); }))
            return compile (withoutDecorations (graph, factory), factory, prepareInfo, generation, previousPlan);

        CompileResult result;
        auto& plan = result.plan;

        const auto& graphNodes = graph.getNodes();
        const auto numNodes = (int) graphNodes.size();

        // ---- Instantiate nodes, assign slots -----------------------------
        plan.nodes.reserve ((size_t) numNodes);
        std::vector<PortIdIndex> portIdIndexBySlot ((size_t) numNodes);
        // Captured alongside portIdIndexBySlot, before each node is moved
        // into plan.nodes — resolveInputChannel() (below) reads each
        // unconnected input's own PortDescriptor to decide Silence vs. its
        // hasFallbackWhenUnconnected NaN-sentinel behaviour.
        std::vector<std::vector<PortDescriptor>> inputPortsBySlot ((size_t) numNodes);
        // Captured alongside inputPortsBySlot, for the same reason:
        // canConnect() (below) needs each connection's actual FROM port
        // descriptor, not just its resolved index.
        std::vector<std::vector<PortDescriptor>> outputPortsBySlot ((size_t) numNodes);
        // Real stereo cable redesign: one flat-slot start index per
        // descriptor, computed from inputPortsBySlot/outputPortsBySlot —
        // always kept in sync with them (recomputed anywhere those change,
        // including inside the polymorphic-port resolution loop below).
        std::vector<std::vector<int>> flatStartForInputBySlot ((size_t) numNodes);
        std::vector<std::vector<int>> flatStartForOutputBySlot ((size_t) numNodes);
        // Whether each slot's node was carried over from previousPlan, and the
        // values that changed on it — its extra channel lanes (below) are
        // reused under exactly the same rule.
        std::vector<char> reusedBySlot ((size_t) numNodes, 0);
        std::vector<std::vector<std::pair<juce::String, float>>> parameterChangesBySlot ((size_t) numNodes);

        // Every input port ID each node is connected to, indexed once here so
        // growable-group sizing (PortGroups.h) doesn't rescan the whole
        // connection list per node.
        std::unordered_map<juce::String, std::vector<juce::String>> incomingPortIdsByNode;
        for (const auto& connection : graph.getConnections())
            incomingPortIdsByNode[connection.toNodeId].push_back (connection.toPortId);

        const auto incomingPortIdsFor = [&incomingPortIdsByNode] (const juce::String& nodeId) -> const std::vector<juce::String>&
        {
            static const std::vector<juce::String> none;
            const auto it = incomingPortIdsByNode.find (nodeId);
            return it == incomingPortIdsByNode.end() ? none : it->second;
        };

        for (int slot = 0; slot < numNodes; ++slot)
        {
            const auto& instance = graphNodes[(size_t) slot];

            if (plan.nodeIdToSlot.find (instance.id) != plan.nodeIdToSlot.end())
            {
                result.errorMessage = "Duplicate node id: " + instance.id;
                return result;
            }

            // M17 state pool (ARCHITECTURE.md §3.2, GraphCompiler.h's own
            // doc comment on `previousPlan`): reuse the exact same Node
            // object — carrying its DSP state forward — ONLY when this
            // instance's id, type, AND current parameters are all
            // identical to the previous plan's. That last condition is
            // load-bearing, not an optimization: a reused node's shared_ptr
            // may still be reachable from the audio thread (the previous
            // plan isn't provably dead until PlanSwapper's epoch-gated
            // reclaim() says so), so the compiler thread must never mutate
            // it after publish — calling setParameter() on a reused node
            // would race the audio thread's own processSample() calls on
            // that SAME object (ExecutionPlan.h's own comment has the full
            // story, including the real bug this fixes). Any parameter
            // difference at all means a fresh node instead: safe, and
            // exactly today's pre-M17 behaviour for that node.
            std::shared_ptr<Node> node;
            bool reused = false;

            if (previousPlan != nullptr)
            {
                const auto typeIt = previousPlan->nodeIdToType.find (instance.id);
                const auto paramsIt = previousPlan->nodeIdToAppliedParameters.find (instance.id);

                if (typeIt != previousPlan->nodeIdToType.end() && typeIt->second == instance.type
                    && paramsIt != previousPlan->nodeIdToAppliedParameters.end()
                    && (paramsIt->second == instance.parameters
                        || onlyLiveApplicableChanges (previousPlan->nodes[(size_t) previousPlan->nodeIdToSlot.at (instance.id)].get(),
                                                      paramsIt->second, instance.parameters)))
                {
                    const auto slotIt = previousPlan->nodeIdToSlot.find (instance.id);
                    if (slotIt != previousPlan->nodeIdToSlot.end())
                    {
                        node = previousPlan->nodes[(size_t) slotIt->second];

                        // A polymorphic-port node (RerouteNode) is never
                        // reused, even with an identical id/type/params:
                        // the resolution pass below mutates it via
                        // resolveIncomingPort(), which would break
                        // the "never mutate a reused node" rule above, AND
                        // a reused one would keep whatever type the LAST
                        // graph resolved it to even if its input has since
                        // been disconnected (fresh-compile default is
                        // Audio, so the same graph would then compile
                        // differently depending on edit history). Such a
                        // node holds no DSP state worth carrying forward.
                        //
                        // A growable-group node (PortGroups.h) is reused only
                        // if the number of group ports this graph needs is the
                        // one it already has: changing it would mutate a node
                        // the audio thread may still be running, exactly the
                        // case the rule above forbids. Adding a cable to a
                        // group's spare port therefore gives that one node a
                        // fresh state (same as editing its parameters, above);
                        // every other node is still reused.
                        reused = (node != nullptr && ! node->hasPolymorphicPorts()
                                   && node->getGroupPortCount() == requiredPortGroupCount (*node, incomingPortIdsFor (instance.id)));
                    }
                }
            }

            // Content (Node::setContent()): a reused node gets it only when it
            // changed — republishing its buffer keeps the running node, so a
            // content edit never resets its state. One that cannot take it
            // (Node::setContent() returned false) is built fresh instead.
            const auto contentJson = instance.content.isVoid() ? juce::String() : juce::JSON::toString (instance.content, true);
            if (reused)
            {
                const auto previous = previousPlan->nodeIdToAppliedContent.find (instance.id);
                const auto previousJson = previous != previousPlan->nodeIdToAppliedContent.end() ? previous->second : juce::String();
                if (previousJson != contentJson && ! node->setContent (instance.content))
                {
                    reused = false;
                    node = nullptr;
                }
            }

            if (reused)
            {
                // A value-only edit: the new values ride with the plan and the
                // audio thread applies them (ExecutionPlan::pendingParameterUpdates).
                // A previous plan replaced before it ever played never applied
                // its own pending values: hand those on first, so newer ones win.
                if (! previousPlan->pendingParametersApplied.value.load (std::memory_order_acquire))
                    for (const auto& update : previousPlan->pendingParameterUpdates)
                        if (update.node == node.get())
                            plan.pendingParameterUpdates.push_back (update);

                const auto& previousParameters = previousPlan->nodeIdToAppliedParameters.at (instance.id);
                for (const auto& [paramId, value] : instance.parameters)
                {
                    const auto previous = previousParameters.find (paramId);
                    if (previous == previousParameters.end() || previous->second != value)
                    {
                        plan.pendingParameterUpdates.push_back ({ node.get(), paramId, value });
                        parameterChangesBySlot[(size_t) slot].emplace_back (paramId, value);
                    }
                }
                reusedBySlot[(size_t) slot] = 1;
            }

            if (! reused)
            {
                node = factory.create (instance.type);
                if (node == nullptr)
                {
                    result.errorMessage = "Unknown node type '" + instance.type + "' for node '" + instance.id + "'";
                    return result;
                }

                // Growable port groups (PortGroups.h): the group's size is
                // derived from this graph's connections, never stored, and must
                // be set before anything reads the node's ports.
                if (const auto groupCount = requiredPortGroupCount (*node, incomingPortIdsFor (instance.id)); groupCount >= 0)
                    node->setGroupPortCount (groupCount);

                node->prepare (prepareInfo);

                for (const auto& [paramId, value] : instance.parameters)
                    node->setParameter (paramId, value);

                if (! instance.content.isVoid())
                    node->setContent (instance.content);
            }

            portIdIndexBySlot[(size_t) slot] = buildPortIdIndex (*node);
            inputPortsBySlot[(size_t) slot] = node->getInputPorts();
            outputPortsBySlot[(size_t) slot] = node->getOutputPorts();
            flatStartForInputBySlot[(size_t) slot] = computeFlatStarts (inputPortsBySlot[(size_t) slot]);
            flatStartForOutputBySlot[(size_t) slot] = computeFlatStarts (outputPortsBySlot[(size_t) slot]);

            // ExecutionPlan::process()'s per-step scratch arrays hold exactly
            // maxPortsPerNode entries, indexed by FLAT channel slot (a
            // Channels::Stereo port occupies two) — a node whose total
            // channel count exceeds that would be a stack overrun (undefined
            // behaviour in a release build, where the jassert there compiles
            // out) — so it's a compile error here.
            if (totalFlatCount (inputPortsBySlot[(size_t) slot]) > ExecutionPlan::maxPortsPerNode
                || totalFlatCount (outputPortsBySlot[(size_t) slot]) > ExecutionPlan::maxPortsPerNode)
            {
                result.errorMessage = "Node '" + instance.id + "' declares more than "
                                       + juce::String (ExecutionPlan::maxPortsPerNode) + " input or output channels";
                return result;
            }
            plan.nodeIdToSlot[instance.id] = slot;
            plan.nodeIdToType[instance.id] = instance.type;
            plan.nodeIdToAppliedParameters[instance.id] = instance.parameters;
            plan.nodeIdToAppliedContent[instance.id] = contentJson;

            // ExecutionPlan::noteInNodeId's own comment has the full story:
            // resolved once here, by type, instead of PluginProcessor
            // hardcoding a specific instance id that the editor's own
            // Add-menu never actually produces.
            if (plan.noteInNodeId.isEmpty() && instance.type == "io.noteIn")
                plan.noteInNodeId = instance.id;

            plan.nodes.push_back (std::move (node));
        }

        // ---- Resolve polymorphic port types (docs/CLEANUP.md Priority 1 #2) ----
        // Must run before canConnect() is ever called below: a node like
        // RerouteNode reports whatever type it currently holds via
        // getInputPorts()/getOutputPorts(), and those need to already
        // reflect what's wired to its input before any connection touching
        // it (incoming OR outgoing) is validated. Iterated to a fixed point
        // — each pass can only propagate a resolved type one hop further
        // along a chain of several such nodes, and a chain can be at most
        // numNodes-1 hops long in a graph with numNodes nodes, so numNodes
        // passes is a safe convergence bound (duplicate/invalid connections
        // are left for the ordinary loop below to reject with a real error;
        // this pass silently skips anything it can't resolve).
        for (int pass = 0; pass < numNodes; ++pass)
        {
            bool anyChanged = false;

            for (const auto& connection : graph.getConnections())
            {
                const auto fromIt = plan.nodeIdToSlot.find (connection.fromNodeId);
                const auto toIt = plan.nodeIdToSlot.find (connection.toNodeId);
                if (fromIt == plan.nodeIdToSlot.end() || toIt == plan.nodeIdToSlot.end())
                    continue;

                auto& toNode = plan.nodes[(size_t) toIt->second];
                if (! toNode->hasPolymorphicPorts())
                    continue;

                const auto& fromPorts = portIdIndexBySlot[(size_t) fromIt->second].outputIndexById;
                const auto fromPortIt = fromPorts.find (connection.fromPortId);
                const auto& toPorts = portIdIndexBySlot[(size_t) toIt->second].inputIndexById;
                const auto toPortIt = toPorts.find (connection.toPortId);
                if (fromPortIt == fromPorts.end() || toPortIt == toPorts.end())
                    continue;

                // Did the node actually change what it reports? Comparing the
                // resolved signature (not "incoming differs from current") is
                // what makes this converge when a node deliberately ignores a
                // source — it keeps its first adoption, so a second, different
                // source must not count as a change every pass.
                const auto signatureBefore = portSignature (inputPortsBySlot[(size_t) toIt->second], outputPortsBySlot[(size_t) toIt->second]);
                toNode->resolveIncomingPort (connection.toPortId, outputPortsBySlot[(size_t) fromIt->second][(size_t) fromPortIt->second]);

                auto inputsNow = toNode->getInputPorts();
                auto outputsNow = toNode->getOutputPorts();
                if (portSignature (inputsNow, outputsNow) != signatureBefore)
                {
                    inputPortsBySlot[(size_t) toIt->second] = std::move (inputsNow);
                    outputPortsBySlot[(size_t) toIt->second] = std::move (outputsNow);
                    portIdIndexBySlot[(size_t) toIt->second] = buildPortIdIndex (*toNode);
                    flatStartForInputBySlot[(size_t) toIt->second] = computeFlatStarts (inputPortsBySlot[(size_t) toIt->second]);
                    flatStartForOutputBySlot[(size_t) toIt->second] = computeFlatStarts (outputPortsBySlot[(size_t) toIt->second]);
                    anyChanged = true;
                }
            }

            if (! anyChanged)
                break;
        }

        // ---- Channel lanes (wiki/plans/StereoChannels.md) ------------------
        // A node is lane-able when its channels are independent: at least
        // one Inherited Audio input, every Audio output Inherited, and no
        // fixed-Stereo Audio port. Such a node runs once per channel of what
        // is wired into it — its author writes mono DSP, the compiler gives
        // each channel its own instance (own filter memory, own delay line),
        // exactly the way a voice gets its own plan. Widths only ever grow
        // (1 -> maxLanes), so iterating over the connections reaches a fixed
        // point even around a feedback loop.
        constexpr int maxLanes = 2;
        std::vector<char> laneable ((size_t) numNodes, 0);
        std::vector<int> laneCount ((size_t) numNodes, 1);

        for (int slot = 0; slot < numNodes; ++slot)
        {
            // Every Signal can carry channels (DataAndWavetable.md D2) — a
            // modulation as much as a sound. A Mono port beside the lane
            // ports is shared: inputs broadcast to every lane, outputs are
            // read from lane 0 (forEachLaneOutput). Only a fixed-Stereo port
            // means the node already handles both channels itself.
            auto inheritedInput = false, inheritedOutput = false, fixedStereo = false;
            for (const auto& port : inputPortsBySlot[(size_t) slot])
                if (port.type == SignalType::Signal)
                {
                    inheritedInput = inheritedInput || port.channels == Channels::Inherited;
                    fixedStereo = fixedStereo || port.channels == Channels::Stereo;
                }
            for (const auto& port : outputPortsBySlot[(size_t) slot])
                if (port.type == SignalType::Signal)
                {
                    inheritedOutput = inheritedOutput || port.channels == Channels::Inherited;
                    fixedStereo = fixedStereo || port.channels == Channels::Stereo;
                }
            laneable[(size_t) slot] = (char) (inheritedInput && inheritedOutput && ! fixedStereo);
        }

        // How many channels a port carries in this compile.
        const auto widthOf = [&] (int slot, const PortDescriptor& port) -> int
        {
            if (port.type != SignalType::Signal)
                return 1;
            if (port.channels == Channels::Stereo)
                return 2;
            if (port.channels == Channels::Inherited && laneable[(size_t) slot])
                return laneCount[(size_t) slot];
            return 1;
        };
        // Whether this port is one the node's lanes each get their own channel of.
        const auto isLanePort = [&] (int slot, const PortDescriptor& port)
        {
            return laneable[(size_t) slot] && port.type == SignalType::Signal && port.channels == Channels::Inherited;
        };

        for (int pass = 0; pass <= numNodes; ++pass)
        {
            bool anyChanged = false;
            for (const auto& connection : graph.getConnections())
            {
                const auto fromIt = plan.nodeIdToSlot.find (connection.fromNodeId);
                const auto toIt = plan.nodeIdToSlot.find (connection.toNodeId);
                if (fromIt == plan.nodeIdToSlot.end() || toIt == plan.nodeIdToSlot.end() || ! laneable[(size_t) toIt->second])
                    continue;

                const auto fromPortIt = portIdIndexBySlot[(size_t) fromIt->second].outputIndexById.find (connection.fromPortId);
                const auto toPortIt = portIdIndexBySlot[(size_t) toIt->second].inputIndexById.find (connection.toPortId);
                if (fromPortIt == portIdIndexBySlot[(size_t) fromIt->second].outputIndexById.end()
                    || toPortIt == portIdIndexBySlot[(size_t) toIt->second].inputIndexById.end())
                    continue;

                if (! isLanePort (toIt->second, inputPortsBySlot[(size_t) toIt->second][(size_t) toPortIt->second]))
                    continue;

                const auto width = std::min (maxLanes, widthOf (fromIt->second, outputPortsBySlot[(size_t) fromIt->second][(size_t) fromPortIt->second]));
                if (width > laneCount[(size_t) toIt->second])
                {
                    laneCount[(size_t) toIt->second] = width;
                    anyChanged = true;
                }
            }
            if (! anyChanged)
                break;
        }

        const auto flatStartsOf = [&] (int slot, const std::vector<PortDescriptor>& ports)
        {
            std::vector<int> starts (ports.size());
            int running = 0;
            for (size_t i = 0; i < ports.size(); ++i)
            {
                starts[i] = running;
                running += widthOf (slot, ports[i]);
            }
            return starts;
        };

        for (int slot = 0; slot < numNodes; ++slot)
        {
            flatStartForInputBySlot[(size_t) slot] = flatStartsOf (slot, inputPortsBySlot[(size_t) slot]);
            flatStartForOutputBySlot[(size_t) slot] = flatStartsOf (slot, outputPortsBySlot[(size_t) slot]);
        }

        // Lane 0 is the node itself; every further lane is another instance
        // of the same type, appended after the graph's own slots. Reused from
        // the previous plan under the same rule as lane 0 (same id, type and
        // parameters, carried state), so a value edit doesn't reset either
        // channel.
        plan.laneSlotsBySlot.assign ((size_t) numNodes, {});
        for (int slot = 0; slot < numNodes; ++slot)
            plan.laneSlotsBySlot[(size_t) slot].push_back (slot);

        for (int slot = 0; slot < numNodes; ++slot)
        {
            const auto& instance = graphNodes[(size_t) slot];
            const auto& laneZero = plan.nodes[(size_t) slot];

            for (int lane = 1; lane < laneCount[(size_t) slot]; ++lane)
            {
                std::shared_ptr<Node> laneNode;

                if (reusedBySlot[(size_t) slot] && previousPlan != nullptr)
                {
                    const auto previousSlot = previousPlan->nodeIdToSlot.at (instance.id);
                    if ((size_t) previousSlot < previousPlan->laneSlotsBySlot.size()
                        && lane < (int) previousPlan->laneSlotsBySlot[(size_t) previousSlot].size())
                    {
                        laneNode = previousPlan->nodes[(size_t) previousPlan->laneSlotsBySlot[(size_t) previousSlot][(size_t) lane]];

                        if (! previousPlan->pendingParametersApplied.value.load (std::memory_order_acquire))
                            for (const auto& update : previousPlan->pendingParameterUpdates)
                                if (update.node == laneNode.get())
                                    plan.pendingParameterUpdates.push_back (update);
                        for (const auto& [paramId, value] : parameterChangesBySlot[(size_t) slot])
                            plan.pendingParameterUpdates.push_back ({ laneNode.get(), paramId, value });
                    }
                }

                if (laneNode == nullptr)
                {
                    laneNode = factory.create (instance.type);
                    if (const auto groupCount = laneZero->getGroupPortCount(); groupCount >= 0)
                        laneNode->setGroupPortCount (groupCount);
                    laneNode->prepare (prepareInfo);
                    for (const auto& [paramId, value] : instance.parameters)
                        laneNode->setParameter (paramId, value);

                    // A polymorphic node's resolved port types come from its
                    // sources, which are final by now — one replay suffices.
                    if (laneNode->hasPolymorphicPorts())
                        for (const auto& connection : graph.getConnections())
                        {
                            if (connection.toNodeId != instance.id)
                                continue;
                            const auto fromIt = plan.nodeIdToSlot.find (connection.fromNodeId);
                            if (fromIt == plan.nodeIdToSlot.end())
                                continue;
                            const auto fromPortIt = portIdIndexBySlot[(size_t) fromIt->second].outputIndexById.find (connection.fromPortId);
                            if (fromPortIt != portIdIndexBySlot[(size_t) fromIt->second].outputIndexById.end())
                                laneNode->resolveIncomingPort (connection.toPortId, outputPortsBySlot[(size_t) fromIt->second][(size_t) fromPortIt->second]);
                        }
                }

                plan.nodes.push_back (std::move (laneNode));
                plan.laneSlotsBySlot[(size_t) slot].push_back ((int) plan.nodes.size() - 1);
            }
        }

        // M21: remember which nodes want the host's per-block data (audio in,
        // MIDI controllers, transport) so the audio thread can hand it to them
        // without scanning the plan (ExecutionPlan::hostInputNodes).
        for (const auto& node : plan.nodes)
            if (node->wantsHostInputs())
                plan.hostInputNodes.push_back (node.get());

        // ---- Resolve connections to slot/port indices --------------------
        // incomingSource maps (toSlot, toDESCRIPTOR-index) -> one PortKey per
        // destination CHANNEL, each a (fromSlot, fromFLAT-index) pair. For an
        // ordinary Mono->Mono connection this is a single-element vector,
        // same as every connection before this redesign. For Mono->Stereo
        // (free broadcast, canConnect's existing rule) both destination
        // channels point at the SAME source flat slot. For Stereo->Stereo
        // they're bound pairwise. Stereo->Mono never reaches here — canConnect
        // already rejects it (NeedsAdapters via mix.downmix).
        std::unordered_map<PortKey, std::vector<PortKey>, PortKeyHash> incomingSource;
        std::vector<std::vector<int>> successors ((size_t) numNodes);
        std::vector<std::unordered_set<int>> successorSet ((size_t) numNodes); // for dedup
        std::vector<NoteConnection> noteConnections; // M18 (ADR-0024)
        std::unordered_set<PortKey, PortKeyHash> noteInputsUsed; // duplicate-connection guard for Note inputs, tracked separately from incomingSource
        std::unordered_set<PortKey, PortKeyHash> dataInputsUsed; // same, for Data inputs (Data Foundations batch)

        for (const auto& connection : graph.getConnections())
        {
            const auto fromIt = plan.nodeIdToSlot.find (connection.fromNodeId);
            const auto toIt = plan.nodeIdToSlot.find (connection.toNodeId);

            if (fromIt == plan.nodeIdToSlot.end())
            {
                result.errorMessage = "Connection references unknown node id: " + connection.fromNodeId;
                return result;
            }

            if (toIt == plan.nodeIdToSlot.end())
            {
                result.errorMessage = "Connection references unknown node id: " + connection.toNodeId;
                return result;
            }

            const auto& fromPorts = portIdIndexBySlot[(size_t) fromIt->second].outputIndexById;
            const auto& toPorts = portIdIndexBySlot[(size_t) toIt->second].inputIndexById;

            const auto fromPortIt = fromPorts.find (connection.fromPortId);
            if (fromPortIt == fromPorts.end())
            {
                result.errorMessage = "Node '" + connection.fromNodeId + "' has no output port '" + connection.fromPortId + "'";
                return result;
            }

            const auto toPortIt = toPorts.find (connection.toPortId);
            if (toPortIt == toPorts.end())
            {
                result.errorMessage = "Node '" + connection.toNodeId + "' has no input port '" + connection.toPortId + "'";
                return result;
            }

            // M16: canConnect (CanConnect.h) is the engine's sole authority
            // on connection validity (SIGNAL_TYPES.md §4). A `NeedsAdapters`
            // result is rejected here exactly like a flat `Reject` — by the
            // time a connection reaches the compiler it must already be
            // directly compatible; auto-inserting the adapter chain is a
            // higher-level concern (GraphEditController::connectWithAutoAdapt),
            // not something the compiler does silently mid-compile.
            const auto& fromPort = outputPortsBySlot[(size_t) fromIt->second][(size_t) fromPortIt->second];
            const auto& toPort = inputPortsBySlot[(size_t) toIt->second][(size_t) toPortIt->second];
            const auto connectivity = canConnect (fromPort, toPort);

            if (connectivity.outcome != ConnectionOutcome::Ok)
            {
                result.errorMessage = "Cannot connect " + connection.fromNodeId + "." + connection.fromPortId
                                       + " to " + connection.toNodeId + "." + connection.toPortId + ": "
                                       + connectivity.reason;
                return result;
            }

            const PortKey toKey { toIt->second, toPortIt->second };

            // M18 (ADR-0024): a Note-typed connection still participates in
            // ordinary successors/SCC scheduling (so the producer is
            // guaranteed to run before the consumer, same as any other
            // edge) but is routed around incomingSource/resolveInputChannel
            // entirely — ExecutionPlan::noteBuffers carries it instead. A
            // Note port's channel count is always 1 (channelCountOf only
            // doubles an Audio port), so its descriptor index and flat index
            // are always the same — no conversion needed here.
            if (fromPort.type == SignalType::Note)
            {
                if (noteInputsUsed.find (toKey) != noteInputsUsed.end())
                {
                    result.errorMessage = "Input port already connected: " + connection.toNodeId
                                           + " port " + connection.toPortId;
                    return result;
                }

                noteInputsUsed.insert (toKey);
                noteConnections.push_back ({ fromIt->second, fromPortIt->second, toIt->second, toPortIt->second });

                if (successorSet[(size_t) fromIt->second].insert (toIt->second).second)
                    successors[(size_t) fromIt->second].push_back (toIt->second);

                continue;
            }

            // Data Foundations batch: a Data-typed connection is wired once,
            // right here, by handing the consumer a raw DataPublisher*
            // (Node.h's own getDataPublisher()/setDataInput() doc comments
            // have the full reasoning for why this is sufficient — the
            // publisher's address never changes, only its published
            // contents do, which the consumer reads for itself later, live,
            // on the audio thread). Same "one source per input" dedup and
            // successors-edge bookkeeping as the Note case above, for the
            // same reasons; entirely bypasses incomingSource/
            // resolveInputChannel, since Data never flows through the
            // ordinary per-sample float blockBuffers at all.
            if (fromPort.type == SignalType::Data)
            {
                if (dataInputsUsed.find (toKey) != dataInputsUsed.end())
                {
                    result.errorMessage = "Input port already connected: " + connection.toNodeId
                                           + " port " + connection.toPortId;
                    return result;
                }

                dataInputsUsed.insert (toKey);
                for (const auto laneSlot : plan.laneSlotsBySlot[(size_t) toIt->second])
                    plan.nodes[(size_t) laneSlot]->setDataInput (connection.toPortId,
                                                                  plan.nodes[(size_t) fromIt->second]->getDataPublisher());

                if (successorSet[(size_t) fromIt->second].insert (toIt->second).second)
                    successors[(size_t) fromIt->second].push_back (toIt->second);

                continue;
            }

            if (incomingSource.find (toKey) != incomingSource.end())
            {
                result.errorMessage = "Input port already connected: " + connection.toNodeId
                                       + " port " + connection.toPortId;
                return result;
            }

            const auto fromChannels = widthOf (fromIt->second, fromPort);
            const auto toChannels = widthOf (toIt->second, toPort);
            const auto fromFlatStart = flatStartForOutputBySlot[(size_t) fromIt->second][(size_t) fromPortIt->second];

            std::vector<PortKey> sources;
            sources.reserve ((size_t) toChannels);
            for (int c = 0; c < toChannels; ++c)
            {
                // Mono source into a wider destination (canConnect's free-
                // broadcast rule): every destination channel reads the same
                // single source flat slot. Otherwise channels are bound one-
                // to-one; a stereo source into a one-channel Inherited port
                // that isn't lane-able (a viewer, instance.sum) reads its
                // first channel.
                const auto channelOffset = (fromChannels == 1) ? 0 : std::min (c, fromChannels - 1);
                sources.push_back ({ fromIt->second, fromFlatStart + channelOffset });
            }
            incomingSource[toKey] = std::move (sources);

            if (successorSet[(size_t) fromIt->second].insert (toIt->second).second)
                successors[(size_t) fromIt->second].push_back (toIt->second);
        }

        // A reused node keeps whatever Data publisher it was last handed: one
        // whose Data cable is gone must be told so, or it reads a producer that
        // may no longer exist. Consumers hold it atomically (the old plan may
        // still be running the node) and fall back to their own content.
        for (int slot = 0; slot < numNodes; ++slot)
        {
            if (! reusedBySlot[(size_t) slot])
                continue;
            const auto& inputs = inputPortsBySlot[(size_t) slot];
            for (int p = 0; p < (int) inputs.size(); ++p)
                if (inputs[(size_t) p].type == SignalType::Data && dataInputsUsed.find ({ slot, p }) == dataInputsUsed.end())
                    for (const auto laneSlot : plan.laneSlotsBySlot[(size_t) slot])
                        plan.nodes[(size_t) laneSlot]->setDataInput (inputs[(size_t) p].id, nullptr);
        }

        // ---- Note buffers (M18, ADR-0024) ---------------------------------
        // One ExecutionPlan::noteBuffers entry per connected Note-typed
        // OUTPUT port, shared by every consumer wired to it (fan-out works
        // exactly like an ordinary blockBuffer — several InputRefs pointing
        // at the same index).
        std::unordered_map<PortKey, int, PortKeyHash> noteOutputBufferIndexFor;
        std::unordered_map<PortKey, int, PortKeyHash> noteInputBufferIndexFor;

        for (const auto& nc : noteConnections)
        {
            const PortKey fromKey { nc.fromSlot, nc.fromPort };
            auto it = noteOutputBufferIndexFor.find (fromKey);
            int bufferIndex;

            if (it == noteOutputBufferIndexFor.end())
            {
                plan.noteBuffers.emplace_back ((size_t) prepareInfo.maxBlockSize);
                bufferIndex = (int) plan.noteBuffers.size() - 1;
                noteOutputBufferIndexFor[fromKey] = bufferIndex;
            }
            else
            {
                bufferIndex = it->second;
            }

            noteInputBufferIndexFor[{ nc.toSlot, nc.toPort }] = bufferIndex;
        }

        // ---- Strongly connected components -> schedule order -------------
        const auto sccs = computeStronglyConnectedComponents (numNodes, successors);

        OutputLocationMap outputLocation; // keyed by (slot, FLAT index)
        plan.maxBlockSize = prepareInfo.maxBlockSize;
        plan.generation = generation;

        // Resolves ONE channel of ONE input descriptor. toDescriptor is a
        // descriptor index (matching inputPortsBySlot's own indexing);
        // channelIndex selects which of that descriptor's 1-or-2 flat
        // channels (always 0 for a Mono port).
        const auto resolveInputChannel = [&] (int toSlot, int toDescriptor, int channelIndex) -> ExecutionPlan::InputRef
        {
            const auto it = incomingSource.find ({ toSlot, toDescriptor });
            if (it == incomingSource.end())
            {
                // hasFallbackWhenUnconnected ports (PortDescriptor.h) read a
                // NaN sentinel instead of plain silence when nothing's
                // wired to them — deliberately NOT the port's own
                // `defaultValue`: baking that in here would freeze it at
                // whatever it was when this plan was compiled, silently
                // breaking any node (DelayNode.h's "time.delay.samples" is
                // the first example) whose value is still meant to be
                // adjustable via setParameter() after compilation. The node
                // itself resolves NaN -> "use my own current value" every
                // sample instead, which stays live no matter when
                // setParameter() was last called. No port with this flag is
                // ever Stereo today, so this path is only ever reached with
                // channelIndex == 0 in practice — written generically
                // anyway, not hardcoded to that assumption.
                const auto& inputPorts = inputPortsBySlot[(size_t) toSlot];
                if (toDescriptor < (int) inputPorts.size() && inputPorts[(size_t) toDescriptor].hasFallbackWhenUnconnected)
                {
                    AlignedBuffer buffer;
                    buffer.resize (1, (size_t) prepareInfo.maxBlockSize);
                    buffer.getBlock().fill (std::numeric_limits<float>::quiet_NaN());
                    plan.blockBuffers.push_back (std::move (buffer));
                    return { ExecutionPlan::InputRef::Kind::BlockBuffer, (int) plan.blockBuffers.size() - 1 };
                }
                return { ExecutionPlan::InputRef::Kind::Silence, -1 };
            }

            const auto& sources = it->second;
            jassert (channelIndex < (int) sources.size());
            const auto locIt = outputLocation.find (sources[(size_t) channelIndex]);
            jassert (locIt != outputLocation.end()); // guaranteed by SCC topological scheduling order
            return locIt->second;
        };

        // ---- Lane-aware step building (StereoChannels.md) ----------------
        // One lane's physical inputs, in its own flat order: a lane port
        // contributes that lane's channel; every other port all of its
        // channels, shared by every lane (one cutoff envelope drives both
        // channels of a stereo filter).
        const auto laneInputs = [&] (int slot, int lane)
        {
            std::vector<ExecutionPlan::InputRef> refs;
            const auto& inputDescs = inputPortsBySlot[(size_t) slot];
            for (int p = 0; p < (int) inputDescs.size(); ++p)
            {
                if (isLanePort (slot, inputDescs[(size_t) p]))
                    refs.push_back (resolveInputChannel (slot, p, lane));
                else
                    for (int c = 0; c < widthOf (slot, inputDescs[(size_t) p]); ++c)
                        refs.push_back (resolveInputChannel (slot, p, c));
            }
            return refs;
        };

        // Visits one lane's physical outputs in its own flat order as
        // (descriptor, logical flat index, visible). A shared (non-lane)
        // output is only visible from lane 0 — the other lanes write a
        // private copy nobody reads.
        const auto forEachLaneOutput = [&] (int slot, int lane, auto&& visit)
        {
            const auto& outputDescs = outputPortsBySlot[(size_t) slot];
            const auto& starts = flatStartForOutputBySlot[(size_t) slot];
            for (int p = 0; p < (int) outputDescs.size(); ++p)
            {
                if (isLanePort (slot, outputDescs[(size_t) p]))
                    visit (p, starts[(size_t) p] + lane, true);
                else
                    for (int c = 0; c < widthOf (slot, outputDescs[(size_t) p]); ++c)
                        visit (p, starts[(size_t) p] + c, lane == 0);
            }
        };

        for (const auto& scc : sccs)
        {
            const auto isSelfLoop = scc.size() == 1 && successorSet[(size_t) scc[0]].count (scc[0]) > 0;
            const auto isRegion = scc.size() > 1 || isSelfLoop;

            if (! isRegion)
            {
                const auto slot = scc[0];
                const auto& inputDescs = inputPortsBySlot[(size_t) slot];
                const auto& outputDescs = outputPortsBySlot[(size_t) slot];
                const auto& lanes = plan.laneSlotsBySlot[(size_t) slot];

                // docs/CLEANUP.md Priority 1 #1: resolve bypass here, once,
                // rather than at process() time — ExecutionPlan::process()
                // must never call getOutputPorts() itself (that allocates).
                // "Primary" output is the one flagged isPrimaryOutput, or
                // output 0 if none is (matching NodeCard.tsx's own
                // splitPorts() convention); "primary" input is always the
                // step's first input. Each lane bypasses its own channel, so
                // a bypassed per-channel node passes stereo through intact; a
                // fixed-Stereo node still only copies its first channel.
                const auto& instanceProperties = graphNodes[(size_t) slot].properties;
                const auto bypassedIt = instanceProperties.find ("bypassed");
                const auto bypassed = bypassedIt != instanceProperties.end() && (bool) bypassedIt->second;

                auto primaryOutputPort = 0;
                for (int p = 0; p < (int) outputDescs.size(); ++p)
                {
                    if (outputDescs[(size_t) p].isPrimaryOutput)
                    {
                        primaryOutputPort = p;
                        break;
                    }
                }

                for (int lane = 0; lane < (int) lanes.size(); ++lane)
                {
                    ExecutionPlan::BlockStep blockStep;
                    blockStep.nodeSlot = lanes[(size_t) lane];
                    blockStep.inputs = laneInputs (slot, lane);

                    int primaryOutputBuffer = -1;
                    forEachLaneOutput (slot, lane, [&] (int p, int flatIndex, bool visible)
                    {
                        AlignedBuffer buffer;
                        buffer.resize (1, (size_t) prepareInfo.maxBlockSize);
                        plan.blockBuffers.push_back (std::move (buffer));
                        const auto bufferIndex = (int) plan.blockBuffers.size() - 1;
                        blockStep.outputBufferIndices.push_back (bufferIndex);

                        if (p == primaryOutputPort && primaryOutputBuffer < 0)
                            primaryOutputBuffer = bufferIndex;
                        if (visible)
                            outputLocation[{ slot, flatIndex }] = { ExecutionPlan::InputRef::Kind::BlockBuffer, bufferIndex };
                    });

                    if (bypassed)
                    {
                        blockStep.bypassed = true;
                        blockStep.bypassOutputBufferIndex = primaryOutputBuffer;
                    }

                    // M18 (ADR-0024): a connected Note-typed port routes through
                    // noteBuffers instead of the ordinary arrays just built
                    // above. Descriptor index used directly — a Note port's
                    // flat index always equals its descriptor index. Every lane
                    // hears the notes; only lane 0 produces them.
                    for (int p = 0; p < (int) inputDescs.size(); ++p)
                    {
                        const auto it = noteInputBufferIndexFor.find ({ slot, p });
                        if (it != noteInputBufferIndexFor.end())
                            blockStep.noteInputBufferIndex = it->second;
                    }

                    if (lane == 0)
                        for (int p = 0; p < (int) outputDescs.size(); ++p)
                        {
                            const auto it = noteOutputBufferIndexFor.find ({ slot, p });
                            if (it != noteOutputBufferIndexFor.end())
                                blockStep.noteOutputBufferIndex = it->second;
                        }

                    ExecutionPlan::Step step;
                    step.kind = ExecutionPlan::Step::Kind::Block;
                    step.block = std::move (blockStep);
                    plan.steps.push_back (std::move (step));
                }

                continue;
            }

            // ---- Per-sample region -----------------------------------
            for (int slot : scc)
            {
                if (! plan.nodes[(size_t) slot]->supportsPerSample())
                {
                    result.errorMessage = "Node '" + graphNodes[(size_t) slot].id
                                           + "' is inside a feedback cycle but doesn't support per-sample processing";
                    return result;
                }
            }

            // M18 (ADR-0024): Note ports aren't supported inside a
            // per-sample region — nothing needs that yet, and silently
            // ignoring a Note connection there would be worse than a clear
            // compile error.
            for (int slot : scc)
            {
                for (const auto& nc : noteConnections)
                {
                    if (nc.fromSlot == slot || nc.toSlot == slot)
                    {
                        result.errorMessage = "Node '" + graphNodes[(size_t) slot].id
                                               + "' has a Note-typed connection but is inside a feedback cycle — "
                                               + "Note ports inside a per-sample region aren't supported yet";
                        return result;
                    }
                }
            }

            const auto internalOrder = computeRegionInternalOrder (scc, successors);
            const std::unordered_set<int> inRegion (scc.begin(), scc.end());

            // Every lane is its own position; a node's lanes sit next to each
            // other and never read one another, so their mutual order is free.
            std::vector<std::pair<int, int>> positions; // (graph slot, lane)
            for (const auto slot : internalOrder)
                for (int lane = 0; lane < (int) plan.laneSlotsBySlot[(size_t) slot].size(); ++lane)
                    positions.emplace_back (slot, lane);

            ExecutionPlan::PerSampleRegionStep regionStep;
            for (const auto& [slot, lane] : positions)
                regionStep.nodeSlotsInOrder.push_back (plan.laneSlotsBySlot[(size_t) slot][(size_t) lane]);

            // First pass: a scalar for every position's every output channel,
            // so resolveInputChannel() finds region-internal producers
            // regardless of position (the stale read of a later position is
            // the loop's one-sample delay).
            regionStep.outputScalarIndices.resize (positions.size());
            for (size_t pos = 0; pos < positions.size(); ++pos)
            {
                const auto [slot, lane] = positions[pos];
                forEachLaneOutput (slot, lane, [&] (int, int flatIndex, bool visible)
                {
                    plan.regionScalars.push_back (0.0f);
                    const auto scalarIndex = (int) plan.regionScalars.size() - 1;
                    regionStep.outputScalarIndices[pos].push_back (scalarIndex);
                    if (visible)
                        outputLocation[{ slot, flatIndex }] = { ExecutionPlan::InputRef::Kind::RegionScalar, scalarIndex };
                });
            }

            // Second pass: wire inputs, now that every region-internal
            // producer has a resolved location.
            regionStep.inputsPerNode.resize (positions.size());
            for (size_t pos = 0; pos < positions.size(); ++pos)
                regionStep.inputsPerNode[pos] = laneInputs (positions[pos].first, positions[pos].second);

            // Every region output channel something outside reads — another
            // step, or the graph's designated output (both channels of it when
            // it's stereo) — is published into its own block buffer.
            std::vector<PortKey> externalKeys;
            std::unordered_set<PortKey, PortKeyHash> externalSeen;
            for (const auto& [toKey, fromRefs] : incomingSource)
            {
                if (inRegion.count (toKey.slot) > 0)
                    continue; // consumer is itself inside the region
                for (const auto& fromKey : fromRefs)
                    if (inRegion.count (fromKey.slot) > 0 && externalSeen.insert (fromKey).second)
                        externalKeys.push_back (fromKey);
            }

            if (const auto outSlotIt = plan.nodeIdToSlot.find (graph.getOutputNodeId());
                outSlotIt != plan.nodeIdToSlot.end() && inRegion.count (outSlotIt->second) > 0)
            {
                const auto& outPortsById = portIdIndexBySlot[(size_t) outSlotIt->second].outputIndexById;
                if (const auto outPortIt = outPortsById.find (graph.getOutputPortId()); outPortIt != outPortsById.end())
                {
                    const auto start = flatStartForOutputBySlot[(size_t) outSlotIt->second][(size_t) outPortIt->second];
                    const auto width = widthOf (outSlotIt->second, outputPortsBySlot[(size_t) outSlotIt->second][(size_t) outPortIt->second]);
                    for (int c = 0; c < width; ++c)
                        if (externalSeen.insert ({ outSlotIt->second, start + c }).second)
                            externalKeys.push_back ({ outSlotIt->second, start + c });
                }
            }

            std::sort (externalKeys.begin(), externalKeys.end(), [] (const PortKey& a, const PortKey& b)
                       { return a.slot != b.slot ? a.slot < b.slot : a.port < b.port; });

            for (const auto& key : externalKeys)
            {
                const auto locIt = outputLocation.find (key);
                if (locIt == outputLocation.end() || locIt->second.kind != ExecutionPlan::InputRef::Kind::RegionScalar)
                    continue;

                AlignedBuffer buffer;
                buffer.resize (1, (size_t) prepareInfo.maxBlockSize);
                plan.blockBuffers.push_back (std::move (buffer));
                const auto bufferIndex = (int) plan.blockBuffers.size() - 1;

                regionStep.externalOutputs.push_back ({ locIt->second.index, bufferIndex });

                // Downstream consumers of this channel now resolve through the
                // published block buffer, not the region-internal scalar — the
                // region boundary is opaque to the rest of the schedule.
                locIt->second = { ExecutionPlan::InputRef::Kind::BlockBuffer, bufferIndex };
            }

            ExecutionPlan::Step step;
            step.kind = ExecutionPlan::Step::Kind::PerSampleRegion;
            step.region = std::move (regionStep);
            plan.steps.push_back (std::move (step));
        }

        // ---- Final output --------------------------------------------
        const auto outSlotIt = plan.nodeIdToSlot.find (graph.getOutputNodeId());
        if (outSlotIt == plan.nodeIdToSlot.end())
        {
            result.errorMessage = "Graph output node not found: " + graph.getOutputNodeId();
            return result;
        }

        const auto& outputPortsById = portIdIndexBySlot[(size_t) outSlotIt->second].outputIndexById;
        const auto outPortIt = outputPortsById.find (graph.getOutputPortId());
        if (outPortIt == outputPortsById.end())
        {
            result.errorMessage = "Graph output node '" + graph.getOutputNodeId() + "' has no output port '"
                                   + graph.getOutputPortId() + "'";
            return result;
        }

        const auto& finalOutputPorts = outputPortsBySlot[(size_t) outSlotIt->second];
        const auto finalFlatStart = flatStartForOutputBySlot[(size_t) outSlotIt->second][(size_t) outPortIt->second];

        const auto outLocIt = outputLocation.find ({ outSlotIt->second, finalFlatStart });
        if (outLocIt == outputLocation.end() || outLocIt->second.kind != ExecutionPlan::InputRef::Kind::BlockBuffer)
        {
            result.errorMessage = "Graph output port has no resolvable buffer";
            return result;
        }

        plan.finalOutputBufferIndex = outLocIt->second.index;

        // Real stereo cable redesign: if the designated output port is
        // itself a genuine 2-channel port (one real `Channels::Stereo`
        // Audio port now, not a separate left/right port pair), its second
        // flat channel ALWAYS has a real, resolvable buffer by construction
        // — every flat channel is allocated unconditionally above, and
        // canConnect's mono->stereo broadcast rule already guarantees a real
        // value reaches both channels even when only a mono source feeds
        // this port. No "is the right channel actually wired" guard is
        // needed here — that guard existed only in the superseded Milestone
        // 0.2 point-fix, where a stereo-shaped node still exposed its two
        // channels as two SEPARATE, independently-(un)connectable ports.
        if (widthOf (outSlotIt->second, finalOutputPorts[(size_t) outPortIt->second]) == 2)
        {
            const auto rightLocIt = outputLocation.find ({ outSlotIt->second, finalFlatStart + 1 });
            if (rightLocIt != outputLocation.end() && rightLocIt->second.kind == ExecutionPlan::InputRef::Kind::BlockBuffer)
                plan.finalOutputBufferIndexRight = rightLocIt->second.index;
        }

        plan.silenceBuffer.assign ((size_t) prepareInfo.maxBlockSize, 0.0f);

        // M20: (nodeId, portId) -> blockBuffers index for every declared
        // output port, reading its FIRST flat channel only (channel 0) — the
        // same "honest MVP" scope this redesign uses for bypass, above: a
        // stereo-aware scope/meter/tap is real future work, not silently
        // promised. Iterated per (slot, descriptor) rather than over
        // outputLocation's own keys directly, since those are now flat
        // indices, not descriptor indices, and would otherwise report the
        // wrong port id for any node after a Stereo one in its own list.
        for (int slot = 0; slot < numNodes; ++slot)
        {
            const auto& outputs = outputPortsBySlot[(size_t) slot];
            const auto& outputStarts = flatStartForOutputBySlot[(size_t) slot];
            const auto& nodeId = graphNodes[(size_t) slot].id;

            for (int p = 0; p < (int) outputs.size(); ++p)
            {
                const auto locIt = outputLocation.find ({ slot, outputStarts[(size_t) p] });
                if (locIt == outputLocation.end() || locIt->second.kind != ExecutionPlan::InputRef::Kind::BlockBuffer)
                    continue;

                plan.outputBufferIndexByNodeAndPort[nodeId][outputs[(size_t) p].id] = locIt->second.index;

                if (widthOf (slot, outputs[(size_t) p]) == 2)
                {
                    const auto rightIt = outputLocation.find ({ slot, outputStarts[(size_t) p] + 1 });
                    if (rightIt != outputLocation.end() && rightIt->second.kind == ExecutionPlan::InputRef::Kind::BlockBuffer)
                        plan.rightOutputBufferIndexByNodeAndPort[nodeId][outputs[(size_t) p].id] = rightIt->second.index;
                }
            }
        }

        // ADR-0029: the same for input ports — what is wired INTO (node,
        // port), reading the first source channel only (see above). An input
        // fed from a per-sample region's internal scalar has no block buffer
        // to point at, exactly like the output case above.
        for (const auto& [toKey, fromRefs] : incomingSource)
        {
            if (fromRefs.empty())
                continue;

            const auto locationIt = outputLocation.find (fromRefs[0]);
            if (locationIt == outputLocation.end() || locationIt->second.kind != ExecutionPlan::InputRef::Kind::BlockBuffer)
                continue;

            const auto& nodeId = graphNodes[(size_t) toKey.slot].id;
            const auto& portId = inputPortsBySlot[(size_t) toKey.slot][(size_t) toKey.port].id;
            plan.inputSourceBufferIndexByNodeAndPort[nodeId][portId] = locationIt->second.index;
        }

        for (int slot = 0; slot < numNodes; ++slot)
            for (const auto& port : outputPortsBySlot[(size_t) slot])
                if (widthOf (slot, port) == 2)
                    plan.stereoOutputPortsByNode[graphNodes[(size_t) slot].id].push_back (port.id);

        plan.tapForBufferIndex = std::make_unique<std::atomic<Tap*>[]> (plan.blockBuffers.size() * (size_t) ExecutionPlan::maxTapsPerBuffer);

        plan.resolvePhaseSources();
        plan.pendingParametersApplied.value.store (plan.pendingParameterUpdates.empty(), std::memory_order_relaxed);
        result.success = true;
        return result;
    }
}
