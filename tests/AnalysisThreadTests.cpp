#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/telemetry/AnalysisThread.h"
#include "bazalt/engine/telemetry/TelemetryHub.h"
#include "bazalt/engine/RtAllocationTrap.h"
#include <atomic>
#include <cmath>
#include <utility>
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

TEST_CASE ("AnalysisThread publishes all five frame types for every tap", "[engine][telemetry][AnalysisThread]")
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
        for (auto type : { TelemetryFrameType::Oscilloscope, TelemetryFrameType::Spectrum, TelemetryFrameType::Meter,
                            TelemetryFrameType::EventImpulse, TelemetryFrameType::RollingHistory })
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

TEST_CASE ("A Waveform-only tap subscription never triggers Spectrum, Meter or EventImpulse publishing",
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
    // for a real Waveform-kind preview. Explicitly 5-argument (not a
    // shorter form): TelemetryFrameTypesNeeded's unlisted fields default to
    // true, so a short aggregate-init here would silently leave e.g.
    // rollingHistory at the struct's own default (true) — still
    // "Oscilloscope only" by accident today only because nothing checked
    // that 5th frame type below, not by what the call site actually said
    // (the exact mistake eventImpulse's own addition already caught once).
    auto* tap = hub.subscribeTap ("node:test:out", { true, false, false, false, false });
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

    // The pushed signal (sin(0.05*i), swinging well past 0.5) would have
    // produced real rising-edge detections had this frame type not been
    // excluded from the subscription above — a real assertion, not a
    // vacuously-true "nothing to detect anyway" check.
    auto* eventImpulseBuffer = hub.getFrameBuffer ("node:test:out", TelemetryFrameType::EventImpulse);
    REQUIRE (eventImpulseBuffer != nullptr);
    CHECK (eventImpulseBuffer->readLatest (dest.data(), dest.size()) == 0);

    // Never published either — same "the work itself was skipped, not just
    // the publish" guarantee as the other excluded frame types above.
    auto* rollingHistoryBuffer = hub.getFrameBuffer ("node:test:out", TelemetryFrameType::RollingHistory);
    REQUIRE (rollingHistoryBuffer != nullptr);
    CHECK (rollingHistoryBuffer->readLatest (dest.data(), dest.size()) == 0);
}

// ---- design/Visualization/Ripple.png: AnalysisThread::publishEventImpulse ----
// Synchronous (processSlotForTesting, never startThread()) exactly like
// TapSettingsTests.cpp's own Rig — deterministic, no sleep-based timing race.

namespace
{
    struct EventImpulseRig
    {
        TelemetryHub hub;
        AnalysisThread analysis { hub };
        Tap* tap = nullptr;
        static constexpr size_t slot = 0;
        static constexpr double sampleRate = 44100.0;

        EventImpulseRig()
        {
            hub.prepare (8192, maxTelemetryFrameBytes);
            analysis.prepare (sampleRate);
            tap = hub.subscribeTap ("t"); // default TelemetryFrameTypesNeeded: all four, including eventImpulse
        }

        void push (const std::vector<float>& samples) { tap->push (samples.data(), (int) samples.size()); }
        void drain() { analysis.processSlotForTesting (slot, 0.01); }

        // Empty (not REQUIRE'd non-empty) when nothing new fired this drain
        // — publishEventImpulse() deliberately skips publishing in that
        // case (AnalysisThread.cpp's own comment on why).
        std::vector<float> ages() const { return latestFrame().second; }

        // sequenceNumber alongside the payload — publishEventImpulse()
        // deliberately leaves a stale frame buffer untouched on a drain
        // with nothing new to report (AnalysisThread.cpp's own comment on
        // why), so `ages()` alone can't tell "still the old frame from a
        // previous drain" apart from "a fresh one with the same content
        // would be a strange coincidence" — the sequence number can.
        std::pair<uint64_t, std::vector<float>> latestFrame() const
        {
            std::vector<std::byte> bytes (maxTelemetryFrameBytes);
            const auto size = hub.getFrameBufferBySlot (slot, TelemetryFrameType::EventImpulse)->readLatest (bytes.data(), bytes.size());
            if (size == 0)
                return { 0, {} };

            TelemetryFrameHeader header;
            const float* payload = nullptr;
            REQUIRE (parseTelemetryFrame (bytes.data(), size, header, payload));
            CHECK (header.frameType == TelemetryFrameType::EventImpulse);
            return { header.sequenceNumber, std::vector<float> (payload, payload + header.payloadNumFloats) };
        }
    };

    // A block of `numSamples` zeros with a single rising step to 1.0 at
    // `pulseAtIndex` (inclusive), held high through the rest of the block —
    // exactly the "non-zero this sample = fired" shape SineOscillatorNode's
    // own "sync" port documents, refined by publishEventImpulse's own
    // >= 0.5 rising-edge threshold (MacroNode.h's own Trigger convention).
    std::vector<float> blockWithPulseAt (int numSamples, int pulseAtIndex)
    {
        std::vector<float> block ((size_t) numSamples, 0.0f);
        for (int i = pulseAtIndex; i < numSamples; ++i)
            block[(size_t) i] = 1.0f;
        return block;
    }
}

TEST_CASE ("publishEventImpulse reports one event at the correct age for a single pulse",
           "[engine][telemetry][AnalysisThread][ripple]")
{
    EventImpulseRig rig;

    constexpr int numSamples = 1000;
    constexpr int pulseAtIndex = 400; // 600 samples "ago" by the end of this block
    rig.push (blockWithPulseAt (numSamples, pulseAtIndex));
    rig.drain();

    const auto ages = rig.ages();
    REQUIRE (ages.size() == 1);
    const auto expectedAge = (float) (numSamples - 1 - pulseAtIndex) / (float) EventImpulseRig::sampleRate;
    CHECK (ages[0] == Catch::Approx (expectedAge).margin (1.0e-6));
}

TEST_CASE ("publishEventImpulse reports exactly one event for a held-high gate, not one per sample",
           "[engine][telemetry][AnalysisThread][ripple]")
{
    EventImpulseRig rig;

    // High for the whole back half of the block — a real gate, not a
    // one-sample pulse. Still exactly one rising edge.
    rig.push (blockWithPulseAt (1000, 300));
    rig.drain();

    CHECK (rig.ages().size() == 1);
}

TEST_CASE ("publishEventImpulse reports every distinct pulse within one drain, each at its own age",
           "[engine][telemetry][AnalysisThread][ripple]")
{
    EventImpulseRig rig;

    // Three separate pulses, each back to 0 before the next rises, so each
    // is its own rising edge.
    std::vector<float> block (1000, 0.0f);
    for (int i : { 100, 101, 102 }) block[(size_t) i] = 1.0f;
    for (int i : { 500, 501 }) block[(size_t) i] = 1.0f;
    for (int i : { 900 }) block[(size_t) i] = 1.0f;
    rig.push (block);
    rig.drain();

    const auto ages = rig.ages();
    REQUIRE (ages.size() == 3);
    // Oldest (earliest index -> largest age) first, matching the forward
    // sample-index scan order publishEventImpulse itself uses.
    CHECK (ages[0] > ages[1]);
    CHECK (ages[1] > ages[2]);
}

TEST_CASE ("publishEventImpulse detects an edge that spans two separate drains exactly once",
           "[engine][telemetry][AnalysisThread][ripple]")
{
    EventImpulseRig rig;

    // First drain: signal goes high right at the very end of the block and
    // stays there.
    rig.push (blockWithPulseAt (1000, 999));
    rig.drain();
    const auto [firstSequence, firstAges] = rig.latestFrame();
    REQUIRE (firstAges.size() == 1); // the real edge, this drain

    // Second drain: still high throughout (no new edge) — eventWasHighBySlot
    // must carry the "already high" state across this call, or this would
    // wrongly re-detect a second edge at sample 0. publishEventImpulse()
    // leaves a "nothing new" drain's frame buffer untouched rather than
    // publishing an empty frame (AnalysisThread.cpp's own comment on why),
    // so the right proof here is "no NEW frame was published" (the
    // sequence number didn't move) — not "the payload read back empty",
    // which it never will be once any frame has ever been published once.
    rig.push (std::vector<float> (1000, 1.0f));
    rig.drain();
    const auto [secondSequence, secondAges] = rig.latestFrame();
    CHECK (secondSequence == firstSequence);
    CHECK (secondAges == firstAges);
}

TEST_CASE ("publishEventImpulse publishes nothing when a drain has no new edge at all",
           "[engine][telemetry][AnalysisThread][ripple]")
{
    EventImpulseRig rig;

    rig.push (std::vector<float> (1000, 0.0f)); // flat silence, never crosses 0.5
    rig.drain();

    CHECK (rig.ages().empty());
}

// ---- design/Visualization/Scope1.png: AnalysisThread::publishRollingHistory ----
// Same synchronous, deterministic shape as EventImpulseRig above.

namespace
{
    struct RollingHistoryRig
    {
        TelemetryHub hub;
        AnalysisThread analysis { hub };
        Tap* tap = nullptr;
        static constexpr size_t slot = 0;
        static constexpr double sampleRate = 44100.0;
        static constexpr const char* tapName = "h";

        explicit RollingHistoryRig (float windowSeconds)
        {
            hub.prepare (8192, maxTelemetryFrameBytes);
            analysis.prepare (sampleRate);
            tap = hub.subscribeTap (tapName);
            setWindow (windowSeconds);
        }

        void setWindow (float windowSeconds)
        {
            TapSettings settings;
            settings.historyWindowSeconds = windowSeconds;
            hub.setTapSettings (tapName, settings);
        }

        void push (const std::vector<float>& samples) { tap->push (samples.data(), (int) samples.size()); }
        void drain() { analysis.processSlotForTesting (slot, 0.01); }

        // (sequenceNumber, (lo, hi) pairs oldest-first) — the sequence
        // number is what lets a test tell "a fresh publish just happened"
        // apart from "readLatest() is handing back the same frame as
        // before" (same reasoning EventImpulseRig::latestFrame() already
        // gives in full).
        std::pair<uint64_t, std::vector<float>> latestFrame() const
        {
            std::vector<std::byte> bytes (maxTelemetryFrameBytes);
            const auto size = hub.getFrameBufferBySlot (slot, TelemetryFrameType::RollingHistory)->readLatest (bytes.data(), bytes.size());
            if (size == 0)
                return { 0, {} };

            TelemetryFrameHeader header;
            const float* payload = nullptr;
            REQUIRE (parseTelemetryFrame (bytes.data(), size, header, payload));
            CHECK (header.frameType == TelemetryFrameType::RollingHistory);
            return { header.sequenceNumber, std::vector<float> (payload, payload + header.payloadNumFloats) };
        }
    };
}

TEST_CASE ("publishRollingHistory publishes (lo, hi) pairs, oldest first, all finite once the ring has wrapped",
           "[engine][telemetry][AnalysisThread][scope]")
{
    // The minimum window (clamped) keeps columnDuration tiny, so a few
    // thousand samples of a moving signal is enough to wrap the whole ring
    // several times over and leave no column at its initial NaN.
    RollingHistoryRig rig (0.0f); // clamped up to AnalysisThread::minHistoryWindowSeconds

    std::vector<float> block (4096);
    for (size_t i = 0; i < block.size(); ++i)
        block[i] = std::sin (0.05f * (float) i);
    rig.push (block);
    rig.drain();

    const auto [sequence, payload] = rig.latestFrame();
    CHECK (sequence > 0);
    REQUIRE (! payload.empty());
    REQUIRE (payload.size() % 2 == 0);

    for (size_t column = 0; column < payload.size() / 2; ++column)
    {
        const auto lo = payload[column * 2];
        const auto hi = payload[column * 2 + 1];
        CHECK (std::isfinite (lo));
        CHECK (std::isfinite (hi));
        CHECK (lo <= hi);
    }
}

TEST_CASE ("publishRollingHistory's columns read exactly (value, value) for a perfectly constant input",
           "[engine][telemetry][AnalysisThread][scope]")
{
    RollingHistoryRig rig (0.0f);

    rig.push (std::vector<float> (4096, 0.5f));
    rig.drain();

    const auto [sequence, payload] = rig.latestFrame();
    CHECK (sequence > 0);
    REQUIRE (! payload.empty());

    for (size_t column = 0; column < payload.size() / 2; ++column)
    {
        // A still-NaN column (the ring hadn't fully wrapped yet) is skipped
        // rather than failed — this test is about every REAL column being
        // exactly flat, not about forcing a full wrap the way the test
        // above already covers.
        if (std::isnan (payload[column * 2]))
            continue;
        CHECK (payload[column * 2] == 0.5f);
        CHECK (payload[column * 2 + 1] == 0.5f);
    }
}

TEST_CASE ("publishRollingHistory's newest (rightmost) column reflects the most recently pushed value",
           "[engine][telemetry][AnalysisThread][scope]")
{
    RollingHistoryRig rig (0.0f);

    rig.push (std::vector<float> (4096, 0.3f));
    rig.drain();
    rig.push (std::vector<float> (16, 0.9f)); // a short, recent burst — not enough to fill the whole ring on its own
    rig.drain();

    const auto payload = rig.latestFrame().second;
    REQUIRE (! payload.empty());
    const auto lastHi = payload[payload.size() - 1];
    CHECK (lastHi == 0.9f);
}

TEST_CASE ("changing the time window resets the ring to NaN immediately, even with no new samples pushed",
           "[engine][telemetry][AnalysisThread][scope]")
{
    RollingHistoryRig rig (0.05f);

    rig.push (std::vector<float> (4096, 0.5f));
    rig.drain();
    const auto [firstSequence, firstPayload] = rig.latestFrame();
    REQUIRE (firstSequence > 0);
    CHECK (! std::isnan (firstPayload.back())); // the newest column is real, at least

    rig.setWindow (0.2f); // a genuinely different window, no new push before the next drain
    rig.drain();

    const auto [secondSequence, secondPayload] = rig.latestFrame();
    CHECK (secondSequence > firstSequence); // a real new publish happened, not a stale re-read
    for (const auto value : secondPayload)
        CHECK (std::isnan (value)); // every column cleared by the reset

    // And real data flows again once something new is actually pushed.
    rig.push (std::vector<float> (4096, 0.7f));
    rig.drain();
    const auto thirdPayload = rig.latestFrame().second;
    CHECK (! std::isnan (thirdPayload.back()));
}

TEST_CASE ("publishRollingHistory publishes nothing new on a drain with no new samples since the last one",
           "[engine][telemetry][AnalysisThread][scope]")
{
    // Regression test: Tap::readLatest() returns whatever is CURRENTLY in
    // the ring, not a delta since the last read, so re-scanning the same
    // already-folded samples on a drain with nothing genuinely new would
    // double-count real time (sealing columns too fast) if the
    // totalPushed-based "only scan the new tail" guard (same mechanism
    // publishEventImpulse already uses) were ever lost in a refactor.
    RollingHistoryRig rig (0.05f);

    // Enough to fully wrap the ring (no column left at its initial NaN) —
    // deliberate: NaN != NaN in IEEE 754, so a vector still holding any
    // NaN columns could never compare equal to itself below even on a
    // correct implementation, which would make that assertion meaningless
    // rather than failing for the right reason.
    rig.push (std::vector<float> (4096, 0.4f));
    rig.drain();
    const auto [firstSequence, firstPayload] = rig.latestFrame();
    REQUIRE (firstSequence > 0);
    REQUIRE (! std::isnan (firstPayload.front()));

    rig.drain(); // nothing new pushed in between
    const auto [secondSequence, secondPayload] = rig.latestFrame();
    CHECK (secondSequence == firstSequence); // no fresh publish happened
    CHECK (secondPayload == firstPayload);
}
