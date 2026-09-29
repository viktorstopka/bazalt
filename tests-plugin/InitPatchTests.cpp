// M22 wave 6 originally made the Init Patch GraphEditController's
// constructor default. The 0.x arc replaced that default with a plain
// master-out-only graph (2026-09-29, on the user's own explicit
// instruction - GraphEditController.cpp's own comment has the reasoning),
// so this file now covers two separate things: the real, current default
// (the first test case), and the Init Patch's own playability, which still
// matters and is still exercised the same way (real MIDI through the real
// BazaltAudioProcessor, VoiceRenderTests.cpp's established approach) - just
// via an explicit setGraph() now, the same pattern buildVoiceProofGraph()
// already used even back when it was the demoted default.
#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/RtAllocationTrap.h"
#include <cmath>

using namespace bazalt;

namespace
{
    float rms (const juce::AudioBuffer<float>& buffer, int channel)
    {
        double sumSquares = 0.0;
        const auto* data = buffer.getReadPointer (channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            sumSquares += (double) data[i] * (double) data[i];
        return (float) std::sqrt (sumSquares / buffer.getNumSamples());
    }
}

TEST_CASE ("A fresh BazaltAudioProcessor opens on the plain master-out-only graph, silent",
           "[plugin][InitPatch]")
{
    BazaltAudioProcessor processor;
    const auto& graph = processor.getGraphEditController().getGraph();

    CHECK (graph.getNodes().size() == 1);
    REQUIRE (graph.findNode ("masterOut") != nullptr);

    processor.prepareToPlay (44100.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    juce::MidiBuffer none;
    processor.processBlock (buffer, none);

    CHECK (rms (buffer, 0) == 0.0f); // nothing wired in - genuinely silent, not just quiet
}

TEST_CASE ("The Init Patch plays a real, finite, bounded note through real MIDI", "[plugin][InitPatch][M22]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (bazalt::engine::buildInitPatchGraph()).success);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    float peak = 0.0f;
    juce::MidiBuffer none;

    for (int block = 0; block < 40; ++block) // ~460ms - past both envelopes' attack/decay into sustain
    {
        buffer.clear();
        processor.processBlock (buffer, none);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto s = buffer.getSample (ch, i);
                REQUIRE (std::isfinite (s));
                peak = std::max (peak, std::fabs (s));
            }
    }

    CHECK (peak > 0.01f);  // genuinely audible
    CHECK (peak <= 1.5f);  // no runaway (the ladder's resonance clamp, the amp envelope, all doing their job together)
}

TEST_CASE ("The Init Patch releases and falls silent after note-off", "[plugin][InitPatch][M22]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (bazalt::engine::buildInitPatchGraph()).success);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    juce::MidiBuffer none;
    for (int block = 0; block < 10; ++block) // let it establish
    {
        buffer.clear();
        processor.processBlock (buffer, none);
    }

    juce::MidiBuffer noteOff;
    noteOff.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    buffer.clear();
    processor.processBlock (buffer, noteOff);

    for (int block = 0; block < 60; ++block) // past release + the ladder's own resonant tail
    {
        buffer.clear();
        processor.processBlock (buffer, none);
    }

    CHECK (rms (buffer, 0) < 0.001f);
}

TEST_CASE ("The Init Patch is genuinely polyphonic: different notes sound different, simultaneously",
           "[plugin][InitPatch][M22]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (bazalt::engine::buildInitPatchGraph()).success);

    juce::MidiBuffer chord;
    chord.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
    chord.addEvent (juce::MidiMessage::noteOn (1, 55, (juce::uint8) 100), 0);
    chord.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    processor.processBlock (buffer, chord);

    juce::MidiBuffer none;
    for (int block = 0; block < 10; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, none);
    }

    // A real 3-note chord is measurably louder than one voice alone -
    // proof the voices are genuinely summing, not stepping on each other.
    const auto chordRms = rms (buffer, 0);

    BazaltAudioProcessor single;
    single.prepareToPlay (44100.0, 512);
    REQUIRE (single.getGraphEditController().setGraph (bazalt::engine::buildInitPatchGraph()).success);
    juce::MidiBuffer oneNote;
    oneNote.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> singleBuffer (2, 512);
    singleBuffer.clear();
    single.processBlock (singleBuffer, oneNote);
    for (int block = 0; block < 10; ++block)
    {
        singleBuffer.clear();
        single.processBlock (singleBuffer, none);
    }
    const auto singleRms = rms (singleBuffer, 0);

    CHECK (chordRms > singleRms * 1.3f);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            CHECK (std::isfinite (buffer.getSample (ch, i)));
}

TEST_CASE ("The Init Patch never allocates on the audio thread, held note included",
           "[plugin][InitPatch][rt-safety][M22]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (bazalt::engine::buildInitPatchGraph()).success);

    juce::AudioBuffer<float> warmUp (2, 512);
    warmUp.clear();
    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    processor.processBlock (warmUp, noteOn); // one untrapped warm-up block, note-on included

    for (int block = 0; block < 10; ++block)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        juce::MidiBuffer none;

        bazalt::engine::ScopedAudioThreadAllocationTrap trap;
        processor.processBlock (buffer, none);
    }
}
