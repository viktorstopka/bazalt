#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/PlanSwapper.h"
#include "bazalt/engine/graph/Node.h"
#include <atomic>
#include <memory>
#include <thread>

using namespace bazalt::engine;

namespace
{
    class DcNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return 1; }
        void processSample (const float*, float* outputs) noexcept override { outputs[0] = value; }

        float value = 0.0f;
    };

    // Hand-built (bypassing GraphCompiler) single-node plan whose only
    // output is a constant equal to its own generation — the simplest
    // signal that makes "was this whole block computed against one plan"
    // trivially checkable.
    std::unique_ptr<ExecutionPlan> makeDcPlan (uint64_t generation, int maxBlockSize)
    {
        auto plan = std::make_unique<ExecutionPlan>();
        plan->generation = generation;
        plan->maxBlockSize = maxBlockSize;
        plan->silenceBuffer.assign ((size_t) maxBlockSize, 0.0f);

        auto node = std::make_unique<DcNode>();
        node->value = (float) generation;
        plan->nodes.push_back (std::move (node));
        plan->nodeIdToSlot["dc"] = 0;

        AlignedBuffer buffer;
        buffer.resize (1, (size_t) maxBlockSize);
        plan->blockBuffers.push_back (std::move (buffer));

        ExecutionPlan::BlockStep blockStep;
        blockStep.nodeSlot = 0;
        blockStep.outputBufferIndices = { 0 };

        ExecutionPlan::Step step;
        step.kind = ExecutionPlan::Step::Kind::Block;
        step.block = std::move (blockStep);
        plan->steps.push_back (std::move (step));

        plan->finalOutputBufferIndex = 0;
        return plan;
    }
}

TEST_CASE ("PlanSwapper publishes and serves plans in order", "[engine][PlanSwapper]")
{
    PlanSwapper swapper;
    REQUIRE (swapper.getCurrentPlanForAudioThread() == nullptr);

    REQUIRE (swapper.publish (makeDcPlan (1, 64)));
    auto* plan1 = swapper.getCurrentPlanForAudioThread();
    REQUIRE (plan1 != nullptr);
    CHECK (plan1->generation == 1);

    REQUIRE (swapper.publish (makeDcPlan (2, 64)));
    auto* plan2 = swapper.getCurrentPlanForAudioThread();
    REQUIRE (plan2 != nullptr);
    CHECK (plan2->generation == 2);
}

TEST_CASE ("PlanSwapper::reclaim frees superseded slots but keeps whatever the audio thread last touched",
           "[engine][PlanSwapper]")
{
    PlanSwapper swapper;

    REQUIRE (swapper.publish (makeDcPlan (1, 64)));
    swapper.getCurrentPlanForAudioThread(); // audio-thread epoch = 1

    REQUIRE (swapper.publish (makeDcPlan (2, 64)));
    // Audio thread hasn't picked up generation 2 yet -> epoch is still 1.
    swapper.reclaim();
    CHECK (swapper.getNumOccupiedSlots() == 2); // both must survive: epoch(1) and current(2)

    swapper.getCurrentPlanForAudioThread(); // epoch advances to 2
    swapper.reclaim();
    CHECK (swapper.getNumOccupiedSlots() == 1); // generation 1 is now safe to free
}

TEST_CASE ("PlanSwapper::publish fails once every slot is occupied and unreclaimed", "[engine][PlanSwapper]")
{
    PlanSwapper swapper;

    for (int i = 1; i <= PlanSwapper::numSlots; ++i)
        REQUIRE (swapper.publish (makeDcPlan ((uint64_t) i, 64)));

    CHECK_FALSE (swapper.publish (makeDcPlan (99, 64))); // no free slot, reclaim() never ran
}

TEST_CASE ("reclaim() immediately before publish absorbs a burst of edits with zero process() calls between them",
           "[engine][PlanSwapper][NODE_EDITOR]")
{
    // The scenario ADR-0003's M7 revision documents: GraphEditController
    // publishes on every command, and a host with a stopped transport may
    // never call process() between them, so reclaim() (which can only free
    // a slot once the audio thread's epoch has moved past it) has nothing
    // to work with — publish() would need to succeed on pool size alone.
    // This proves numSlots is generous enough for a realistic edit burst
    // (more edits than the pre-M7 4-slot pool could have absorbed) with no
    // process() calls at all, calling reclaim() before each publish the
    // same way GraphEditController::recompileAndPublish() does.
    PlanSwapper swapper;

    for (int i = 1; i <= PlanSwapper::numSlots; ++i)
    {
        swapper.reclaim();
        REQUIRE (swapper.publish (makeDcPlan ((uint64_t) i, 64)));
    }

    auto* plan = swapper.getCurrentPlanForAudioThread();
    REQUIRE (plan != nullptr);
    CHECK (plan->generation == (uint64_t) PlanSwapper::numSlots);
}

TEST_CASE ("Swap-under-load: a plan swap never produces a torn read within a single process() call",
           "[engine][PlanSwapper][swap-under-load]")
{
    constexpr int maxBlockSize = 64;
    PlanSwapper swapper;
    REQUIRE (swapper.publish (makeDcPlan (0, maxBlockSize)));

    std::atomic<bool> stop { false };
    std::atomic<bool> discontinuityWithinBlock { false };
    std::atomic<uint64_t> blocksProcessed { 0 };

    std::thread audioThread ([&]
    {
        while (! stop.load (std::memory_order_relaxed))
        {
            auto* plan = swapper.getCurrentPlanForAudioThread();
            if (plan == nullptr)
                continue;

            plan->process (maxBlockSize);

            const auto* data = plan->blockBuffers[(size_t) plan->finalOutputBufferIndex].getBlock().getChannelPointer (0);
            const auto first = data[0];

            for (int i = 1; i < maxBlockSize; ++i)
            {
                if (data[i] != first)
                {
                    discontinuityWithinBlock.store (true, std::memory_order_relaxed);
                    break;
                }
            }

            blocksProcessed.fetch_add (1, std::memory_order_relaxed);
        }
    });

    std::thread compilerThread ([&]
    {
        for (uint64_t generation = 1; generation <= 2000; ++generation)
        {
            while (! swapper.publish (makeDcPlan (generation, maxBlockSize)))
                swapper.reclaim();

            swapper.reclaim();
        }
    });

    compilerThread.join();
    stop.store (true, std::memory_order_relaxed);
    audioThread.join();

    CHECK_FALSE (discontinuityWithinBlock.load());
    CHECK (blocksProcessed.load() > 0);
}
