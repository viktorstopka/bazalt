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

    // Aux buses declare a stereo layout but are inactive by default — a
    // host that never activates them must still be a supported layout
    // (ARCHITECTURE.md §4.1).
    for (int i = 0; i < BazaltAudioProcessor::numAuxBuses; ++i)
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

TEST_CASE ("The default bus layout (main stereo, aux sidechains inactive) is supported", "[plugin][buses]")
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

TEST_CASE ("A non-stereo aux bus layout is rejected", "[plugin][buses]")
{
    BazaltAudioProcessor processor;

    auto layout = processor.getBusesLayout();
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::mono();

    CHECK_FALSE (processor.checkBusesLayoutSupported (layout));
}
