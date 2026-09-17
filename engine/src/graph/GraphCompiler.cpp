#include "bazalt/engine/graph/GraphCompiler.h"
#include <algorithm>
#include <functional>
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

    CompileResult GraphCompiler::compile (const NodeGraph& graph,
                                           const NodeFactory& factory,
                                           const NodePrepareInfo& prepareInfo,
                                           uint64_t generation)
    {
        CompileResult result;
        auto& plan = result.plan;

        const auto& graphNodes = graph.getNodes();
        const auto numNodes = (int) graphNodes.size();

        // ---- Instantiate nodes, assign slots -----------------------------
        plan.nodes.reserve ((size_t) numNodes);
        std::vector<PortIdIndex> portIdIndexBySlot ((size_t) numNodes);

        for (int slot = 0; slot < numNodes; ++slot)
        {
            const auto& instance = graphNodes[(size_t) slot];

            if (plan.nodeIdToSlot.find (instance.id) != plan.nodeIdToSlot.end())
            {
                result.errorMessage = "Duplicate node id: " + instance.id;
                return result;
            }

            auto node = factory.create (instance.type);
            if (node == nullptr)
            {
                result.errorMessage = "Unknown node type '" + instance.type + "' for node '" + instance.id + "'";
                return result;
            }

            node->prepare (prepareInfo);

            for (const auto& [paramId, value] : instance.parameters)
                node->setParameter (paramId, value);

            portIdIndexBySlot[(size_t) slot] = buildPortIdIndex (*node);
            plan.nodeIdToSlot[instance.id] = slot;
            plan.nodes.push_back (std::move (node));
        }

        // ---- Resolve connections to slot/port indices --------------------
        std::unordered_map<PortKey, PortKey, PortKeyHash> incomingSource; // (toSlot,toPort) -> (fromSlot,fromPort)
        std::vector<std::vector<int>> successors ((size_t) numNodes);
        std::vector<std::unordered_set<int>> successorSet ((size_t) numNodes); // for dedup

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

            const PortKey toKey { toIt->second, toPortIt->second };

            if (incomingSource.find (toKey) != incomingSource.end())
            {
                result.errorMessage = "Input port already connected: " + connection.toNodeId
                                       + " port " + connection.toPortId;
                return result;
            }

            incomingSource[toKey] = { fromIt->second, fromPortIt->second };

            if (successorSet[(size_t) fromIt->second].insert (toIt->second).second)
                successors[(size_t) fromIt->second].push_back (toIt->second);
        }

        // ---- Strongly connected components -> schedule order -------------
        const auto sccs = computeStronglyConnectedComponents (numNodes, successors);

        OutputLocationMap outputLocation;
        plan.maxBlockSize = prepareInfo.maxBlockSize;
        plan.generation = generation;

        const auto resolveInput = [&] (int toSlot, int toPort) -> ExecutionPlan::InputRef
        {
            const auto it = incomingSource.find ({ toSlot, toPort });
            if (it == incomingSource.end())
                return { ExecutionPlan::InputRef::Kind::Silence, -1 };

            const auto locIt = outputLocation.find (it->second);
            jassert (locIt != outputLocation.end()); // guaranteed by SCC topological scheduling order
            return locIt->second;
        };

        for (const auto& scc : sccs)
        {
            const auto isSelfLoop = scc.size() == 1 && successorSet[(size_t) scc[0]].count (scc[0]) > 0;
            const auto isRegion = scc.size() > 1 || isSelfLoop;

            if (! isRegion)
            {
                const auto slot = scc[0];
                auto& node = plan.nodes[(size_t) slot];
                const auto numOutputs = node->getNumOutputPorts();
                const auto numInputs = node->getNumInputPorts();

                ExecutionPlan::BlockStep blockStep;
                blockStep.nodeSlot = slot;

                blockStep.inputs.reserve ((size_t) numInputs);
                for (int p = 0; p < numInputs; ++p)
                    blockStep.inputs.push_back (resolveInput (slot, p));

                blockStep.outputBufferIndices.reserve ((size_t) numOutputs);
                for (int p = 0; p < numOutputs; ++p)
                {
                    AlignedBuffer buffer;
                    buffer.resize (1, (size_t) prepareInfo.maxBlockSize);
                    plan.blockBuffers.push_back (std::move (buffer));
                    const auto bufferIndex = (int) plan.blockBuffers.size() - 1;
                    blockStep.outputBufferIndices.push_back (bufferIndex);
                    outputLocation[{ slot, p }] = { ExecutionPlan::InputRef::Kind::BlockBuffer, bufferIndex };
                }

                ExecutionPlan::Step step;
                step.kind = ExecutionPlan::Step::Kind::Block;
                step.block = std::move (blockStep);
                plan.steps.push_back (std::move (step));

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

            const auto internalOrder = computeRegionInternalOrder (scc, successors);
            const std::unordered_set<int> inRegion (scc.begin(), scc.end());

            ExecutionPlan::PerSampleRegionStep regionStep;
            regionStep.nodeSlotsInOrder = internalOrder;

            // First pass: allocate a scalar for every region node's every
            // output port, so resolveInput() can find region-internal
            // producers regardless of position within the region.
            std::unordered_map<int, int> positionOfSlot;
            for (int pos = 0; pos < (int) internalOrder.size(); ++pos)
                positionOfSlot[internalOrder[(size_t) pos]] = pos;

            regionStep.outputScalarIndices.resize (internalOrder.size());

            for (int pos = 0; pos < (int) internalOrder.size(); ++pos)
            {
                const auto slot = internalOrder[(size_t) pos];
                auto& node = plan.nodes[(size_t) slot];
                const auto numOutputs = node->getNumOutputPorts();

                for (int p = 0; p < numOutputs; ++p)
                {
                    plan.regionScalars.push_back (0.0f);
                    const auto scalarIndex = (int) plan.regionScalars.size() - 1;
                    regionStep.outputScalarIndices[(size_t) pos].push_back (scalarIndex);
                    outputLocation[{ slot, p }] = { ExecutionPlan::InputRef::Kind::RegionScalar, scalarIndex };
                }
            }

            // Second pass: wire inputs, now that every region-internal
            // producer has a resolved location.
            regionStep.inputsPerNode.resize (internalOrder.size());

            for (int pos = 0; pos < (int) internalOrder.size(); ++pos)
            {
                const auto slot = internalOrder[(size_t) pos];
                auto& node = plan.nodes[(size_t) slot];
                const auto numInputs = node->getNumInputPorts();

                for (int p = 0; p < numInputs; ++p)
                    regionStep.inputsPerNode[(size_t) pos].push_back (resolveInput (slot, p));
            }

            // Does anything outside the region (another node, or the
            // graph's own designated output) need this region's result?
            int externalProducerSlot = -1, externalProducerPort = -1;

            for (const auto& [toKey, fromKey] : incomingSource)
                if (inRegion.count (fromKey.slot) > 0 && inRegion.count (toKey.slot) == 0)
                {
                    externalProducerSlot = fromKey.slot;
                    externalProducerPort = fromKey.port;
                    break;
                }

            if (externalProducerSlot == -1)
            {
                const auto outSlotIt = plan.nodeIdToSlot.find (graph.getOutputNodeId());
                if (outSlotIt != plan.nodeIdToSlot.end() && inRegion.count (outSlotIt->second) > 0)
                {
                    const auto& outPortsById = portIdIndexBySlot[(size_t) outSlotIt->second].outputIndexById;
                    const auto outPortIt = outPortsById.find (graph.getOutputPortId());
                    if (outPortIt != outPortsById.end())
                    {
                        externalProducerSlot = outSlotIt->second;
                        externalProducerPort = outPortIt->second;
                    }
                }
            }

            if (externalProducerSlot != -1)
            {
                AlignedBuffer buffer;
                buffer.resize (1, (size_t) prepareInfo.maxBlockSize);
                plan.blockBuffers.push_back (std::move (buffer));
                const auto bufferIndex = (int) plan.blockBuffers.size() - 1;

                regionStep.externalOutputBufferIndex = bufferIndex;
                regionStep.outputRegionPosition = positionOfSlot.at (externalProducerSlot);
                regionStep.outputPortIndexInNode = externalProducerPort;

                // Downstream (already- or yet-to-be-scheduled) consumers of
                // this port now resolve through the published block buffer,
                // not the region-internal scalar — the region boundary is
                // opaque to the rest of the schedule, by design.
                outputLocation[{ externalProducerSlot, externalProducerPort }] =
                    { ExecutionPlan::InputRef::Kind::BlockBuffer, bufferIndex };
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

        const auto outLocIt = outputLocation.find ({ outSlotIt->second, outPortIt->second });
        if (outLocIt == outputLocation.end() || outLocIt->second.kind != ExecutionPlan::InputRef::Kind::BlockBuffer)
        {
            result.errorMessage = "Graph output port has no resolvable buffer";
            return result;
        }

        plan.finalOutputBufferIndex = outLocIt->second.index;

        plan.silenceBuffer.assign ((size_t) prepareInfo.maxBlockSize, 0.0f);

        result.success = true;
        return result;
    }
}
