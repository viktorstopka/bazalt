#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/nodes/NoiseBurstNode.h"
#include "bazalt/engine/nodes/IoNoteInNode.h"
#include <cmath>
#include <vector>

using namespace bazalt::engine;

namespace
{
    float rmsOf (const std::vector<float>& samples)
    {
        double sumSquares = 0.0;
        for (auto s : samples)
            sumSquares += (double) s * (double) s;
        return (float) std::sqrt (sumSquares / (double) samples.size());
    }
}

TEST_CASE ("Voice proof graph is silent before noteOn, sounds during sustain, decays after noteOff",
           "[engine][ProofGraphs][voice]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;

    auto graph = buildVoiceProofGraph();
    auto factory = buildDefaultNodeFactory();
    auto result = GraphCompiler::compile (graph, factory, { sampleRate, blockSize }, 1);
    REQUIRE (result.success);

    auto& plan = result.plan;

    // M18 (ADR-0024): "osc"/"env" no longer take frequency/gate directly —
    // instance.allocator's real pitch/gate ports drive them now, fed by
    // io.noteIn.
    auto* noteIn = dynamic_cast<nodes::IoNoteInNode*> (plan.getNodeById ("noteIn"));
    REQUIRE (noteIn != nullptr);

    const auto* outputPtr = plan.blockBuffers[(size_t) plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);

    // Before noteOn: silence.
    plan.process (blockSize);
    std::vector<float> beforeNoteOn (outputPtr, outputPtr + blockSize);
    CHECK (rmsOf (beforeNoteOn) == 0.0f);

    // After noteOn, well into sustain: real signal.
    noteIn->injectNoteOn (57.0f, 1.0f); // A3, ~220Hz — matches the tone this test has always rendered
    for (int i = 0; i < 20; ++i)
        plan.process (blockSize);

    std::vector<float> duringSustain (outputPtr, outputPtr + blockSize);
    CHECK (rmsOf (duringSustain) > 0.05f);

    for (auto s : duringSustain)
        REQUIRE (std::isfinite (s));

    // After noteOff and enough blocks for the release tail to finish:
    // back to silence.
    noteIn->injectNoteOff();
    for (int i = 0; i < 40; ++i)
        plan.process (blockSize);

    std::vector<float> afterRelease (outputPtr, outputPtr + blockSize);
    CHECK (rmsOf (afterRelease) < 0.001f);
}

TEST_CASE ("Karplus-Strong proof graph produces a decaying plucked-string tone", "[engine][ProofGraphs][karplus-strong]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;

    auto graph = buildKarplusStrongProofGraph();
    auto factory = buildDefaultNodeFactory();
    auto result = GraphCompiler::compile (graph, factory, { sampleRate, blockSize }, 1);
    REQUIRE (result.success);

    auto& plan = result.plan;
    plan.getNodeById ("delay")->setParameter ("delay.line.samples", (float) (sampleRate / 220.0));

    auto* excite = dynamic_cast<nodes::NoiseBurstNode*> (plan.getNodeById ("excite"));
    REQUIRE (excite != nullptr);
    excite->trigger ((int) (sampleRate * 0.005));

    const auto* outputPtr = plan.blockBuffers[(size_t) plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);

    // Render in ~0.1s windows, tracking RMS per window — Karplus-Strong
    // with damping < 1 must decay: each window's RMS should not exceed a
    // generous multiple of the previous one once past the initial pluck.
    const auto windowBlocks = (int) std::ceil ((sampleRate * 0.1) / blockSize);
    std::vector<float> windowRms;

    for (int w = 0; w < 15; ++w)
    {
        std::vector<float> windowSamples;
        for (int b = 0; b < windowBlocks; ++b)
        {
            plan.process (blockSize);
            windowSamples.insert (windowSamples.end(), outputPtr, outputPtr + blockSize);
        }

        for (auto s : windowSamples)
            REQUIRE (std::isfinite (s));

        windowRms.push_back (rmsOf (windowSamples));
    }

    REQUIRE (windowRms.front() > 0.01f); // the pluck is actually audible

    // Skip window 0 (the transient pluck itself can be louder than the
    // settled loop) and require an overall decaying trend from window 1
    // onward — matches what the render-cli WAV showed empirically.
    for (size_t i = 2; i < windowRms.size(); ++i)
        CHECK (windowRms[i] <= windowRms[1] * 1.05f);

    CHECK (windowRms.back() < windowRms[1]);
}
