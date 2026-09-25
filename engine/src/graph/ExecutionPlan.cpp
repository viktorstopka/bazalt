#include "bazalt/engine/graph/ExecutionPlan.h"

namespace bazalt::engine
{
    void ExecutionPlan::process (int numSamples) noexcept
    {
        jassert (numSamples <= maxBlockSize);

        const float* inputPtrs[maxPortsPerNode];
        float* outputPtrs[maxPortsPerNode];

        for (auto& step : steps)
        {
            if (step.kind == Step::Kind::Block)
            {
                auto& blockStep = step.block;
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

                    if (region.externalOutputBufferIndex >= 0)
                    {
                        const auto& regionOutputIndices = region.outputScalarIndices[(size_t) region.outputRegionPosition];
                        const auto value = regionScalars[(size_t) regionOutputIndices[(size_t) region.outputPortIndexInNode]];
                        blockBuffers[(size_t) region.externalOutputBufferIndex].getBlock().getChannelPointer (0)[s] = value;
                    }
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
                if (auto* tap = tapForBufferIndex[(size_t) i].load (std::memory_order_acquire))
                    tap->push (blockBuffers[(size_t) i].getBlock().getChannelPointer (0), numSamples);
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
}
