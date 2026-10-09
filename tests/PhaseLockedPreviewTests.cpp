// PreviewKind::PhaseLocked end to end: a compiled graph pushes samples +
// phase track + PhaseSnapshot into a tap; AnalysisThread turns them into a
// frame whose horizontal axis is phase. Covers both modes and the "phase
// follows the cable" resolution through a non-phase node.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/CurveData.h"
#include "bazalt/engine/telemetry/AnalysisThread.h"
#include "bazalt/engine/telemetry/TelemetryHub.h"
#include <cmath>
#include <vector>

using namespace bazalt::engine;

namespace
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;

    struct Rig
    {
        TelemetryHub hub;
        AnalysisThread analysis { hub };
        CompileResult compiled;
        Tap* tap = nullptr;

        Rig (const NodeGraph& graph, const juce::String& nodeId, const juce::String& portId, bool fold)
        {
            hub.prepare (8192, maxTelemetryFrameBytes);
            analysis.prepare (sampleRate);
            tap = hub.subscribeTap ("t");
            TapSettings settings;
            settings.phaseLockedFold = fold;
            hub.setTapSettings ("t", settings);

            auto factory = buildDefaultNodeFactory();
            compiled = GraphCompiler::compile (graph, factory, { sampleRate, blockSize }, 1);
            REQUIRE (compiled.success);
            REQUIRE (compiled.plan.addTapForBufferIndex (compiled.plan.findTappableBufferIndex (nodeId, portId), tap));
        }

        void run (int blocks)
        {
            for (int b = 0; b < blocks; ++b)
            {
                compiled.plan.process (blockSize);
                if (b % 4 == 3)
                    analysis.processSlotForTesting (0, 0.01);
            }
            analysis.processSlotForTesting (0, 0.01);
        }

        std::vector<float> frame() const
        {
            std::vector<std::byte> bytes (maxTelemetryFrameBytes);
            const auto size = hub.getFrameBufferBySlot (0, TelemetryFrameType::PhaseLocked)->readLatest (bytes.data(), bytes.size());
            if (size == 0)
                return {};
            TelemetryFrameHeader header;
            const float* payload = nullptr;
            REQUIRE (parseTelemetryFrame (bytes.data(), size, header, payload));
            return { payload, payload + header.payloadNumFloats };
        }
    };

    NodeGraph oscillatorGraph (const CurveDocument& shape, float frequency, bool throughGain, float amplitude = 1.0f)
    {
        NodeGraph graph;
        graph.addNode (withContent ({ "osc", "source.oscillator", {},
                                      { { "source.oscillator.frequency", frequency }, { "source.oscillator.amplitude", amplitude } }, {} },
                                    shape));
        if (throughGain)
        {
            graph.addNode ({ "gain", "math.multiply", {}, { { "in.1", 0.5f } }, {} });
            graph.addConnection ({ "osc", "out", "gain", "in.0" });
            graph.setOutput ("gain", "out");
        }
        else
            graph.setOutput ("osc", "out");
        return graph;
    }
}

TEST_CASE ("An oscillator's own phase-locked frame folds its real output by its phase", "[engine][telemetry][PhaseLocked]")
{
    // A drawn curve has no closed-form render function (1b), so even the
    // oscillator's own preview folds: after enough cycles every bin is the
    // waveform at that phase.
    Rig rig (oscillatorGraph (CurveDocument::sine(), 441.3f, false), "osc", "out", true);
    rig.run (400);
    const auto frame = rig.frame();
    REQUIRE (frame.size() == (size_t) phaseLockedPoints + 2);
    CHECK (frame[1] == Catch::Approx (441.3f));
    CHECK (frame[0] >= 0.0f);
    CHECK (frame[0] < (float) phaseLockedCycles);
    for (int i = 0; i < phaseLockedPoints; i += 7)
    {
        const auto t = (double) i / phaseLockedPointsPerCycle;
        CHECK (frame[(size_t) i + 2] == Catch::Approx (std::sin (juce::MathConstants<double>::twoPi * t)).margin (0.03)); // a bin is 1/256 of a cycle wide
    }
}

TEST_CASE ("Render mode publishes nothing for a source with no render function, rather than a guess",
           "[engine][telemetry][PhaseLocked]")
{
    Rig rig (oscillatorGraph (CurveDocument::sine(), 440.0f, false), "osc", "out", false);
    rig.run (8);
    CHECK (rig.frame().empty());
}

TEST_CASE ("The folded frame shows the shape's duty and the amplitude", "[engine][telemetry][PhaseLocked]")
{
    Rig rig (oscillatorGraph (CurveDocument::square (0.25f), 441.3f, false, 0.5f), "osc", "out", true);
    rig.run (400);
    const auto frame = rig.frame();
    REQUIRE (frame.size() > 2);

    int high = 0;
    for (int i = 0; i < phaseLockedPointsPerCycle; ++i)
    {
        const auto v = frame[(size_t) i + 2];
        CHECK (std::fabs (v) <= 0.5f * 1.2f); // amplitude 0.5, plus the band-limited (Gibbs) overshoot
        if (v > 0.0f)
            ++high;
    }
    CHECK ((double) high / phaseLockedPointsPerCycle == Catch::Approx (0.25).margin (0.03));
}

TEST_CASE ("Phase follows the cable: a non-phase node's output folds against the upstream oscillator's phase",
           "[engine][telemetry][PhaseLocked]")
{
    // A saw through a 0.5 gain, folded by the saw's own phase: after enough
    // cycles every bin is filled and the result is the saw at half height.
    Rig rig (oscillatorGraph (CurveDocument::saw(), 441.3f, true), "gain", "out", true);
    rig.run (400); // ~2.3 s, ~1000 cycles
    const auto frame = rig.frame();
    REQUIRE (frame.size() == (size_t) phaseLockedPoints + 2);
    CHECK (frame[1] == Catch::Approx (441.3f));

    int compared = 0;
    for (int i = 0; i < phaseLockedPoints; ++i)
    {
        const auto v = frame[(size_t) i + 2];
        REQUIRE_FALSE (std::isnan (v));
        const auto t = std::fmod ((double) i / phaseLockedPointsPerCycle, 1.0);
        if (t < 0.1 || t > 0.9)
            continue; // the band-limited reset and its ringing
        CHECK (v == Catch::Approx (0.5 * (2.0 * t - 1.0)).margin (0.03));
        ++compared;
    }
    CHECK (compared > phaseLockedPoints / 2);
}

TEST_CASE ("A buffer with no phase source anywhere upstream publishes no phase-locked frame",
           "[engine][telemetry][PhaseLocked]")
{
    NodeGraph graph;
    graph.addNode ({ "c", "util.constant", {}, {}, {} });
    graph.setOutput ("c", "out");
    Rig rig (graph, "c", "out", true);
    rig.run (8);
    CHECK (rig.frame().empty());
}
