#pragma once

#include <juce_dsp/juce_dsp.h>
#include <memory>

namespace bazalt::engine
{
    enum class OversamplingFactor
    {
        x2 = 1,
        x4 = 2,
        x8 = 3
    };

    /** Reusable stage wrapping juce::dsp::Oversampling (polyphase IIR
        halfband), selectable 2x/4x/8x, latency read via
        getLatencyInSamples() (ARCHITECTURE.md §5). Not exercised by the
        MVP's hardcoded graph's audio path yet, but built and unit-tested
        now so any future nonlinear node can wrap itself in it trivially.
        Reporting the latency to the host (setLatencySamples()) is a
        plugin-layer concern once a node actually uses this — this wrapper
        only reports the number, it doesn't call back into anything.
    */
    class OversamplingStage
    {
    public:
        void prepare (int numChannels, int maximumBlockSize, OversamplingFactor factor)
        {
            oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
                (size_t) numChannels,
                (size_t) factor,
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);

            oversampling->initProcessing ((size_t) maximumBlockSize);
        }

        void reset() noexcept
        {
            if (oversampling != nullptr)
                oversampling->reset();
        }

        juce::dsp::AudioBlock<float> processSamplesUp (const juce::dsp::AudioBlock<const float>& input) noexcept
        {
            return oversampling->processSamplesUp (input);
        }

        void processSamplesDown (juce::dsp::AudioBlock<float>& output) noexcept
        {
            oversampling->processSamplesDown (output);
        }

        float getLatencyInSamples() const noexcept
        {
            return oversampling != nullptr ? oversampling->getLatencyInSamples() : 0.0f;
        }

        size_t getOversamplingFactor() const noexcept
        {
            return oversampling != nullptr ? oversampling->getOversamplingFactor() : 1;
        }

    private:
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    };
}
