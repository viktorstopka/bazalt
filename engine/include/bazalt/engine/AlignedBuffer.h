#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace bazalt::engine
{
    /** Owned, SIMD-aligned, structure-of-arrays sample storage: a HeapBlock
        paired with an AudioBlock view, the same pattern JUCE uses
        internally. No SIMD kernels land until later, but choosing this
        layout now means that's a local change, not a data restructure
        (ARCHITECTURE.md §5).
    */
    class AlignedBuffer
    {
    public:
        AlignedBuffer() = default;

        void resize (size_t numChannels, size_t numSamples)
        {
            // The HeapBlock<char>, AudioBlock<float> constructor allocates
            // (and aligns) storage itself — it resizes storage to fit.
            block = juce::dsp::AudioBlock<float> (storage, numChannels, numSamples);
        }

        void clear() noexcept
        {
            if (block.getNumSamples() > 0)
                block.clear();
        }

        juce::dsp::AudioBlock<float>& getBlock() noexcept { return block; }
        const juce::dsp::AudioBlock<float>& getBlock() const noexcept { return block; }

        size_t getNumChannels() const noexcept { return block.getNumChannels(); }
        size_t getNumSamples() const noexcept { return block.getNumSamples(); }

    private:
        juce::HeapBlock<char> storage;
        juce::dsp::AudioBlock<float> block;

        JUCE_DECLARE_NON_COPYABLE (AlignedBuffer)
    };
}
