#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include <algorithm>
#include <cmath>
#include <vector>

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
    // 0.x arc, 2026-09-29: the constructor default is a plain master-out-only
    // graph now, not a playable voice chain - set one explicitly.
    REQUIRE (processor.getGraphEditController().setGraph (bazalt::engine::buildVoiceProofGraph()).success);

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
    REQUIRE (processor.getGraphEditController().setGraph (bazalt::engine::buildVoiceProofGraph()).success);

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
    REQUIRE (processor.getGraphEditController().setGraph (bazalt::engine::buildVoiceProofGraph()).success);

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

TEST_CASE ("A per-voice delay tail keeps the voice alive past its envelope's own release",
           "[plugin][render][M17]")
{
    // M17's generic, signal-level silence detector (VoiceManager.h) is
    // what makes this possible — the old hardcoded check
    // (dynamic_cast<AdsrNode*>("env")->isActive()) would have freed the
    // voice the moment the ENVELOPE alone finished releasing, silently
    // dropping whatever real tail a downstream delay/reverb still had to
    // play (RECONCILIATION.md 3.2's exact documented gap).
    //
    // Rather than hardcoding exactly when JUCE's ADSR release curve and the
    // delay's own lag line up (fragile — got this wrong once already), this
    // compares the SAME note-on/release timing with and without a delay
    // spliced after amp: the delay-tail run must stay audible for
    // meaningfully more blocks than the plain envelope-only run, whatever
    // the exact sample counts are.
    auto lastAudibleBlock = [] (bool withDelayTail) -> int
    {
        BazaltAudioProcessor processor;
        processor.prepareToPlay (44100.0, 512);
        // buildInitPatchGraph() (M22's own default) has no "amp" node - this
        // test's own splice point is buildVoiceProofGraph()'s, needed explicitly.
        REQUIRE (processor.getGraphEditController().setGraph (bazalt::engine::buildVoiceProofGraph()).success);

        if (withDelayTail)
        {
            auto& controller = processor.getGraphEditController();
            REQUIRE (controller.addNode ("time.delay", "tail", 0.0f, 0.0f).success);
            REQUIRE (controller.connect ("amp", "out", "tail", "in").success);
            REQUIRE (controller.setParameterValue ("tail", "time.delay.samples", 4000.0f).success); // ~90ms @44.1kHz, near DelayNode's default 4096-sample ceiling
            REQUIRE (controller.setOutput ("tail", "out").success);
        }

        juce::AudioBuffer<float> buffer (2, 512);

        juce::MidiBuffer noteOn;
        noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        buffer.clear();
        processor.processBlock (buffer, noteOn);

        // Let the note establish (well within attack+decay+sustain, and
        // long enough that the delay's ring buffer is full of real,
        // non-zero signal) before releasing it.
        for (int block = 0; block < 5; ++block)
        {
            buffer.clear();
            juce::MidiBuffer empty;
            processor.processBlock (buffer, empty);
        }

        juce::MidiBuffer noteOff;
        noteOff.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        buffer.clear();
        processor.processBlock (buffer, noteOff);

        int lastAudible = -1;
        for (int block = 0; block < 60; ++block)
        {
            buffer.clear();
            juce::MidiBuffer empty;
            processor.processBlock (buffer, empty);
            if (rms (buffer, 0) > 0.0005f)
                lastAudible = block;
        }

        return lastAudible;
    };

    const auto withoutTail = lastAudibleBlock (false);
    const auto withTail = lastAudibleBlock (true);

    REQUIRE (withoutTail >= 0);
    REQUIRE (withTail >= 0);
    CHECK (withTail > withoutTail); // the delay's own lag genuinely extends audible output past the envelope's own release
}

TEST_CASE ("A pitch-bend message continuously shifts pitch with no special-cased path",
           "[plugin][render][M18]")
{
    // M18's own exit criterion (ADR-0024): pitch bend folds straight into
    // io.noteIn's continuous "pitch" output, so it needs no dedicated
    // routing beyond the ordinary Control connection osc.analog's "pitch"
    // port already has. Proven here by confirming a pitch-wheel message
    // measurably changes the rendered waveform, not by measuring the
    // resulting frequency precisely.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.getGraphEditController().setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    juce::AudioBuffer<float> buffer (2, 512);

    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    buffer.clear();
    processor.processBlock (buffer, noteOn);

    juce::MidiBuffer empty;
    for (int block = 0; block < 10; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, empty);
    }

    buffer.clear();
    processor.processBlock (buffer, empty);
    const std::vector<float> unbent (buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples());

    juce::MidiBuffer bend;
    bend.addEvent (juce::MidiMessage::pitchWheel (1, 8192 + 4096), 0); // +1 semitone at the default +-2 semitone range
    buffer.clear();
    processor.processBlock (buffer, bend);

    for (int block = 0; block < 5; ++block) // let the new frequency's phase visibly diverge
    {
        buffer.clear();
        processor.processBlock (buffer, empty);
    }

    buffer.clear();
    processor.processBlock (buffer, empty);
    const std::vector<float> bent (buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples());

    for (auto s : bent)
        REQUIRE (std::isfinite (s));

    double diffSumSquares = 0.0;
    for (size_t i = 0; i < unbent.size(); ++i)
    {
        const auto diff = bent[i] - unbent[i];
        diffSumSquares += (double) diff * (double) diff;
    }
    const auto diffRms = (float) std::sqrt (diffSumSquares / (double) unbent.size());

    CHECK (diffRms > 0.02f); // the bent waveform meaningfully diverges from the unbent one
}
