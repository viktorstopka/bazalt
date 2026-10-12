#include "bazalt/engine/graph/ExecutionPlan.h"
#include "bazalt/engine/graph/InstanceExpansion.h"
#include "bazalt/engine/nodes/LifeReduceNode.h"

namespace bazalt::engine
{
    void ExecutionPlan::process (int numSamples) noexcept
    {
        jassert (numSamples <= maxBlockSize);

        if (! pendingParametersApplied.value.load (std::memory_order_relaxed))
        {
            for (const auto& update : pendingParameterUpdates)
                update.node->setParameter (update.parameterId, update.value);
            pendingParametersApplied.value.store (true, std::memory_order_release);
        }

        const float* inputPtrs[maxPortsPerNode];
        float* outputPtrs[maxPortsPerNode];

        const auto instanceRuns = [this] (int group, int instance) noexcept
        {
            return group < 0 || instanceGroups[(size_t) group]->instances[(size_t) instance].run;
        };

        for (auto& step : steps)
        {
            if (step.kind == Step::Kind::Block)
            {
                auto& blockStep = step.block;
                if (! instanceRuns (blockStep.group, blockStep.instance))
                    continue;
                auto& node = nodes[(size_t) blockStep.nodeSlot];

                if (blockStep.bypassed)
                {
                    if (blockStep.bypassOutputBufferIndex >= 0)
                    {
                        const auto* inputPtr = (! blockStep.inputs.empty() && blockStep.inputs[0].kind == InputRef::Kind::BlockBuffer)
                                                    ? blockBuffers[(size_t) blockStep.inputs[0].index].getBlock().getChannelPointer (0)
                                                    : silenceBuffer.data();
                        auto* outputPtr = blockBuffers[(size_t) blockStep.bypassOutputBufferIndex].getBlock().getChannelPointer (0);

                        for (int i = 0; i < numSamples; ++i)
                            outputPtr[i] = inputPtr[i];
                    }

                    continue;
                }

                const auto numInputs = (int) blockStep.inputs.size();
                jassert (numInputs <= maxPortsPerNode);

                for (int i = 0; i < numInputs; ++i)
                {
                    const auto& ref = blockStep.inputs[(size_t) i];
                    inputPtrs[i] = ref.kind == InputRef::Kind::BlockBuffer
                                       ? blockBuffers[(size_t) ref.index].getBlock().getChannelPointer (0)
                                       : silenceBuffer.data();
                }

                const auto numOutputs = (int) blockStep.outputBufferIndices.size();
                jassert (numOutputs <= maxPortsPerNode);

                for (int o = 0; o < numOutputs; ++o)
                    outputPtrs[o] = blockBuffers[(size_t) blockStep.outputBufferIndices[(size_t) o]].getBlock().getChannelPointer (0);

                // M18 (ADR-0024): Note ports are routed around the ordinary
                // inputPtrs/outputPtrs float arrays above — a node that
                // consumes/produces Note data reads/writes it here instead.
                if (blockStep.noteInputBufferIndex >= 0)
                    node->consumeNoteBlock (noteBuffers[(size_t) blockStep.noteInputBufferIndex].data(), numSamples);

                node->processBlock (inputPtrs, outputPtrs, numSamples);

                if (blockStep.noteOutputBufferIndex >= 0)
                    node->produceNoteBlock (noteBuffers[(size_t) blockStep.noteOutputBufferIndex].data(), numSamples);
            }
            else
            {
                auto& region = step.region;
                if (! instanceRuns (region.group, region.instance))
                    continue;
                const auto numNodesInRegion = (int) region.nodeSlotsInOrder.size();

                float sampleIn[maxPortsPerNode];
                float sampleOut[maxPortsPerNode];

                for (int s = 0; s < numSamples; ++s)
                {
                    for (int pos = 0; pos < numNodesInRegion; ++pos)
                    {
                        auto& node = nodes[(size_t) region.nodeSlotsInOrder[(size_t) pos]];
                        const auto& inputRefs = region.inputsPerNode[(size_t) pos];
                        const auto numInputs = (int) inputRefs.size();
                        jassert (numInputs <= maxPortsPerNode);

                        for (int i = 0; i < numInputs; ++i)
                            sampleIn[i] = readInput (inputRefs[(size_t) i], s);

                        node->processSample (sampleIn, sampleOut);

                        const auto& outIndices = region.outputScalarIndices[(size_t) pos];
                        for (int o = 0; o < (int) outIndices.size(); ++o)
                            regionScalars[(size_t) outIndices[(size_t) o]] = sampleOut[o];
                    }

                    for (const auto& external : region.externalOutputs)
                        blockBuffers[(size_t) external.bufferIndex].getBlock().getChannelPointer (0)[s] = regionScalars[(size_t) external.scalarIndex];
                }
            }
        }

        // M20: push this block's final output values into whichever taps
        // are currently subscribed. Runs after every step above, so every
        // buffer already holds its complete, final value for this block —
        // no ordering dependency on which step happens to write which
        // buffer. Lock-free, no allocation: Tap::push() is itself RT-safe
        // (SPSC ring buffer, overwrite-oldest), and tapForBufferIndex's
        // element loads are plain atomic reads.
        if (tapForBufferIndex && arePreviewTapsEnabled())
        {
            const auto numBuffers = (int) blockBuffers.size();
            for (int i = 0; i < numBuffers; ++i)
            {
                // The instance axis: only the instance a group's previews show
                // is pushed, and only while it runs (otherwise its buffer is stale).
                if ((size_t) i < bufferInstance.size() && bufferInstance[(size_t) i].first >= 0)
                {
                    const auto [group, instance] = bufferInstance[(size_t) i];
                    const auto& runtime = *instanceGroups[(size_t) group];
                    if (instance != runtime.previewInstance.load (std::memory_order_relaxed)
                        || ! runtime.instances[(size_t) instance].run)
                        continue;
                }

                // A phase source (PreviewKind::PhaseLocked) rides along with
                // the samples in the same push, so a folded preview's phase
                // can never disagree with its samples.
                auto* phaseSource = (size_t) i < phaseSourceForBuffer.size() ? phaseSourceForBuffer[(size_t) i] : nullptr;
                PhaseSnapshot snapshot;
                bool snapshotTaken = false;

                for (int n = 0; n < maxTapsPerBuffer; ++n)
                {
                    if (auto* tap = tapForBufferIndex[(size_t) (i * maxTapsPerBuffer + n)].load (std::memory_order_acquire))
                    {
                        tap->push (blockBuffers[(size_t) i].getBlock().getChannelPointer (0), numSamples,
                                   phaseSource != nullptr ? phaseSource->getPhaseTrack() : nullptr);

                        if (phaseSource != nullptr)
                        {
                            if (! snapshotTaken)
                            {
                                phaseSource->capturePhaseSnapshot (snapshot);
                                snapshotTaken = true;
                            }
                            tap->publishSnapshot (snapshot);
                        }
                    }
                }
            }
        }
    }

    void ExecutionPlan::resolvePhaseSources()
    {
        phaseSourceForBuffer.assign (blockBuffers.size(), nullptr);

        // Which node writes each buffer.
        std::vector<juce::String> producerOfBuffer (blockBuffers.size());
        for (const auto& [nodeId, ports] : outputBufferIndexByNodeAndPort)
            for (const auto& [portId, bufferIndex] : ports)
                if (bufferIndex >= 0 && (size_t) bufferIndex < producerOfBuffer.size())
                    producerOfBuffer[(size_t) bufferIndex] = nodeId;

        for (size_t buffer = 0; buffer < blockBuffers.size(); ++buffer)
        {
            // Walk upstream through first inputs; bounded, so a feedback
            // loop just ends without a phase source.
            auto nodeId = producerOfBuffer[buffer];
            for (int depth = 0; depth < 32 && nodeId.isNotEmpty(); ++depth)
            {
                auto* node = getNodeById (nodeId);
                if (node == nullptr)
                    break;
                if (node->isPhaseSource())
                {
                    phaseSourceForBuffer[buffer] = node;
                    break;
                }

                const auto inputs = node->getInputPorts();
                const auto sourcesIt = inputSourceBufferIndexByNodeAndPort.find (nodeId);
                if (inputs.empty() || sourcesIt == inputSourceBufferIndexByNodeAndPort.end())
                    break;
                const auto sourceIt = sourcesIt->second.find (inputs.front().id);
                if (sourceIt == sourcesIt->second.end() || sourceIt->second < 0 || (size_t) sourceIt->second >= producerOfBuffer.size())
                    break;
                nodeId = producerOfBuffer[(size_t) sourceIt->second];
            }
        }
    }

    float ExecutionPlan::readInput (const InputRef& ref, int sampleIndexForBlockBuffer) const noexcept
    {
        switch (ref.kind)
        {
            case InputRef::Kind::BlockBuffer:
                return blockBuffers[(size_t) ref.index].getBlock().getChannelPointer (0)[sampleIndexForBlockBuffer];
            case InputRef::Kind::RegionScalar:
                return regionScalars[(size_t) ref.index];
            case InputRef::Kind::Silence:
            default:
                return 0.0f;
        }
    }

    void ExecutionPlan::applyInstanceLayout (const InstanceLayout& layout)
    {
        instanceGroups.clear();
        copySlotsByBaseId.clear();
        bufferInstance.assign (blockBuffers.size(), { -1, -1 });
        if (layout.groups.empty())
            return;

        for (const auto& group : layout.groups)
        {
            auto runtime = std::make_unique<InstanceGroupRuntime>();
            runtime->originId = group.originId;
            runtime->count = group.count;
            runtime->instances.assign ((size_t) group.count, {});
            runtime->originNodes.assign ((size_t) group.count, nullptr);
            runtime->noteInNodes.assign ((size_t) group.count, nullptr);
            runtime->nodeSlotsByInstance.assign ((size_t) group.count, {});
            runtime->regionStepsByInstance.assign ((size_t) group.count, {});
            instanceGroups.push_back (std::move (runtime));
        }

        // Each slot (every lane included) -> its graph id's (group, instance).
        std::vector<std::pair<int, int>> tagOfSlot (nodes.size(), { -1, -1 });
        for (const auto& [id, slot] : nodeIdToSlot)
        {
            int instance = -1;
            const auto base = baseNodeId (id, &instance);
            const auto group = instance >= 0 ? layout.groupOf (base) : -1;
            const auto& lanes = (size_t) slot < laneSlotsBySlot.size() ? laneSlotsBySlot[(size_t) slot] : std::vector<int> { slot };
            for (const auto laneSlot : lanes)
            {
                copySlotsByBaseId[base].push_back (laneSlot);
                if (group >= 0)
                {
                    tagOfSlot[(size_t) laneSlot] = { group, instance };
                    instanceGroups[(size_t) group]->nodeSlotsByInstance[(size_t) instance].push_back (laneSlot);
                }
            }
            if (group >= 0)
            {
                auto& runtime = *instanceGroups[(size_t) group];
                if (base == runtime.originId)
                    runtime.originNodes[(size_t) instance] = nodes[(size_t) slot].get();
                if (const auto type = nodeIdToType.find (id); type != nodeIdToType.end() && type->second == "io.noteIn")
                    runtime.noteInNodes[(size_t) instance] = nodes[(size_t) slot].get();
            }
        }
        for (auto& [base, slots] : copySlotsByBaseId)
            std::sort (slots.begin(), slots.end());

        for (size_t s = 0; s < steps.size(); ++s)
        {
            auto& step = steps[s];
            if (step.kind == Step::Kind::Block)
            {
                std::tie (step.block.group, step.block.instance) = tagOfSlot[(size_t) step.block.nodeSlot];
                if (step.block.group >= 0)
                    for (const auto index : step.block.outputBufferIndices)
                        bufferInstance[(size_t) index] = { step.block.group, step.block.instance };
            }
            else
            {
                auto& region = step.region;
                if (! region.nodeSlotsInOrder.empty())
                    std::tie (region.group, region.instance) = tagOfSlot[(size_t) region.nodeSlotsInOrder.front()];
                region.scalarIndices.clear();
                for (const auto& indices : region.outputScalarIndices)
                    region.scalarIndices.insert (region.scalarIndices.end(), indices.begin(), indices.end());
                if (region.group >= 0)
                {
                    instanceGroups[(size_t) region.group]->regionStepsByInstance[(size_t) region.instance].push_back ((int) s);
                    for (const auto& external : region.externalOutputs)
                        bufferInstance[(size_t) external.bufferIndex] = { region.group, region.instance };
                }
            }
        }

        // Each reduce sums one group: hand it that group's runtime.
        for (const auto& [id, slot] : nodeIdToSlot)
        {
            const auto type = nodeIdToType.find (id);
            if (type == nodeIdToType.end() || type->second != "life.reduce")
                continue;
            auto* first = dynamic_cast<nodes::LifeReduceNode*> (nodes[(size_t) slot].get());
            const auto group = first != nullptr ? first->getGroupIndex() : -1;
            if (group < 0 || (size_t) group >= instanceGroups.size())
                continue;
            const auto& lanes = (size_t) slot < laneSlotsBySlot.size() ? laneSlotsBySlot[(size_t) slot] : std::vector<int> { slot };
            for (const auto laneSlot : lanes)
                if (auto* reduce = dynamic_cast<nodes::LifeReduceNode*> (nodes[(size_t) laneSlot].get()))
                    reduce->setRuntime (instanceGroups[(size_t) group].get());
        }
    }

    void ExecutionPlan::resetInstance (int group, int instance) noexcept
    {
        if (group < 0 || (size_t) group >= instanceGroups.size())
            return;
        auto& runtime = *instanceGroups[(size_t) group];
        if (instance < 0 || instance >= runtime.count)
            return;
        for (const auto slot : runtime.nodeSlotsByInstance[(size_t) instance])
            nodes[(size_t) slot]->reset();
        for (const auto stepIndex : runtime.regionStepsByInstance[(size_t) instance])
            for (const auto scalar : steps[(size_t) stepIndex].region.scalarIndices)
                regionScalars[(size_t) scalar] = 0.0f;
    }

    std::vector<int> ExecutionPlan::findOutputBufferIndicesForAllInstances (const juce::String& baseNodeId, const juce::String& portId) const
    {
        std::vector<int> indices;
        if (const auto index = findOutputBufferIndex (baseNodeId, portId); index >= 0)
            indices.push_back (index);
        for (int i = 0;; ++i)
        {
            const auto index = findOutputBufferIndex (instanceNodeId (baseNodeId, i), portId);
            if (index < 0)
                break;
            indices.push_back (index);
        }
        return indices;
    }

    std::vector<int> ExecutionPlan::findTappableBufferIndicesForAllInstances (const juce::String& baseNodeId, const juce::String& portId) const
    {
        if (const auto index = findTappableBufferIndex (baseNodeId, portId); index >= 0)
            return { index };
        std::vector<int> indices;
        for (int i = 0;; ++i)
        {
            const auto id = instanceNodeId (baseNodeId, i);
            if (nodeIdToSlot.find (id) == nodeIdToSlot.end())
                break;
            if (const auto index = findTappableBufferIndex (id, portId); index >= 0)
                indices.push_back (index);
        }
        return indices;
    }
}
