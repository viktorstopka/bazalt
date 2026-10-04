// LiveParameterEdits: values streamed while a slider is dragged glide into
// the running nodes without a recompile, and the slot frees itself once the
// drag is released and the glide has arrived.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "LiveParameterEdits.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"

using namespace bazalt::engine;

namespace
{
    double liveFrequency (ExecutionPlan& plan)
    {
        PhaseSnapshot snapshot;
        plan.getNodeById ("osc")->capturePhaseSnapshot (snapshot); // reports the frequency it last ran at
        return snapshot.frequencyHz;
    }
}

TEST_CASE ("A live edit glides the running node to the dragged value, then frees its slot on release",
           "[plugin][LiveParameterEdits]")
{
    NodeGraph graph;
    graph.addNode ({ "osc", "osc.sine", {}, { { "osc.sine.frequency", 100.0f } }, {} });
    graph.setOutput ("osc", "out");
    auto factory = buildDefaultNodeFactory();
    auto compiled = GraphCompiler::compile (graph, factory, { 48000.0, 64 }, 1);
    REQUIRE (compiled.success);
    auto* plans = &compiled.plan;

    bazalt::LiveParameterEdits live;
    live.prepare (48000.0);

    auto block = [&]
    {
        live.advance (64);
        live.applyToPlans (&plans, 1);
        compiled.plan.process (64);
        live.retireFinished();
    };

    // The first value of a drag applies at once...
    REQUIRE (live.set ("osc", "osc.sine.frequency", 200.0f));
    block();
    CHECK (liveFrequency (compiled.plan) == Catch::Approx (200.0));

    // ...later ones glide: one block (1.3 ms) moves only part of the way.
    live.set ("osc", "osc.sine.frequency", 400.0f);
    block();
    const auto partway = liveFrequency (compiled.plan);
    CHECK (partway > 200.0);
    CHECK (partway < 300.0);

    for (int i = 0; i < 200; ++i) // ~270 ms: arrived
        block();
    CHECK (liveFrequency (compiled.plan) == Catch::Approx (400.0));

    // Still held: the slot stays. Released: it frees itself once arrived.
    CHECK (live.isActive ("osc", "osc.sine.frequency"));
    live.release ("osc", "osc.sine.frequency");
    block();
    CHECK_FALSE (live.isActive ("osc", "osc.sine.frequency"));
}
