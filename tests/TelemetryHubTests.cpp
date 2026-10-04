#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/telemetry/TelemetryHub.h"

using namespace bazalt::engine;

TEST_CASE ("TelemetryHub::subscribeTap returns a stable, non-null Tap for a new name",
           "[engine][telemetry][TelemetryHub][NODE_EDITOR]")
{
    TelemetryHub hub;
    hub.prepare (64, 4096);

    auto* tap = hub.subscribeTap ("node:osc:preview");
    REQUIRE (tap != nullptr);
    CHECK (hub.getNumActiveTaps() == 1);
}

TEST_CASE ("Re-subscribing an already-subscribed name returns the same Tap and doesn't grow the active count",
           "[engine][telemetry][TelemetryHub][NODE_EDITOR]")
{
    TelemetryHub hub;
    hub.prepare (64, 4096);

    auto* first = hub.subscribeTap ("a");
    auto* second = hub.subscribeTap ("a");

    CHECK (first == second);
    CHECK (hub.getNumActiveTaps() == 1);
}

TEST_CASE ("unsubscribeTap frees the slot and it no longer resolves by name", "[engine][telemetry][TelemetryHub][NODE_EDITOR]")
{
    TelemetryHub hub;
    hub.prepare (64, 4096);

    hub.subscribeTap ("a");
    CHECK (hub.getNumActiveTaps() == 1);

    hub.unsubscribeTap ("a");
    CHECK (hub.getNumActiveTaps() == 0);
    CHECK (hub.getFrameBuffer ("a", TelemetryFrameType::Meter) == nullptr);
}

TEST_CASE ("getFrameBuffer returns nullptr for a name that was never subscribed", "[engine][telemetry][TelemetryHub]")
{
    TelemetryHub hub;
    hub.prepare (64, 4096);

    CHECK (hub.getFrameBuffer ("never-subscribed", TelemetryFrameType::Oscilloscope) == nullptr);
}

TEST_CASE ("Filling the pool then subscribing one more evicts the least-recently-subscribed tap",
           "[engine][telemetry][TelemetryHub][NODE_EDITOR]")
{
    TelemetryHub hub;
    hub.prepare (64, 4096);

    // Fill every slot, "tap0" first (so it's the LRU victim once the pool is full).
    for (size_t i = 0; i < TelemetryHub::maxTaps; ++i)
        hub.subscribeTap ("tap" + juce::String ((int) i));

    REQUIRE (hub.getNumActiveTaps() == TelemetryHub::maxTaps);

    // Touch every tap except "tap0" to refresh their LRU timestamps ahead of it.
    for (size_t i = 1; i < TelemetryHub::maxTaps; ++i)
        hub.subscribeTap ("tap" + juce::String ((int) i));

    auto* evictedTap = hub.getFrameBuffer ("tap0", TelemetryFrameType::Meter);
    REQUIRE (evictedTap != nullptr); // still resolves — not evicted yet

    // One more distinct name forces an eviction; "tap0" is the only
    // candidate with the oldest (never-refreshed) timestamp.
    auto* newTap = hub.subscribeTap ("brand-new");
    REQUIRE (newTap != nullptr);

    CHECK (hub.getNumActiveTaps() == TelemetryHub::maxTaps); // pool stays at capacity, not grown
    CHECK (hub.getFrameBuffer ("tap0", TelemetryFrameType::Meter) == nullptr); // evicted
    CHECK (hub.getFrameBuffer ("brand-new", TelemetryFrameType::Meter) != nullptr);

    // Every other tap ("tap1".."tap63") must have survived the eviction.
    for (size_t i = 1; i < TelemetryHub::maxTaps; ++i)
        CHECK (hub.getFrameBuffer ("tap" + juce::String ((int) i), TelemetryFrameType::Meter) != nullptr);
}

TEST_CASE ("Re-calling prepare() preserves an already-active subscription instead of silently dropping it",
           "[engine][telemetry][TelemetryHub][NODE_EDITOR]")
{
    // Direct, reproducible feedback: "when i switched the output in
    // settings, the ripple stopped responding" — switching the Standalone
    // app's audio output device re-runs prepareToPlay(), which re-prepares
    // the real TelemetryHub exactly like this test's second prepare() call
    // does. A dynamically-subscribed per-node preview tap (any
    // NodePreview.tsx/RippleBody.tsx subscription) must still resolve by
    // name afterwards, with no re-subscribe round trip — this hub has no
    // way to tell its client anything happened.
    TelemetryHub hub;
    hub.prepare (64, 4096);

    auto* before = hub.subscribeTap ("node:ripple1:in");
    REQUIRE (before != nullptr);
    REQUIRE (hub.getNumActiveTaps() == 1);

    hub.prepare (64, 4096); // the exact call PluginProcessor::prepareToPlay() makes again

    CHECK (hub.getNumActiveTaps() == 1);
    auto* after = hub.subscribeTap ("node:ripple1:in"); // still resolves by name, not a fresh/evicted slot
    CHECK (after == before); // same stable Tap* — nothing had to re-subscribe to get it back
    CHECK (hub.getFrameBuffer ("node:ripple1:in", TelemetryFrameType::EventImpulse) != nullptr);

    // A never-subscribed slot is still reset to the same clean state as
    // before this fix — this isn't "stop resetting slots", just "don't
    // reset the ones something is actually using".
    CHECK (hub.getFrameBuffer ("never-subscribed", TelemetryFrameType::Meter) == nullptr);
}

TEST_CASE ("A reused (evicted-and-resubscribed) slot's Tap pointer is reset, not carrying over stale data",
           "[engine][telemetry][TelemetryHub][NODE_EDITOR]")
{
    TelemetryHub hub;
    hub.prepare (64, 4096);

    auto* tapA = hub.subscribeTap ("a");
    const float sample = 0.5f;
    tapA->push (&sample, 1);

    float readBack = 0.0f;
    REQUIRE (tapA->readLatest (&readBack, 1) == 1);
    CHECK (readBack == 0.5f);

    hub.unsubscribeTap ("a");
    auto* tapB = hub.subscribeTap ("b"); // may or may not reuse the same physical slot

    float afterReuse = -1.0f;
    const auto numRead = tapB->readLatest (&afterReuse, 1);
    CHECK (numRead == 0); // freshly (re)prepared — nothing pushed yet, not "a"'s old 0.5
}
