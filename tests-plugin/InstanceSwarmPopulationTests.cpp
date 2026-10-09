// Domain Extensions batch — instance.allocate.swarmPopulation's real
// plugin-level behavior: an "always live, fixed count" origin that never
// calls VoiceManager::noteOn()/noteOff() at all (DOMAINS.md §3's own
// "fixed count, always live... no spawn logic"), so it needs its own
// bypass of VoiceManager's whole Idle/Active/Releasing/Stealing stage
// machine in renderOriginVoiceRange — these tests prove that bypass
// actually produces real, population-size-dependent audio with ZERO MIDI
// involved, and that the instance-count badge reports it correctly.
#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "GraphEditController.h"
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

    // Builds a swarm of `populationSize` identical oscillators (random1's
    // per-slot deterministic value drives each one's pitch via an
    // auto-inserted adapter, so they're not all perfectly in phase/pitch —
    // irrelevant for these tests, which only care about gross RMS) summed
    // through instance.sum into the processor's own default Master Out —
    // no io.noteIn, no envelope, no gate wiring at all. Deliberately
    // minimal: proving this origin needs no MIDI/spawn signal whatsoever to
    // produce real audio IS the point.
    //
    // Goes through the real GraphEditController command sequence
    // (addNode/connectWithAutoAdapt/connect), the same path a live UI
    // drag-to-wire gesture would use — NOT a raw NodeGraph::addConnection
    // built by hand, which enforces the same canConnect type/quantity
    // rules but never auto-inserts the adapter chain a direct Bipolar
    // (random1) -> Pitch (osc's "pitch" input) connection needs.
    bool buildSwarmPopulationGraph (GraphEditController& controller, int populationSize)
    {
        if (! controller.addNode ("life.swarmPopulation", "swarm", 0.0f, 0.0f).success)
            return false;
        if (! controller.setParameterValue (
                "swarm", "life.swarmPopulation.populationSize", (float) populationSize).success)
            return false;
        if (! controller.addNode ("osc.analog", "osc", 200.0f, 0.0f).success)
            return false;
        if (! controller.addNode ("life.merge", "sum", 400.0f, 0.0f).success)
            return false;
        if (! controller.connectWithAutoAdapt ("swarm", "random1", "osc", "pitch").success)
            return false;
        if (! controller.connect ("osc", "out", "sum", "in").success)
            return false;
        // The default graph (ProofGraphs.h::buildMasterOutOnlyGraph(), live
        // from construction per CLAUDE.md) already has an unconnected
        // "masterOut" node with nothing wired into its own "in" — route the
        // swarm's summed output there directly (mono -> stereo is a free
        // broadcast, OutputNode.h's own doc comment).
        return controller.connect ("sum", "out", "masterOut", "in").success;
    }
}

TEST_CASE ("instance.allocate.swarmPopulation produces real audio with zero MIDI/spawn input at all",
           "[plugin][InstanceSwarmPopulation]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (buildSwarmPopulationGraph (controller, 4));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer noMidi; // deliberately empty - this origin needs none

    float peakRms = 0.0f;
    for (int block = 0; block < 5; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, noMidi);
        peakRms = juce::jmax (peakRms, rms (buffer, 0));
    }

    CHECK (peakRms > 0.001f);
}

TEST_CASE ("life.swarmPopulation's populationSize genuinely scales how many instances "
           "are summed - more oscillators measure louder",
           "[plugin][InstanceSwarmPopulation]")
{
    auto measureRms = [] (int populationSize)
    {
        BazaltAudioProcessor processor;
        processor.prepareToPlay (44100.0, 512);
        REQUIRE (buildSwarmPopulationGraph (processor.getGraphEditController(), populationSize));

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer noMidi;
        float peakRms = 0.0f;
        for (int block = 0; block < 5; ++block)
        {
            buffer.clear();
            processor.processBlock (buffer, noMidi);
            peakRms = juce::jmax (peakRms, rms (buffer, 0));
        }
        return peakRms;
    };

    const auto rmsOne = measureRms (1);
    const auto rmsFour = measureRms (4);

    // Four oscillators (different per-slot pitch via random1, not
    // perfectly in-phase/unison) sum to measurably louder than one - a
    // loose bound (> 1.3x) rather than an exact 4x to stay robust against
    // partial cancellation/the default oscillator's own waveform shape,
    // not pin an unrelated implementation detail.
    CHECK (rmsFour > rmsOne * 1.3f);
}

TEST_CASE ("life.swarmPopulation's own slots beyond populationSize never run at all - "
           "a populationSize of 1 stays far quieter than the full 8-slot pool would be",
           "[plugin][InstanceSwarmPopulation]")
{
    // Indirect proof that renderOriginVoiceRange's own liveCount bound
    // (min(populationSize, numVoices)) is respected - if slots beyond
    // populationSize somehow still rendered, a populationSize=1 graph would
    // sound as loud as a populationSize=8 one (8 oscillators instead of 1).
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (buildSwarmPopulationGraph (processor.getGraphEditController(), 1));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer noMidi;
    buffer.clear();
    processor.processBlock (buffer, noMidi);

    // A single PolyBLEP saw/sine at unity gain into instance.sum stays
    // comfortably under 1.0 RMS - a gross sanity bound against "every
    // physical slot rendered and summed" blowing this far past what one
    // oscillator could ever produce.
    CHECK (rms (buffer, 0) < 1.0f);
}

TEST_CASE ("life.swarmPopulation's instance-count badge reports populationSize for "
           "both active and max, not VoiceManager's own (permanently-zero) count",
           "[plugin][InstanceSwarmPopulation]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (buildSwarmPopulationGraph (controller, 5));

    const auto indices = controller.getOriginBundleIndices();
    const auto it = indices.find ("swarm");
    REQUIRE (it != indices.end());

    CHECK (processor.getOriginActiveVoiceCount (it->second) == 5);
    CHECK (processor.getOriginMaxVoices (it->second) == 5);
}
