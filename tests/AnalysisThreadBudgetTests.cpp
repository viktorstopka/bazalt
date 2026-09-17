#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/telemetry/AnalysisThread.h"
#include "bazalt/engine/telemetry/TelemetryHub.h"
#include <cmath>
#include <vector>

using namespace bazalt::engine;

TEST_CASE ("Under a per-cycle budget too small for every active tap, round-robin still reaches all of them "
           "over several cycles rather than starving any one",
           "[engine][telemetry][AnalysisThread][NODE_EDITOR]")
{
    // NODE_EDITOR.md §3: "degrade gracefully (lower preview rate, coarser
    // decimation) rather than dropping frames." A budget of 0ms forces
    // AnalysisThread::run() to process exactly one active slot per drain
    // cycle (the time check runs *after* processing at least one, so it
    // never processes zero) — a fully deterministic way to prove the
    // round-robin sweep, without depending on real FFT timing variance.
    constexpr int numTaps = 5;

    TelemetryHub hub;
    hub.prepare (2048, 4096);

    std::vector<Tap*> taps;
    for (int i = 0; i < numTaps; ++i)
        taps.push_back (hub.subscribeTap ("tap" + juce::String (i)));

    std::vector<float> block (256);
    for (int i = 0; i < 256; ++i)
        block[(size_t) i] = std::sin (0.1f * (float) i);

    for (auto* tap : taps)
        tap->push (block.data(), (int) block.size());

    AnalysisThread analysis (hub);
    analysis.prepare (44100.0);
    analysis.setMaxProcessingMsPerCycleForTesting (0.0);
    analysis.startThread();

    // drainIntervalMs is 10ms internally; numTaps cycles (one tap each)
    // comfortably finish well within this budget of wall-clock time.
    juce::Thread::sleep (numTaps * 30);

    analysis.stopThread (2000);

    std::vector<std::byte> dest (4096);

    for (int i = 0; i < numTaps; ++i)
    {
        auto* buffer = hub.getFrameBuffer ("tap" + juce::String (i), TelemetryFrameType::Meter);
        REQUIRE (buffer != nullptr);

        const auto numBytes = buffer->readLatest (dest.data(), dest.size());
        CHECK (numBytes > 0); // every tap got at least one published frame, not just the first ones visited
    }
}
