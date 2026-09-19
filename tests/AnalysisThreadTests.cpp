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
    hub.prepare (8192, 16384);

    AnalysisThread analysis (hub);
    analysis.prepare (sampleRate);
    analysis.startThread();

    auto* tap = hub.subscribeTap ("main");
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

TEST_CASE ("A \"demo.\"-prefixed tap gets synthetic telemetry with no external pusher at all",
           "[engine][telemetry][AnalysisThread][NODE_EDITOR]")
{
    // M8's UI-only rendering stress test subscribes many synthetic
    // per-cable taps (StressTestCanvas.tsx) that have nothing on the
    // audio-thread side pushing real data — TelemetryHub.h's subscribeTap()
    // note. Proves AnalysisThread really does generate their content
    // itself, end to end through the same publish path real taps use.
    TelemetryHub hub;
    hub.prepare (2048, 4096);

    hub.subscribeTap ("demo.cable42"); // never pushed to by anything else in this test

    AnalysisThread analysis (hub);
    analysis.prepare (44100.0);
    analysis.startThread();

    juce::Thread::sleep (60);
    analysis.stopThread (2000);

    auto* buffer = hub.getFrameBuffer ("demo.cable42", TelemetryFrameType::Meter);
    REQUIRE (buffer != nullptr);

    std::vector<std::byte> dest (4096);
    const auto numBytes = buffer->readLatest (dest.data(), dest.size());
    REQUIRE (numBytes > 0);

    TelemetryFrameHeader header;
    const float* payload = nullptr;
    REQUIRE (parseTelemetryFrame (dest.data(), numBytes, header, payload));
    REQUIRE (header.payloadNumFloats >= 1);
    CHECK (std::isfinite (payload[0]));
}

TEST_CASE ("AnalysisThread publishes all three frame types for every tap", "[engine][telemetry][AnalysisThread]")
{
    TelemetryHub hub;
    hub.prepare (8192, 16384);

    AnalysisThread analysis (hub);
    analysis.prepare (44100.0);
    analysis.startThread();

    auto* mainTap = hub.subscribeTap ("main");
    auto* auxTap = hub.subscribeTap ("aux1");
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

TEST_CASE ("A Waveform-only tap subscription never triggers Spectrum or Meter publishing",
           "[engine][telemetry][AnalysisThread][M20]")
{
    TelemetryHub hub;
    hub.prepare (8192, 16384);

    AnalysisThread analysis (hub);
    analysis.prepare (44100.0);
    analysis.startThread();

    // Waveform previews only ever read Oscilloscope frames
    // (PluginProcessor.cpp's frameTypesNeededFor()) — subscribe with
    // exactly that scope, matching what subscribeVisualizationTap() does
    // for a real Waveform-kind preview.
    auto* tap = hub.subscribeTap ("node:test:out", { true, false, false });
    REQUIRE (tap != nullptr);

    std::vector<float> block (2048);
    for (int i = 0; i < 2048; ++i)
        block[(size_t) i] = std::sin (0.05f * (float) i);

    for (int i = 0; i < 20; ++i)
    {
        tap->push (block.data(), (int) block.size());
        juce::Thread::sleep (10);
    }

    analysis.stopThread (2000);

    std::vector<std::byte> dest (16384);

    auto* oscilloscopeBuffer = hub.getFrameBuffer ("node:test:out", TelemetryFrameType::Oscilloscope);
    REQUIRE (oscilloscopeBuffer != nullptr);
    CHECK (oscilloscopeBuffer->readLatest (dest.data(), dest.size()) > 0);

    // Never published — AnalysisThread must have skipped the FFT/ballistics
    // work entirely for this tap, not just declined to publish afterward.
    auto* spectrumBuffer = hub.getFrameBuffer ("node:test:out", TelemetryFrameType::Spectrum);
    REQUIRE (spectrumBuffer != nullptr);
    CHECK (spectrumBuffer->readLatest (dest.data(), dest.size()) == 0);

    auto* meterBuffer = hub.getFrameBuffer ("node:test:out", TelemetryFrameType::Meter);
    REQUIRE (meterBuffer != nullptr);
    CHECK (meterBuffer->readLatest (dest.data(), dest.size()) == 0);
}
