// wiki/plans/InstanceAxis.md §3: "two engines side by side". The same patch and
// the same MIDI go through the per-voice engine and the instance axis; the
// outputs must match sample for sample before the instance axis replaces it.
// Patches here keep their once-only part free of running state that feeds the
// voices (an LFO into every voice), the one place the instance axis changes
// the sound on purpose (one shared LFO instead of a copy per voice).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "PluginProcessor.h"
#include "bazalt/engine/graph/CurveData.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/patch/PatchSerializer.h"
#include <cmath>
#include <vector>

using namespace bazalt;
using namespace bazalt::engine;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int block = 256;

    struct Event
    {
        int block, offset;
        juce::MidiMessage message;
    };

    std::vector<float> render (const NodeGraph& graph, bool instanceAxis, const std::vector<Event>& events, int blocks)
    {
        BazaltAudioProcessor processor;
        processor.setOutputLimiterEnabledForTesting (false);
        processor.prepareToPlay (sampleRate, block);
        processor.getGraphEditController().setUseInstanceAxis (instanceAxis);
        const auto result = processor.getGraphEditController().setGraph (graph);
        INFO (result.errorMessage);
        REQUIRE (result.success);
        REQUIRE (processor.isInstanceAxisMode() == (instanceAxis && ! processor.getGraphEditController().getOriginBundleIndices().empty()));
        if (processor.isInstanceAxisMode())
        {
            auto* plan = processor.getGlobalPlanSwapper().peekCurrentPlan();
            REQUIRE (plan != nullptr);
            REQUIRE (! plan->instanceGroups.empty()); // really the instanced plan
        }

        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, block);
        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            juce::MidiBuffer midi;
            for (const auto& e : events)
                if (e.block == b)
                    midi.addEvent (e.message, e.offset);
            processor.processBlock (buffer, midi);
            for (int i = 0; i < block; ++i)
            {
                out.push_back (buffer.getSample (0, i));
                out.push_back (buffer.getSample (1, i));
            }
        }
        return out;
    }

    void requireSame (const NodeGraph& graph, const std::vector<Event>& events, int blocks, bool mustPlay = true)
    {
        const auto perVoice = render (graph, false, events, blocks);
        const auto instanced = render (graph, true, events, blocks);
        REQUIRE (perVoice.size() == instanced.size());
        size_t mismatches = 0, first = perVoice.size();
        float loudest = 0.0f;
        for (size_t i = 0; i < perVoice.size(); ++i)
        {
            if (perVoice[i] != instanced[i])
            {
                ++mismatches;
                first = std::min (first, i);
            }
            loudest = std::max (loudest, std::abs (perVoice[i]));
        }
        INFO ("first mismatch at sample " << first / 2 << (first % 2 ? " (right)" : " (left)") << " of " << perVoice.size() / 2
                                           << ": per-voice " << (first < perVoice.size() ? perVoice[first] : 0.0f)
                                           << " vs instanced " << (first < instanced.size() ? instanced[first] : 0.0f));
        CHECK (mismatches == 0);
        if (mustPlay)
            CHECK (loudest > 0.01f); // it really played
    }

    juce::MidiMessage on (int note, float velocity = 0.8f) { return juce::MidiMessage::noteOn (1, note, velocity); }
    juce::MidiMessage off (int note) { return juce::MidiMessage::noteOff (1, note); }

    /** noteIn → Voice → saw × envelope → SVF → [pan] → Merge → (delay) → out. */
    NodeGraph subtractive (bool stereo, bool mergeInFront = true)
    {
        NodeGraph graph;
        graph.addNode ({ "noteIn", "io.noteIn", {}, {}, {} });
        graph.addNode ({ "voice", "life.voice", {}, {}, {} });
        graph.addNode ({ "p2f", "math.pitchToFrequency", {}, {}, {} });
        graph.addNode ({ "saw", "source.saw", {}, {}, {} });
        NodeInstance env { "env", "source.envelope", {}, {}, {} };
        env.content = CurveDocument::adsr (0.01f, 0.1f, 0.6f, 0.05f).toVar();
        graph.addNode (env);
        graph.addNode ({ "vca", "math.multiply", {}, {}, {} });
        graph.addNode ({ "svf", "filter.svf", {}, { { "filter.svf.cutoff", 1800.0f }, { "filter.svf.resonance", 2.0f } }, {} });
        graph.addConnection ({ "noteIn", "notes", "voice", "spawn" });
        graph.addConnection ({ "voice", "pitch", "p2f", "pitch" });
        graph.addConnection ({ "p2f", "frequency", "saw", "source.saw.frequency" });
        graph.addConnection ({ "voice", "gate", "env", "gate" });
        graph.addConnection ({ "saw", "out", "vca", "in.0" });
        graph.addConnection ({ "env", "out", "vca", "in.1" });
        graph.addConnection ({ "vca", "out", "svf", "in" });
        juce::String last = "svf";
        if (stereo)
        {
            graph.addNode ({ "pan", "space.pan", {}, {}, {} });
            graph.addConnection ({ "svf", "out", "pan", "in" });
            graph.addConnection ({ "voice", "velocity", "pan", "space.pan.pan" });
            last = "pan";
        }
        if (! mergeInFront)
        {
            graph.setOutput (last, "out");
            return graph;
        }
        graph.addNode ({ "merge", "life.merge", {}, {}, {} });
        graph.addNode ({ "delay", "time.delay", {}, { { "time.delay.samples", 300.0f } }, {} });
        graph.addNode ({ "out", "io.output", {}, {}, {} });
        graph.addConnection ({ last, "out", "merge", "in" });
        graph.addConnection ({ "merge", "out", "delay", "in" });
        graph.addConnection ({ "delay", "out", "out", "in" });
        graph.setOutput ("out", "out");
        return graph;
    }

    /** Chords, releases, more notes than voices (steals), mid-block events. */
    std::vector<Event> busyMidi()
    {
        std::vector<Event> events {
            { 0, 0, on (60) }, { 0, 37, on (64, 0.5f) }, { 0, 100, on (67, 0.9f) },
            { 6, 50, off (64) }, { 8, 3, on (72) },
        };
        for (int n = 0; n < 10; ++n) // ten more: the pool of eight steals
            events.push_back ({ 10 + n, 17 * n % block, on (48 + n * 2, 0.3f + 0.05f * n) });
        for (int n = 0; n < 10; ++n)
            events.push_back ({ 24 + n, 31 * n % block, off (48 + n * 2) });
        events.push_back ({ 26, 128, off (60) });
        events.push_back ({ 28, 5, off (67) });
        events.push_back ({ 30, 200, off (72) });
        return events;
    }
}

TEST_CASE ("Instance axis = per-voice engine: a polyphonic subtractive patch with steals", "[plugin][instanceAxis][parity]")
{
    requireSame (subtractive (false), busyMidi(), 60);
}

TEST_CASE ("Instance axis = per-voice engine: stereo voices", "[plugin][instanceAxis][parity]")
{
    requireSame (subtractive (true), busyMidi(), 60);
}

TEST_CASE ("Instance axis = per-voice engine: the output itself per voice (no Merge)", "[plugin][instanceAxis][parity]")
{
    requireSame (subtractive (false, false), busyMidi(), 60);
}

TEST_CASE ("Instance axis = per-voice engine: the Init Patch", "[plugin][instanceAxis][parity]")
{
    requireSame (buildInitPatchGraph(), busyMidi(), 60);
}

TEST_CASE ("Instance axis = per-voice engine: a swarm population", "[plugin][instanceAxis][parity]")
{
    NodeGraph graph;
    graph.addNode ({ "swarm", "life.swarmPopulation", {}, { { "life.swarmPopulation.populationSize", 5.0f } }, {} });
    graph.addNode ({ "map", "math.map", {}, { { "math.map.outMin", 200.0f }, { "math.map.outMax", 800.0f } }, {} });
    graph.addNode ({ "osc", "source.sine", {}, {}, {} });
    graph.addNode ({ "merge", "life.merge", {}, {}, {} });
    graph.addNode ({ "out", "io.output", {}, {}, {} });
    graph.addConnection ({ "swarm", "random1", "map", "in" });
    graph.addConnection ({ "map", "out", "osc", "source.sine.frequency" });
    graph.addConnection ({ "swarm", "gate", "osc", "source.sine.amplitude" });
    graph.addConnection ({ "osc", "out", "merge", "in" });
    graph.addConnection ({ "merge", "out", "out", "in" });
    graph.setOutput ("out", "out");
    requireSame (graph, {}, 20);
}

TEST_CASE ("Instance axis = per-voice engine: an internally triggered swarm", "[plugin][instanceAxis][parity]")
{
    NodeGraph graph;
    graph.addNode ({ "clock", "time.clock", {}, {}, {} });
    graph.addNode ({ "swarm", "life.swarmTransient", {}, { { "life.swarmTransient.duration", 0.05f } }, {} });
    graph.addNode ({ "osc", "source.sine", {}, { { "source.sine.frequency", 330.0f } }, {} });
    graph.addNode ({ "merge", "life.merge", {}, {}, {} });
    graph.addNode ({ "out", "io.output", {}, {}, {} });
    graph.addConnection ({ "clock", "tick", "swarm", "spawn" });
    graph.addConnection ({ "swarm", "gate", "osc", "source.sine.amplitude" });
    graph.addConnection ({ "osc", "out", "merge", "in" });
    graph.addConnection ({ "merge", "out", "out", "in" });
    graph.setOutput ("out", "out");
    requireSame (graph, {}, 120);
}

TEST_CASE ("Factory patches through both engines", "[plugin][instanceAxis][parity][factory]")
{
    const auto files = juce::File (BAZALT_FACTORY_PATCH_DIR).findChildFiles (juce::File::findFiles, false, "*.bazalt");
    REQUIRE (! files.isEmpty());
    for (const auto& file : files)
    {
        INFO (file.getFileName());
        const auto parsed = parsePatchFromJson (file.loadFileAsString());
        REQUIRE (parsed.success);
        requireSame (parsed.document.toNodeGraph(), busyMidi(), 60, false); // some answer other input than these notes
    }
}
