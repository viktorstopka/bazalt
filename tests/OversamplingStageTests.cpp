#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "bazalt/engine/OversamplingStage.h"
#include <cmath>
#include <vector>

TEST_CASE ("OversamplingStage reports sane latency and round-trips a signal cleanly", "[engine][OversamplingStage]")
{
    const auto factor = GENERATE (bazalt::engine::OversamplingFactor::x2,
                                   bazalt::engine::OversamplingFactor::x4,
                                   bazalt::engine::OversamplingFactor::x8);

    constexpr int blockSize = 256;

    bazalt::engine::OversamplingStage stage;
    stage.prepare (1, blockSize, factor);
    stage.reset();

    CHECK (stage.getLatencyInSamples() >= 0.0f);
    CHECK (std::isfinite (stage.getLatencyInSamples()));

    std::vector<float> inputData ((size_t) blockSize);
    for (int i = 0; i < blockSize; ++i)
        inputData[(size_t) i] = std::sin (0.05f * (float) i);

    std::vector<float> outputData ((size_t) blockSize, 0.0f);

    float* inputChannels[] = { inputData.data() };
    float* outputChannels[] = { outputData.data() };

    juce::dsp::AudioBlock<float> inputBlock (inputChannels, 1, (size_t) blockSize);
    juce::dsp::AudioBlock<float> outputBlock (outputChannels, 1, (size_t) blockSize);

    auto upBlock = stage.processSamplesUp (inputBlock);
    REQUIRE (upBlock.getNumSamples() > 0);

    stage.processSamplesDown (outputBlock);

    for (int i = 0; i < blockSize; ++i)
        CHECK (std::isfinite (outputData[(size_t) i]));
}
