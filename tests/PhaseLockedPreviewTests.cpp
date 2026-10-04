// PreviewKind::PhaseLocked end to end: a compiled graph pushes samples +
// phase track + PhaseSnapshot into a tap; AnalysisThread turns them into a
// frame whose horizontal axis is phase. Covers both modes and the "phase
// follows the cable" resolution through a non-phase node.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/SineOscillatorNode.h"
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

    NodeGraph oscillatorGraph (const juce::String& type, float frequency, bool throughGain)
    {
        NodeGraph graph;
        graph.addNode ({ "osc", type, {}, { { type + ".frequency", frequency } }, {} });
        if (throughGain)
        {
            graph.addNode ({ "gain", "mix.gain", {}, { { "gain", 0.5f } }, {} });
            graph.addConnection ({ "osc", "out", "gain", "audio" });
            graph.setOutput ("gain", "out");
        }
        else
            graph.setOutput ("osc", "out");
        return graph;
    }
}

TEST_CASE ("A generator's phase-locked frame is its own waveform over a fixed number of cycles, whatever its rate",
           "[engine][telemetry][PhaseLocked]")
{
    // "A 0.3 Hz square and a 440 Hz square render identically." For a sine,
    // whose shape carries no band-limiting, that is exact.
    auto shapeAt = [] (float frequency)
    {
        Rig rig (oscillatorGraph ("osc.sine", frequency, false), "osc", "out", false);
        rig.run (8);
        return rig.frame();
    };
    const auto slow = shapeAt (0.3f);
    const auto fast = shapeAt (440.0f);
    REQUIRE (slow.size() == (size_t) phaseLockedPoints + 2);
    REQUIRE (fast.size() == slow.size());

    CHECK (slow[1] == Catch::Approx (0.3f));
    CHECK (fast[1] == Catch::Approx (440.0f));
    for (size_t i = 2; i < slow.size(); ++i)
        CHECK (slow[i] == Catch::Approx (fast[i]).margin (1.0e-5f));

    // Aligned to phase zero: sin starts at 0, peaks a quarter cycle in.
    CHECK (slow[2] == Catch::Approx (0.0f).margin (1.0e-5f));
    CHECK (slow[2 + phaseLockedPointsPerCycle / 4] == Catch::Approx (1.0f).margin (1.0e-4f));

    // The playhead is inside the displayed span.
    CHECK (fast[0] >= 0.0f);
    CHECK (fast[0] < (float) phaseLockedCycles);
}

TEST_CASE ("The phase-locked frame follows the oscillator's current, modulated parameters",
           "[engine][telemetry][PhaseLocked]")
{
    NodeGraph graph;
    graph.addNode ({ "osc", "osc.square", {}, { { "osc.square.frequency", 2.0f }, { "osc.square.amplitude", 0.5f },
                                                 { "osc.square.pulseWidth", 0.25f } }, {} });
    graph.setOutput ("osc", "out");
    Rig rig (graph, "osc", "out", false);
    rig.run (8);
    const auto frame = rig.frame();
    REQUIRE (frame.size() > 2);

    int high = 0, offLevel = 0;
    for (int i = 0; i < phaseLockedPointsPerCycle; ++i)
    {
        const auto v = frame[(size_t) i + 2];
        CHECK (std::fabs (v) <= 0.5f + 1.0e-3f); // amplitude 0.5
        if (std::fabs (std::fabs (v) - 0.5f) > 1.0e-3f)
            ++offLevel; // only the points exactly on an edge (the band-limited jump's midpoint)
        if (v > 0.0f)
            ++high;
    }
    CHECK (offLevel <= 2);
    CHECK ((double) high / phaseLockedPointsPerCycle == Catch::Approx (0.25).margin (0.01));
}

TEST_CASE ("Phase follows the cable: a non-phase node's output folds against the upstream oscillator's phase",
           "[engine][telemetry][PhaseLocked]")
{
    // A saw through a 0.5 gain, folded by the saw's own phase: after enough
    // cycles every bin is filled and the result is the saw at half height.
    Rig rig (oscillatorGraph ("osc.saw", 441.3f, true), "gain", "out", true);
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
        if (t < 0.03 || t > 0.97)
            continue; // the band-limited reset
        CHECK (v == Catch::Approx (0.5 * (2.0 * t - 1.0)).margin (0.02));
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
