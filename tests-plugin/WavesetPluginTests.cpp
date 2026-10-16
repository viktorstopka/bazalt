// wiki/plans/Wavesets.md W1: the Interleave patch (two Wavesets into a Waveset
// Play) compiles through the real controller — a growable group of Data inputs —
// and keeps playing across an edit.
#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"

using namespace bazalt;

TEST_CASE ("The Interleave patch compiles, plays both sources, and survives an edit", "[plugin][wavesets][3a]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    auto& c = processor.getGraphEditController();

    bazalt::engine::NodeGraph g;
    bazalt::engine::NodeInstance sine { "sine", "source.sine", {}, {}, {} };
    sine.parameters["source.sine.frequency"] = 200.0f;
    g.addNode (sine);
    bazalt::engine::NodeInstance saw { "saw", "source.saw", {}, {}, {} };
    saw.parameters["source.saw.frequency"] = 320.0f;
    g.addNode (saw);
    g.addNode ({ "a", "waveset.capture", {}, {}, {} });
    g.addNode ({ "b", "waveset.capture", {}, {}, {} });
    g.addNode ({ "play", "waveset.play", {}, {}, {} });
    g.addNode ({ "masterOut", "io.output", {}, {}, {} });
    g.addConnection ({ "sine", "out", "a", "in" });
    g.addConnection ({ "saw", "out", "b", "in" });
    g.addConnection ({ "a", "wavesets", "play", "in.0" });
    g.addConnection ({ "b", "wavesets", "play", "in.1" });
    g.addConnection ({ "play", "out", "masterOut", "in" });
    g.setOutput ("masterOut", "out");
    const auto loaded = c.setGraph (g);
    INFO (loaded.errorMessage);
    REQUIRE (loaded.success);

    juce::AudioBuffer<float> buffer (2, 512);
    auto render = [&] (int blocks)
    {
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            juce::MidiBuffer midi;
            processor.processBlock (buffer, midi);
            peak = std::max (peak, buffer.getMagnitude (0, 0, 512));
        }
        return peak;
    };
    CHECK (render (20) > 0.1f);

    // A third source: the group grows, the patch recompiles, and it still plays.
    REQUIRE (c.addNode ("waveset.capture", "c", 0, 0).success);
    REQUIRE (c.connect ("sine", "out", "c", "in").success);
    const auto third = c.connect ("c", "wavesets", "play", "in.2");
    INFO (third.errorMessage);
    REQUIRE (third.success);
    CHECK (render (20) > 0.1f);
}
