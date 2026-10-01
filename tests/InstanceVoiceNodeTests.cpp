#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/nodes/InstanceVoiceNode.h"
#include <algorithm>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    struct RandomPair
    {
        float random1;
        float random2;
    };

    /** A fresh node, prepared, with `seed` set, spawning one voice via a
        direct noteOn() poke (the same call real MIDI/an io.noteIn
        connection ultimately drives — see InstanceVoiceNode.h's own class
        comment) — returns whatever random1/random2 came out of that one
        spawn.
    */
    RandomPair spawnOnceWithSeed (float seed)
    {
        InstanceVoiceNode node;
        node.prepare ({ 44100.0, 64 });
        node.setParameter ("instance.allocate.voice.seed", seed);
        node.reset();
        node.noteOn (60.0f, 1.0f);

        float out[9] {};
        node.processSample (nullptr, out);
        return { out[5], out[6] }; // random1, random2
    }
}

TEST_CASE ("09-28-InstanceAllocator.2: random1/random2 are bit-identical across two separate "
           "prepare()s, same seed - real patch-level determinism, not time-seeded",
           "[engine][nodes][InstanceVoiceNode]")
{
    // Regression test for the real bug: InstanceVoiceNode::prepare() used to
    // call random.setSeedRandomly() - wall-clock-seeded, so two separate
    // construct->prepare->noteOn sequences (exactly what two separate
    // render-cli runs, or two plugin loads of the same patch, do) produced
    // DIFFERENT random1/random2 every time, contradicting the one stated
    // reason these ports are allocator-owned state at all (DOMAINS.md §4:
    // "the same patch, the same MIDI, the same seed produce bit-identical
    // output").
    const auto first = spawnOnceWithSeed (42.0f);
    const auto second = spawnOnceWithSeed (42.0f);

    CHECK (first.random1 == second.random1);
    CHECK (first.random2 == second.random2);
}

TEST_CASE ("09-28-InstanceAllocator.2: a different seed produces different random1/random2",
           "[engine][nodes][InstanceVoiceNode]")
{
    // The flip side of the above - confirms the seed is actually READ, not
    // just that the output happens to be some other constant regardless of
    // input (a mutation that hardcoded a fixed return value would pass the
    // test above and fail this one).
    const auto a = spawnOnceWithSeed (1.0f);
    const auto b = spawnOnceWithSeed (2.0f);

    CHECK ((a.random1 != b.random1 || a.random2 != b.random2));
}

TEST_CASE ("09-28-InstanceAllocator.2: random1/random2 are deterministic per spawn ordinal, "
           "not just per node instance - two successive spawns differ from each other",
           "[engine][nodes][InstanceVoiceNode]")
{
    // Same seed, same node, but the SECOND spawn (instanceIndex incremented)
    // must not reuse the first spawn's random values - each spawn ordinal
    // gets its own point in the (seed, instanceIndex) space, per the design
    // ("a pure function of (patch seed, spawn ordinal)").
    InstanceVoiceNode node;
    node.prepare ({ 44100.0, 64 });
    node.setParameter ("instance.allocate.voice.seed", 7.0f);
    node.reset();

    node.noteOn (60.0f, 1.0f);
    float firstSpawn[9] {};
    node.processSample (nullptr, firstSpawn);

    node.noteOff();
    node.noteOn (62.0f, 1.0f);
    float secondSpawn[9] {};
    node.processSample (nullptr, secondSpawn);

    CHECK ((firstSpawn[5] != secondSpawn[5] || firstSpawn[6] != secondSpawn[6]));

    // And repeating the exact same two-spawn sequence from a clean node,
    // same seed, must reproduce BOTH spawns' values exactly - not just the
    // first one.
    InstanceVoiceNode replay;
    replay.prepare ({ 44100.0, 64 });
    replay.setParameter ("instance.allocate.voice.seed", 7.0f);
    replay.reset();

    replay.noteOn (60.0f, 1.0f);
    float replayFirst[9] {};
    replay.processSample (nullptr, replayFirst);
    replay.noteOff();
    replay.noteOn (62.0f, 1.0f);
    float replaySecond[9] {};
    replay.processSample (nullptr, replaySecond);

    CHECK (replayFirst[5] == firstSpawn[5]);
    CHECK (replayFirst[6] == firstSpawn[6]);
    CHECK (replaySecond[5] == secondSpawn[5]);
    CHECK (replaySecond[6] == secondSpawn[6]);
}

TEST_CASE ("InstanceVoiceNode declares a real, structural seed parameter defaulting to 1",
           "[engine][nodes][InstanceVoiceNode]")
{
    InstanceVoiceNode node;
    const auto parameters = node.getParameters();

    const auto it = std::find_if (parameters.begin(), parameters.end(),
                                   [] (const ParameterDescriptor& p) { return p.id == "instance.allocate.voice.seed"; });
    REQUIRE (it != parameters.end());
    CHECK (it->isStructural);
    CHECK (it->isInteger);
    CHECK (it->defaultValue == 1.0f);
}
