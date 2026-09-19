#include <catch2/catch_test_macros.hpp>
#include "MacroParameters.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"

using namespace bazalt;

namespace
{
    // Minimal host just to give MacroParameters::addParametersTo() somewhere
    // real to register its juce::AudioParameterFloats — every pure virtual
    // is a trivial stub, nothing here is exercised by this test.
    class DummyProcessor : public juce::AudioProcessor
    {
    public:
        const juce::String getName() const override { return "Dummy"; }
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
    };
}

TEST_CASE ("MacroParameters::applyToPlans skips a null plan instead of dereferencing it",
           "[plugin][MacroParameters]")
{
    // Regression test for a real Standalone-app crash: on the very first
    // processBlock() call, a voice's PlanSwapper can legitimately still be
    // unpublished (nullptr) — JUCE doesn't guarantee prepareToPlay() has
    // fully returned on the message thread before the audio callback starts
    // firing on its own thread. applyToPlans() originally dereferenced
    // plans[i] unconditionally; this reproduces exactly that shape (a null
    // entry mixed with a real one) and must not crash.
    DummyProcessor dummy;
    MacroParameters macros;
    macros.addParametersTo (dummy);
    macros.prepare (44100.0);
    macros.setMappings ({ { 0, "osc", "osc.analog.frequency", 100.0f, 2000.0f } });
    macros.getParameter (0).setValueNotifyingHost (0.5f);

    auto graph = bazalt::engine::buildVoiceProofGraph();
    auto factory = bazalt::engine::buildDefaultNodeFactory();
    auto result = bazalt::engine::GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);

    bazalt::engine::ExecutionPlan* plans[3] = { nullptr, &result.plan, nullptr };
    macros.applyToPlans (plans, 3, 64); // must not crash
    SUCCEED ("applyToPlans handled null plan entries without crashing");
}
