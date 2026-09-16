#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include <algorithm>
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

TEST_CASE ("A MIDI note-on produces real, finite audio on the main output", "[plugin][render]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);

    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    for (int block = 0; block < 19; ++block)
    {
        buffer.clear();
        juce::MidiBuffer empty;
        processor.processBlock (buffer, empty);
    }

    CHECK (rms (buffer, 0) > 0.01f);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (std::isfinite (buffer.getSample (ch, i)));
}

TEST_CASE ("Note-off releases the voice and the signal decays to silence", "[plugin][render]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    for (int block = 0; block < 10; ++block)
    {
        buffer.clear();
        juce::MidiBuffer empty;
        processor.processBlock (buffer, empty);
    }

    REQUIRE (rms (buffer, 0) > 0.01f); // still sounding (sustain)

    juce::MidiBuffer noteOff;
    noteOff.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    buffer.clear();
    processor.processBlock (buffer, noteOff);

    for (int block = 0; block < 40; ++block)
    {
        buffer.clear();
        juce::MidiBuffer empty;
        processor.processBlock (buffer, empty);
    }

    CHECK (rms (buffer, 0) < 0.001f); // released and decayed to silence
}

TEST_CASE ("Different MIDI notes trigger different voices, playable polyphonically", "[plugin][render]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);

    juce::MidiBuffer chord;
    chord.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    chord.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
    chord.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);

    buffer.clear();
    processor.processBlock (buffer, chord);

    for (int block = 0; block < 10; ++block)
    {
        buffer.clear();
        juce::MidiBuffer empty;
        processor.processBlock (buffer, empty);
    }

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (std::isfinite (buffer.getSample (ch, i)));

    CHECK (rms (buffer, 0) > 0.01f);
}

TEST_CASE ("A hard, instant macro automation jump never destabilizes the engine", "[plugin][macros]")
{
    // The smoothing mechanism itself (SmoothedParameter ramping toward a
    // target over ~20ms) is unit-tested directly at the engine level
    // (tests/SmoothedParameterTests.cpp) — what's worth proving here is
    // that a host slamming a macro from one extreme to the other doesn't
    // produce instability (NaN/Inf, runaway amplitude) in the connected
    // node. Confirming the result is subjectively click-free is a manual/
    // DAW-listening check, not something this automated suite claims.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    auto& parameters = processor.getParameters();
    auto* cutoffMacro = dynamic_cast<juce::AudioParameterFloat*> (parameters[1]); // macro 2 -> filter cutoff
    REQUIRE (cutoffMacro != nullptr);

    *cutoffMacro = 1.0f; // instant jump to the opposite extreme

    float maxAbsSample = 0.0f;

    for (int block = 0; block < 10; ++block)
    {
        buffer.clear();
        juce::MidiBuffer empty;
        processor.processBlock (buffer, empty);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                REQUIRE (std::isfinite (data[i]));
                maxAbsSample = std::max (maxAbsSample, std::abs (data[i]));
            }
        }
    }

    CHECK (maxAbsSample < 10.0f); // bounded, no runaway
}
