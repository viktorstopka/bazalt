// wiki/plans/StereoChannels.md: a stereo per-voice chain reaches the speakers
// as stereo — through the voice sum, with or without an instance.sum and
// global effects after it. Before this, every voice was summed into one mono
// buffer and the right channel of a per-voice pan was silently dropped.
#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "bazalt/engine/graph/ProofGraphs.h"

using bazalt::BazaltAudioProcessor;

namespace
{
    // The voice proof graph with a hard-right pan after the amp; optionally
    // an instance.sum + filter + master output after it (a global domain).
    bazalt::engine::NodeGraph pannedVoices (bool withGlobalDomain)
    {
        auto graph = bazalt::engine::buildVoiceProofGraph();
        graph.addNode ({ "pan", "space.pan", {}, { { "space.pan.pan", 1.0f } }, {} });
        graph.addConnection ({ "amp", "out", "pan", "in" });

        if (! withGlobalDomain)
        {
            graph.setOutput ("pan", "out");
            return graph;
        }

        graph.addNode ({ "sum", "instance.sum", {}, {}, {} });
        graph.addNode ({ "post", "filter.svf", {}, { { "filter.svf.cutoff", 8000.0f } }, {} });
        graph.addNode ({ "master", "io.output", {}, {}, {} });
        graph.addConnection ({ "pan", "out", "sum", "in" });
        graph.addConnection ({ "sum", "out", "post", "in" });
        graph.addConnection ({ "post", "out", "master", "in" });
        graph.setOutput ("master", "out");
        return graph;
    }

    std::pair<float, float> playNoteAndMeasure (BazaltAudioProcessor& processor)
    {
        juce::MidiBuffer noteOn;
        noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        processor.processBlock (buffer, noteOn);

        float left = 0.0f, right = 0.0f;
        juce::MidiBuffer none;
        for (int block = 0; block < 20; ++block)
        {
            buffer.clear();
            processor.processBlock (buffer, none);
            left = std::max (left, buffer.getMagnitude (0, 0, buffer.getNumSamples()));
            right = std::max (right, buffer.getMagnitude (1, 0, buffer.getNumSamples()));
        }
        return { left, right };
    }
}

TEST_CASE ("A per-voice hard-right pan is heard on the right only", "[plugin][stereo]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (pannedVoices (false)).success);

    const auto [left, right] = playNoteAndMeasure (processor);
    CHECK (right > 0.01f);
    CHECK (left < right * 0.01f);
}

TEST_CASE ("Stereo voices stay stereo through instance.sum and a global per-channel filter", "[plugin][stereo]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (pannedVoices (true)).success);

    const auto [left, right] = playNoteAndMeasure (processor);
    CHECK (right > 0.01f);
    CHECK (left < right * 0.01f);
}
