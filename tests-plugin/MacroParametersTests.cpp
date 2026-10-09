#include <catch2/catch_test_macros.hpp>
#include "MacroParameters.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include <atomic>
#include <thread>

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
    macros.setMappings ({ { 0, "osc", "source.oscillator.frequency", 100.0f, 2000.0f } });
    macros.getParameter (0).setValueNotifyingHost (0.5f);

    auto graph = bazalt::engine::buildVoiceProofGraph();
    auto factory = bazalt::engine::buildDefaultNodeFactory();
    auto result = bazalt::engine::GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);

    bazalt::engine::ExecutionPlan* plans[3] = { nullptr, &result.plan, nullptr };
    macros.advanceSmoothers (64); // real call order: advance once, then apply per domain
    macros.applyToPlans (plans, 3, 64); // must not crash
    SUCCEED ("applyToPlans handled null plan entries without crashing");
}

TEST_CASE ("MacroParameters::applyToPlans reads the smoother's current value without advancing it - "
           "a second call in the same block (simulating a second active domain) sees the identical value",
           "[plugin][MacroParameters]")
{
    // Regression test: PluginProcessor::processBlock calls applyToPlans() once
    // per active domain (once per origin bundle, once more for a shared global
    // plan) but must only ever advance each macro's ~20ms smoothing ramp ONCE
    // per block, via the separate advanceSmoothers() call - not once per
    // applyToPlans() call. The old combined implementation advanced the ramp
    // on every applyToPlans() call, so a block with N active domains ticked
    // the ramp N times and could even read two different interpolated values
    // for the SAME macro within one block.
    DummyProcessor dummy;
    MacroParameters macros;
    macros.addParametersTo (dummy);
    macros.prepare (44100.0);
    macros.setMappings ({ { 0, "k", "util.constant.value", 0.0f, 1.0f } });
    macros.getParameter (0).setValueNotifyingHost (1.0f); // ramp target: raw 1.0 -> mapped 1.0

    bazalt::engine::NodeGraph graph;
    graph.addNode ({ "k", "util.constant", {}, {}, {} });
    graph.setOutput ("k", "out");
    auto factory = bazalt::engine::buildDefaultNodeFactory();
    auto result = bazalt::engine::GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);

    bazalt::engine::ExecutionPlan* plans[1] = { &result.plan };

    macros.advanceSmoothers (64); // one block's worth of ramp time - a 20ms ramp at
                                   // 44.1kHz needs ~882 samples, so 64 is well short
                                   // of the target, which is what makes this test able
                                   // to tell "didn't advance further" apart from
                                   // "already arrived at the target either way".

    macros.applyToPlans (plans, 1, 64);
    result.plan.process (64);
    const auto afterFirstApply = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex]
                                      .getBlock()
                                      .getChannelPointer (0)[0];

    // A SECOND applyToPlans() call in the SAME block, no intervening
    // advanceSmoothers() - exactly what a second active origin bundle or a
    // shared global plan triggers in the real processBlock().
    macros.applyToPlans (plans, 1, 64);
    result.plan.process (64);
    const auto afterSecondApply = result.plan.blockBuffers[(size_t) result.plan.finalOutputBufferIndex]
                                       .getBlock()
                                       .getChannelPointer (0)[0];

    CHECK (afterFirstApply > 0.0f);
    CHECK (afterFirstApply < 1.0f); // genuinely mid-ramp, not already settled
    CHECK (afterSecondApply == afterFirstApply);
}

TEST_CASE ("MacroParameters::setMappings/applyToPlans never produce a torn read under real concurrent load",
           "[plugin][MacroParameters][swap-under-load]")
{
    // Regression test for a real audio-thread data race: setMappings() (message
    // thread, called on every single graph-edit command once util.macro became
    // real) used to reassign a plain std::vector<MacroMapping> member that
    // applyToPlans() (audio thread, every processBlock()) range-for'd over with
    // no synchronization at all. Races both functions against each other for
    // real wall-clock time and checks for any crash/hang/torn read - same shape
    // as PlanSwapperTests.cpp's own "Swap-under-load" concurrency test.
    DummyProcessor dummy;
    MacroParameters macros;
    macros.addParametersTo (dummy);
    macros.prepare (44100.0);
    for (int i = 0; i < MacroParameters::numMacros; ++i)
        macros.getParameter (i).setValueNotifyingHost (1.0f);

    bazalt::engine::NodeGraph graph;
    graph.addNode ({ "k", "util.constant", {}, {}, {} });
    graph.setOutput ("k", "out");
    auto factory = bazalt::engine::buildDefaultNodeFactory();
    auto result = bazalt::engine::GraphCompiler::compile (graph, factory, { 44100.0, 64 }, 1);
    REQUIRE (result.success);

    bazalt::engine::ExecutionPlan* plans[1] = { &result.plan };

    std::atomic<bool> stop { false };
    std::atomic<uint64_t> blocksProcessed { 0 };

    std::thread audioThread ([&]
    {
        while (! stop.load (std::memory_order_relaxed))
        {
            macros.advanceSmoothers (64);
            macros.applyToPlans (plans, 1, 64); // must never crash/hang/read torn data
            result.plan.process (64);
            blocksProcessed.fetch_add (1, std::memory_order_relaxed);
        }
    });

    std::thread editorThread ([&]
    {
        for (uint64_t generation = 0; generation < 20000; ++generation)
        {
            const auto slot = (int) (generation % (uint64_t) MacroParameters::numMacros);
            macros.setMappings ({ { slot, "k", "util.constant.value", 0.0f, 1.0f } });
        }
    });

    editorThread.join();
    stop.store (true, std::memory_order_relaxed);
    audioThread.join();

    CHECK (blocksProcessed.load() > 0);
}
