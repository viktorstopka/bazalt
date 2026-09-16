#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/PolyBlepOscillator.h"
#include "bazalt/engine/SvfFilter.h"
#include <algorithm>
#include <vector>

namespace
{
    // No node algorithm may depend on block size for its math
    // (ARCHITECTURE.md §5) — rendering the same total number of samples
    // through different call-chunking must produce bit-identical output.
    std::vector<float> renderInChunks (int totalSamples, int chunkSize)
    {
        bazalt::engine::PolyBlepOscillator oscillator;
        oscillator.prepare (44100.0);
        oscillator.setWaveform (bazalt::engine::OscillatorWaveform::Saw);
        oscillator.setFrequency (330.0f);

        bazalt::engine::SvfFilter filter;
        filter.prepare (44100.0, (uint32_t) totalSamples, 1);
        filter.setType (bazalt::engine::SvfFilterType::Lowpass);
        filter.setCutoffFrequency (1500.0f);
        filter.setResonance (1.2f);

        std::vector<float> output ((size_t) totalSamples);
        int rendered = 0;

        while (rendered < totalSamples)
        {
            const auto thisChunk = std::min (chunkSize, totalSamples - rendered);

            for (int i = 0; i < thisChunk; ++i)
            {
                const auto raw = oscillator.renderNextSample();
                output[(size_t) (rendered + i)] = filter.processSample (0, raw);
            }

            rendered += thisChunk;
        }

        return output;
    }
}

TEST_CASE ("Oscillator + SvfFilter output is identical regardless of block size", "[engine][block-size-invariance]")
{
    constexpr int totalSamples = 2000;
    const auto reference = renderInChunks (totalSamples, totalSamples); // one giant "block"

    for (int chunkSize : { 1, 7, 64, 512 })
    {
        INFO ("chunk size = " << chunkSize);
        const auto candidate = renderInChunks (totalSamples, chunkSize);

        REQUIRE (candidate.size() == reference.size());

        for (size_t i = 0; i < reference.size(); ++i)
            CHECK (candidate[i] == reference[i]);
    }
}
