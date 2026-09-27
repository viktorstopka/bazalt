#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/IoAudioInNode.h"
#include "bazalt/engine/nodes/IoControlNode.h"
#include "bazalt/engine/nodes/IoTransportNode.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    // One block through a node's processBlock with `numOutputs` output buffers.
    std::vector<std::vector<float>> runBlock (Node& node, int numOutputs, int numSamples)
    {
        std::vector<std::vector<float>> outputs ((size_t) numOutputs, std::vector<float> ((size_t) numSamples, -99.0f));
        std::vector<float*> pointers;
        for (auto& buffer : outputs)
            pointers.push_back (buffer.data());

        node.processBlock (nullptr, pointers.data(), numSamples);
        return outputs;
    }

    std::vector<float> ramp (int n, float start = 1.0f)
    {
        std::vector<float> values ((size_t) n);
        std::iota (values.begin(), values.end(), start);
        return values;
    }
}

// ---- io.audioIn -----------------------------------------------------------

TEST_CASE ("io.audioIn passes the selected host bus through, and reads silence when it isn't active",
           "[engine][nodes][io][host-input][M21]")
{
    const auto main = ramp (8, 1.0f);
    const auto aux1 = ramp (8, 100.0f);

    HostInputs inputs;
    inputs.audio[0] = { main.data(), main.data() };
    inputs.audio[1] = { aux1.data(), nullptr }; // aux 1: left present, right absent

    IoAudioInNode node;
    REQUIRE (node.wantsHostInputs());

    node.setHostInputs (inputs);
    auto out = runBlock (node, 2, 8);
    CHECK (out[0] == main);
    CHECK (out[1] == main);

    node.setParameter ("io.audioIn.bus", 1.0f); // aux 1
    node.setHostInputs (inputs);
    out = runBlock (node, 2, 8);
    CHECK (out[0] == aux1);
    CHECK (out[1] == std::vector<float> (8, 0.0f)); // the missing channel is silence, not garbage

    node.setParameter ("io.audioIn.bus", 4.0f); // aux 4, never provided by this host
    node.setHostInputs (inputs);
    out = runBlock (node, 2, 8);
    CHECK (out[0] == std::vector<float> (8, 0.0f));
}

TEST_CASE ("io.audioIn drops the host pointers after one block, so a process() with no fresh host data reads silence",
           "[engine][nodes][io][host-input][M21]")
{
    // The host buffer is only valid for the process() call it was handed to;
    // holding the pointer across calls would read freed memory.
    const auto main = ramp (8);
    HostInputs inputs;
    inputs.audio[0] = { main.data(), main.data() };

    IoAudioInNode node;
    node.setHostInputs (inputs);
    CHECK (runBlock (node, 2, 8)[0] == main);
    CHECK (runBlock (node, 2, 8)[0] == std::vector<float> (8, 0.0f)); // no setHostInputs in between
}

TEST_CASE ("io.audioIn clamps an out-of-range bus setting instead of indexing past the host's buses",
           "[engine][nodes][io][host-input][M21]")
{
    const auto main = ramp (4);
    HostInputs inputs;
    inputs.audio[0] = { main.data(), main.data() };

    IoAudioInNode node;
    node.setParameter ("io.audioIn.bus", -3.0f);
    node.setHostInputs (inputs);
    CHECK (runBlock (node, 2, 4)[0] == main); // clamped to the main bus

    node.setParameter ("io.audioIn.bus", 99.0f);
    node.setHostInputs (inputs); // clamped to aux 4, which is empty: silence, and above all no crash
    CHECK (runBlock (node, 2, 4)[0] == std::vector<float> (4, 0.0f));
}

TEST_CASE ("io.audioIn through the real compiler: the plan records it, and host samples reach the output",
           "[engine][nodes][io][host-input][M21][GraphCompiler]")
{
    const auto factory = buildDefaultNodeFactory();

    NodeGraph graph;
    graph.addNode ({ "in", "io.audioIn", {}, {}, {} });
    graph.addNode ({ "out", "io.output", {}, {}, {} });
    graph.addConnection ({ "in", "channel.0", "out", "in" });
    graph.setOutput ("out", "out");

    auto result = GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);
    REQUIRE (result.plan.hostInputNodes.size() == 1);

    const auto samples = ramp (8, 5.0f);
    HostInputs inputs;
    inputs.audio[0] = { samples.data(), samples.data() };

    auto outputOf = [&]
    {
        result.plan.process (8);
        const auto* data = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
        return std::vector<float> (data, data + 8);
    };

    result.plan.applyHostInputs (inputs);
    CHECK (outputOf() == samples);
    CHECK (outputOf() == std::vector<float> (8, 0.0f)); // nothing applied this time
}

TEST_CASE ("Only the host-boundary nodes ask for host inputs", "[engine][nodes][host-input][M21]")
{
    const auto factory = buildDefaultNodeFactory();

    for (const auto* typeId : { "io.audioIn", "io.control", "io.transport" })
        CHECK (factory.create (typeId)->wantsHostInputs());

    for (const auto* typeId : { "osc.analog", "io.noteIn", "io.output", "math.add", "mix.sum" })
        CHECK_FALSE (factory.create (typeId)->wantsHostInputs());
}

// ---- io.control -----------------------------------------------------------

namespace
{
    float controlValue (IoControlNode& node, const HostInputs& inputs, int samples = 1)
    {
        node.setHostInputs (inputs);
        float out = -1.0f;
        for (int i = 0; i < samples; ++i)
            node.processSample (nullptr, &out);
        return out;
    }
}

TEST_CASE ("io.control follows the source it is set to, scaled onto 0..1", "[engine][nodes][io][host-input][M21]")
{
    HostInputs inputs;
    inputs.controllers[1] = 0.25f;   // mod wheel
    inputs.controllers[64] = 1.0f;   // sustain pedal down
    inputs.controllers[74] = 0.75f;  // an arbitrary CC
    inputs.channelPressure = 0.5f;
    inputs.pitchBend = -1.0f;

    auto valueFor = [&] (float source, float cc = 74.0f, float pitchBend = -1.0f)
    {
        IoControlNode node;
        node.setParameter ("io.control.smoothing", 0.0f); // exact, so the test reads the mapping itself
        node.setParameter ("io.control.source", source);
        node.setParameter ("io.control.cc", cc);
        auto in = inputs;
        in.pitchBend = pitchBend;
        return controlValue (node, in);
    };

    CHECK (valueFor (0.0f, 74.0f) == 0.75f);  // CC by number
    CHECK (valueFor (0.0f, 1.0f) == 0.25f);   // ... a different number
    CHECK (valueFor (1.0f) == 0.25f);         // mod wheel is CC 1
    CHECK (valueFor (2.0f) == 0.5f);          // channel pressure
    CHECK (valueFor (4.0f) == 1.0f);          // sustain is CC 64

    // Pitch bend is -1..1 mapped onto the Unipolar output: centre is 0.5.
    CHECK (valueFor (3.0f, 74.0f, -1.0f) == 0.0f);
    CHECK (valueFor (3.0f, 74.0f, 0.0f) == 0.5f);
    CHECK (valueFor (3.0f, 74.0f, 1.0f) == 1.0f);
}

TEST_CASE ("io.control smooths CC steps with a time constant that is the same in real time at any sample rate",
           "[engine][nodes][io][host-input][M21]")
{
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        DYNAMIC_SECTION ("sample rate " << rate)
        {
            IoControlNode node;
            NodePrepareInfo info;
            info.sampleRate = rate;
            node.prepare (info);
            node.setParameter ("io.control.source", 0.0f);
            node.setParameter ("io.control.cc", 20.0f);
            node.setParameter ("io.control.smoothing", 10.0f); // ms

            HostInputs inputs;
            inputs.controllers[20] = 0.0f;
            REQUIRE (controlValue (node, inputs) == 0.0f); // first value adopted

            inputs.controllers[20] = 1.0f; // a CC jump to full
            const auto oneTimeConstant = (int) std::lround (0.010 * rate);
            const auto value = controlValue (node, inputs, oneTimeConstant);
            CHECK (value == Catch::Approx (1.0f - std::exp (-1.0f)).margin (0.01));
        }
    }
}

TEST_CASE ("io.control adopts its first value instead of sweeping up from 0, and clamps to 0..1",
           "[engine][nodes][io][host-input][M21]")
{
    IoControlNode node;
    node.setParameter ("io.control.source", 0.0f);
    node.setParameter ("io.control.cc", 7.0f);
    node.setParameter ("io.control.smoothing", 500.0f); // very slow: any ramp from 0 would be obvious

    HostInputs inputs;
    inputs.controllers[7] = 0.8f;
    CHECK (controlValue (node, inputs) == 0.8f);

    inputs.controllers[7] = 3.0f; // out of range must not escape the declared Unipolar range
    node.setParameter ("io.control.smoothing", 0.0f);
    CHECK (controlValue (node, inputs) == 1.0f);
}

// ---- io.transport ---------------------------------------------------------

namespace
{
    struct TransportRun
    {
        std::vector<float> beat, tempo, playing, position;
    };

    TransportRun runTransport (IoTransportNode& node, const HostInputs& inputs, int numSamples)
    {
        node.setHostInputs (inputs);
        auto out = runBlock (node, 4, numSamples);
        return { out[0], out[1], out[2], out[3] };
    }

    std::vector<int> beatSamples (const std::vector<float>& beat, int offset = 0)
    {
        std::vector<int> samples;
        for (int i = 0; i < (int) beat.size(); ++i)
            if (beat[(size_t) i] != 0.0f)
                samples.push_back (offset + i);
        return samples;
    }
}

TEST_CASE ("io.transport reports tempo (as beats per second), the playing flag and a running position",
           "[engine][nodes][io][host-input][M21]")
{
    IoTransportNode node;
    HostInputs inputs;
    inputs.transportPlaying = true;
    inputs.tempoBpm = 90.0;
    inputs.sampleRate = 48000.0;
    inputs.timeSeconds = 2.0;
    inputs.ppqPosition = 3.0;

    const auto run = runTransport (node, inputs, 48000);
    CHECK (run.tempo.front() == Catch::Approx (1.5f));  // 90 BPM = 1.5 beats per second
    CHECK (run.playing.front() == 1.0f);
    CHECK (run.position.front() == Catch::Approx (2.0f));
    CHECK (run.position.back() == Catch::Approx (3.0f).margin (0.001)); // one second later

    inputs.transportPlaying = false;
    const auto stopped = runTransport (node, inputs, 64);
    CHECK (stopped.playing.front() == 0.0f);
    CHECK (stopped.position.front() == Catch::Approx (2.0f));
    CHECK (stopped.position.back() == Catch::Approx (2.0f)); // position holds while stopped
    CHECK (beatSamples (stopped.beat).empty());
}

TEST_CASE ("io.transport pulses on the exact sample each beat starts, at the right rate",
           "[engine][nodes][io][host-input][M21]")
{
    IoTransportNode node;
    HostInputs inputs;
    inputs.transportPlaying = true;
    inputs.tempoBpm = 120.0;       // 2 beats per second
    inputs.sampleRate = 48000.0;   // -> a beat every 24000 samples
    inputs.ppqPosition = 0.0;

    const auto beats = beatSamples (runTransport (node, inputs, 100000).beat);
    CHECK (beats == std::vector<int> { 0, 24000, 48000, 72000, 96000 });

    // Starting mid-beat: the next beat lands where the arithmetic says it must.
    inputs.ppqPosition = 0.5; // half a beat in -> next beat after 12000 samples
    const auto midBeat = beatSamples (runTransport (node, inputs, 30000).beat);
    CHECK (midBeat == std::vector<int> { 12000 });
}

TEST_CASE ("io.transport never drops or doubles a beat when the block is split into sub-ranges",
           "[engine][nodes][io][host-input][M21]")
{
    // The voice domain renders a block in pieces at MIDI event boundaries, so
    // a beat that falls on a boundary must fire exactly once.
    constexpr auto sampleRate = 48000.0;
    constexpr auto bpm = 133.0; // an awkward tempo, so beats fall between samples
    constexpr auto total = 200000;

    auto beatsForChunking = [&] (const std::vector<int>& chunkSizes)
    {
        IoTransportNode node;
        std::vector<int> beats;
        auto start = 0;
        auto chunkIndex = 0;
        while (start < total)
        {
            const auto size = std::min (chunkSizes[(size_t) (chunkIndex++ % (int) chunkSizes.size())], total - start);

            HostInputs inputs;
            inputs.transportPlaying = true;
            inputs.tempoBpm = bpm;
            inputs.sampleRate = sampleRate;
            inputs.ppqPosition = (double) start * bpm / 60.0 / sampleRate;
            inputs.timeSeconds = (double) start / sampleRate;

            for (const auto sample : beatSamples (runTransport (node, inputs, size).beat, start))
                beats.push_back (sample);

            start += size;
        }
        return beats;
    };

    const auto whole = beatsForChunking ({ total });
    REQUIRE (whole.size() > 8);

    CHECK (beatsForChunking ({ 512 }) == whole);
    CHECK (beatsForChunking ({ 1, 7, 64, 333, 4096 }) == whole);
    CHECK (beatsForChunking ({ 3 }) == whole); // tiny pieces, so nearly every beat sits on some boundary

    // No two adjacent beats are ever closer than a beat's worth of samples.
    for (size_t i = 1; i < whole.size(); ++i)
        CHECK (whole[i] - whole[i - 1] >= (int) (60.0 / bpm * sampleRate) - 1);
}

TEST_CASE ("io.transport is safe at a zero or negative tempo", "[engine][nodes][io][host-input][M21]")
{
    IoTransportNode node;
    HostInputs inputs;
    inputs.transportPlaying = true;
    inputs.sampleRate = 44100.0;

    for (const auto bpm : { 0.0, -120.0 })
    {
        inputs.tempoBpm = bpm;
        const auto run = runTransport (node, inputs, 256);
        CHECK (beatSamples (run.beat).empty());
        CHECK (run.tempo.front() == 0.0f);
        for (const auto value : run.position)
            CHECK (std::isfinite (value));
    }
}

// A displayed default has to be the node's real default. osc.analog's card shows
// "Frequency 440 Hz", but PolyBlepOscillator starts at 0 Hz, so with nothing wired
// to its pitch the oscillator was silent DC. Invisible while every oscillator sat
// in a voice graph (the allocator always drives pitch); found by wiring a scope to
// one in an allocator-free graph in the running app.
TEST_CASE ("An oscillator with nothing wired to its pitch sounds at its displayed 440 Hz",
           "[engine][osc][M21]")
{
    auto factory = buildDefaultNodeFactory();

    NodeGraph graph;
    graph.addNode ({ "osc", "osc.analog", {}, {}, {} });
    graph.setOutput ("osc", "out");

    constexpr double sampleRate = 44100.0;
    auto compiled = GraphCompiler::compile (graph, factory, { sampleRate, 4410 }, 1);
    REQUIRE (compiled.success);

    compiled.plan.process (4410); // 0.1 s
    const auto* out = compiled.plan.blockBuffers[(size_t) compiled.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);

    // A saw at 440 Hz: loud, and crossing zero about 2 * 44 times in 0.1 s.
    float peak = 0.0f;
    int crossings = 0;
    for (int i = 0; i < 4410; ++i)
    {
        peak = std::max (peak, std::abs (out[i]));
        if (i > 0 && (out[i - 1] < 0.0f) != (out[i] < 0.0f))
            ++crossings;
    }

    CHECK (peak > 0.5f);
    CHECK (crossings >= 80);
    CHECK (crossings <= 96);

    // A saved frequency still wins over that starting point.
    NodeGraph saved;
    saved.addNode ({ "osc", "osc.analog", {}, { { "osc.analog.frequency", 220.0f } }, {} });
    saved.setOutput ("osc", "out");
    auto savedCompiled = GraphCompiler::compile (saved, factory, { sampleRate, 4410 }, 1);
    REQUIRE (savedCompiled.success);
    savedCompiled.plan.process (4410);
    const auto* savedOut = savedCompiled.plan.blockBuffers[(size_t) savedCompiled.plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);

    int savedCrossings = 0;
    for (int i = 1; i < 4410; ++i)
        if ((savedOut[i - 1] < 0.0f) != (savedOut[i] < 0.0f))
            ++savedCrossings;
    CHECK (savedCrossings >= 38);
    CHECK (savedCrossings <= 50);
}
