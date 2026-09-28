// M22 wave 6: the Init Patch (NODE_CATALOG.md's Part B one-liner, built for
// real in ProofGraphs.h::buildInitPatchGraph()) compiles through the real
// engine pipeline. tests-plugin/InitPatchTests.cpp is the companion proof
// that it actually PLAYS, through the real MIDI-driven processor - this file
// is the lighter-weight, engine-only compile/shape check.
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/DomainSplitter.h"

using namespace bazalt::engine;

TEST_CASE ("buildInitPatchGraph() has a real global domain - instance.mix genuinely exercised, not a synthetic test graph",
           "[engine][InitPatch][M22]")
{
    const auto graph = buildInitPatchGraph();
    const auto split = DomainSplitter::split (graph);

    REQUIRE (split.success);
    CHECK (split.hasGlobalDomain); // instance.mix is really in this graph
    CHECK_FALSE (split.monoOnly);  // instance.allocate.voice is really in this graph too
}

TEST_CASE ("buildInitPatchGraph() compiles, voice domain and global domain both", "[engine][InitPatch][M22]")
{
    auto factory = buildDefaultNodeFactory();
    const auto graph = buildInitPatchGraph();
    const auto split = DomainSplitter::split (graph);
    REQUIRE (split.success);

    const auto voiceCompile = GraphCompiler::compile (split.voiceGraph, factory, { 44100.0, 512 }, 1);
    INFO (voiceCompile.errorMessage);
    REQUIRE (voiceCompile.success);

    const auto globalCompile = GraphCompiler::compile (split.globalGraph, factory, { 44100.0, 512 }, 1);
    INFO (globalCompile.errorMessage);
    REQUIRE (globalCompile.success);

    // Every node the doc comment claims is really there.
    for (const auto* id : { "noteIn", "allocator", "osc1", "osc2", "oscMix", "ladder", "filterEnv", "ampEnv",
                            "ampVCA" })
        CHECK (split.voiceGraph.findNode (id) != nullptr);
    for (const auto* id : { "voiceMix", "masterOut" })
        CHECK (split.globalGraph.findNode (id) != nullptr);
}

TEST_CASE ("buildInitPatchGraph() compiles at every sample rate this project claims to support",
           "[engine][InitPatch][M22]")
{
    auto factory = buildDefaultNodeFactory();
    const auto graph = buildInitPatchGraph();
    const auto split = DomainSplitter::split (graph);
    REQUIRE (split.success);

    for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        INFO ("sample rate = " << sampleRate);
        const auto voiceCompile = GraphCompiler::compile (split.voiceGraph, factory, { sampleRate, 512 }, 1);
        CHECK (voiceCompile.success);
        const auto globalCompile = GraphCompiler::compile (split.globalGraph, factory, { sampleRate, 512 }, 1);
        CHECK (globalCompile.success);
    }
}
