#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"

using namespace bazalt;

TEST_CASE ("BazaltAudioProcessor declares main stereo in/out plus 4 aux stereo sidechain inputs",
           "[plugin][buses]")
{
    BazaltAudioProcessor processor;

    REQUIRE (processor.getBusCount (true) == 1 + BazaltAudioProcessor::numAuxBuses);
    REQUIRE (processor.getBusCount (false) == 1);

    CHECK (processor.getChannelCountOfBus (true, 0) == 2);
    CHECK (processor.getChannelCountOfBus (false, 0) == 2);

    // Sidechain 1 is active by default (Ableton Live never activates an
    // inactive VST3 aux bus); 2-4 declare stereo but start inactive.
    CHECK (processor.getChannelCountOfBus (true, 1) == 2);
    for (int i = 1; i < BazaltAudioProcessor::numAuxBuses; ++i)
        CHECK (processor.getChannelCountOfBus (true, i + 1) == 0);
}

TEST_CASE ("Two processor instances get distinct identities", "[plugin][multi-instance]")
{
    // A real, DAW-relevant gap found and fixed while researching crash
    // risks before testing in a real host (Ableton): PluginEditor.cpp used
    // to point EVERY instance's WebView2 at the exact same fixed user-data
    // folder - never exercised by the Standalone app (always exactly one
    // instance), but a real host routinely runs several instances of the
    // same plugin at once. getInstanceId() is what makes each instance's
    // own folder unique; this just confirms two separately-constructed
    // processors never collide.
    BazaltAudioProcessor a, b;
    CHECK (a.getInstanceId() != b.getInstanceId());
}

TEST_CASE ("The default bus layout (main stereo, Sidechain 1 on, 2-4 inactive) is supported", "[plugin][buses]")
{
    BazaltAudioProcessor processor;
    CHECK (processor.checkBusesLayoutSupported (processor.getBusesLayout()));
}

TEST_CASE ("A mono main output is rejected", "[plugin][buses]")
{
    BazaltAudioProcessor processor;

    auto layout = processor.getBusesLayout();
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();

    CHECK_FALSE (processor.checkBusesLayoutSupported (layout));
}

TEST_CASE ("Activating all 4 sidechain aux buses as stereo is still a supported layout", "[plugin][buses]")
{
    BazaltAudioProcessor processor;

    auto layout = processor.getBusesLayout();
    for (int i = 0; i < BazaltAudioProcessor::numAuxBuses; ++i)
        layout.inputBuses.getReference (i + 1) = juce::AudioChannelSet::stereo();

    CHECK (processor.checkBusesLayoutSupported (layout));
}

TEST_CASE ("A mono aux bus is accepted (a host may offer its sidechain in mono); a surround one is not", "[plugin][buses]")
{
    BazaltAudioProcessor processor;

    auto layout = processor.getBusesLayout();
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::mono();
    CHECK (processor.checkBusesLayoutSupported (layout));

    layout.inputBuses.getReference (1) = juce::AudioChannelSet::create5point1();
    CHECK_FALSE (processor.checkBusesLayoutSupported (layout));
}

// Ableton deactivates and reactivates the plugin around a bus-layout change
// (switching its sidechain on): releaseResources(), then prepareToPlay() with
// the same settings. Scopes and meters must come back, not just the audio.
TEST_CASE ("Visualizations keep running after a release and an unchanged re-prepare", "[plugin][buses]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.isAnalysisRunningForTesting());

    processor.releaseResources();
    auto layout = processor.getBusesLayout();
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::mono();
    REQUIRE (processor.setBusesLayout (layout));
    processor.prepareToPlay (44100.0, 512);

    CHECK (processor.isAnalysisRunningForTesting());
    processor.releaseResources();
}
