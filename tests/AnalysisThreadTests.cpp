#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/telemetry/AnalysisThread.h"
#include "bazalt/engine/telemetry/TelemetryHub.h"
#include "bazalt/engine/RtAllocationTrap.h"
#include <atomic>
#include <cmath>
#include <vector>

using namespace bazalt::engine;

TEST_CASE ("AnalysisThread never forces a concurrently-running simulated audio thread to allocate",
           "[engine][telemetry][AnalysisThread]")
{
    // This is the M4 exit criterion made concrete: "Analysis thread never
    // blocks the audio thread (verified by the RT-safety checks from M1
    // running concurrently with telemetry active)." A real background
    // AnalysisThread runs concurrently with a simulated audio-thread loop
    // that pushes samples into the same tap wrapped in
    // ScopedAudioThreadAllocationTrap (M1) — if anything on the telemetry
    // side forced the "audio" side to allocate, the trap would catch it.
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    TelemetryHub hub;
    hub.prepare ({ "main" }, 8192, 16384);

    AnalysisThread analysis (hub);
    analysis.prepare (sampleRate);
    analysis.startThread();

    auto* tap = hub.getTap ("main");
    REQUIRE (tap != nullptr);

    std::atomic<bool> allocationTrapViolation { false };
    int blocksProcessed = 0;

    std::vector<float> block ((size_t) blockSize);
    for (int i = 0; i < blockSize; ++i)
        block[(size_t) i] = std::sin (0.1f * (float) i);

    const auto start = juce::Time::getMillisecondCounterHiRes();

    while (juce::Time::getMillisecondCounterHiRes() - start < 200.0) // ~200ms of simulated audio
    {
        try
        {
            ScopedAudioThreadAllocationTrap trap;
            tap->push (block.data(), blockSize);
        }
        catch (const AudioThreadAllocationViolation&)
        {
            allocationTrapViolation.store (true);
            break;
        }

        ++blocksProcessed;
        juce::Thread::sleep (1);
    }

    analysis.stopThread (2000);

    CHECK_FALSE (allocationTrapViolation.load());
    CHECK (blocksProcessed > 0);
}

TEST_CASE ("AnalysisThread publishes all three frame types for every tap", "[engine][telemetry][AnalysisThread]")
{
    TelemetryHub hub;
    hub.prepare ({ "main", "aux1" }, 8192, 16384);

    AnalysisThread analysis (hub);
    analysis.prepare (44100.0);
    analysis.startThread();

    auto* mainTap = hub.getTap ("main");
    auto* auxTap = hub.getTap ("aux1");
    REQUIRE (mainTap != nullptr);
    REQUIRE (auxTap != nullptr);

    std::vector<float> block (2048);
    for (int i = 0; i < 2048; ++i)
        block[(size_t) i] = std::sin (0.05f * (float) i);

    for (int i = 0; i < 20; ++i)
    {
        mainTap->push (block.data(), (int) block.size());
        auxTap->push (block.data(), (int) block.size());
        juce::Thread::sleep (10);
    }

    analysis.stopThread (2000);

    std::vector<std::byte> dest (16384);

    for (const auto& tapName : { juce::String ("main"), juce::String ("aux1") })
    {
        for (auto type : { TelemetryFrameType::Oscilloscope, TelemetryFrameType::Spectrum, TelemetryFrameType::Meter })
        {
            auto* buffer = hub.getFrameBuffer (tapName, type);
            REQUIRE (buffer != nullptr);

            const auto numBytes = buffer->readLatest (dest.data(), dest.size());
            REQUIRE (numBytes > 0);

            TelemetryFrameHeader header;
            const float* payload = nullptr;
            REQUIRE (parseTelemetryFrame (dest.data(), numBytes, header, payload));
            CHECK (header.frameType == type);

            for (uint32_t i = 0; i < header.payloadNumFloats; ++i)
                REQUIRE (std::isfinite (payload[i]));
        }
    }
}
