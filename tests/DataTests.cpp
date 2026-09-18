#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/Data.h"
#include "bazalt/engine/RtAllocationTrap.h"
#include <atomic>
#include <thread>

using namespace bazalt::engine;

namespace
{
    // A buffer where every element equals `generation` — the simplest
    // content that makes "did I read a torn/partial buffer" trivially
    // checkable: every element must agree, always, on any single read.
    std::unique_ptr<DataBuffer> makeConstantModalSet (float generation, int numModes = 8)
    {
        std::vector<float> values;
        values.reserve ((size_t) numModes * 3);
        for (int i = 0; i < numModes; ++i)
        {
            values.push_back (generation); // frequency
            values.push_back (generation); // gain
            values.push_back (generation); // decay
        }
        return std::make_unique<DataBuffer> (DataTag::ModalSet, std::move (values), 3);
    }
}

TEST_CASE ("DataBuffer exposes its tag, stride, and element count correctly", "[engine][Data]")
{
    const auto buffer = makeConstantModalSet (1.0f, 4);
    CHECK (buffer->tag() == DataTag::ModalSet);
    CHECK (buffer->stride() == 3);
    CHECK (buffer->length() == 4);      // 12 floats / stride 3
    CHECK (buffer->rawSize() == 12);
    CHECK (buffer->at (0, 0) == 1.0f);  // mode 0's frequency
    CHECK (buffer->at (3, 2) == 1.0f);  // mode 3's decay
}

TEST_CASE ("DataPublisher publishes and serves buffers in order, allocation-free on the read side",
           "[engine][Data][DataPublisher]")
{
    DataPublisher publisher;
    CHECK (publisher.getCurrentForAudioThread() == nullptr); // nothing published yet

    // "Built on a worker thread" — DataPublisher doesn't care which thread
    // calls publish(), only that it's never the audio thread; a real
    // producer node would do this construction off-thread too.
    REQUIRE (publisher.publish (makeConstantModalSet (1.0f)));

    const DataBuffer* buffer = nullptr;
    {
        ScopedAudioThreadAllocationTrap trap; // throws if the read below allocates
        buffer = publisher.getCurrentForAudioThread();
    }

    REQUIRE (buffer != nullptr);
    CHECK (buffer->tag() == DataTag::ModalSet);
    CHECK (buffer->at (0, 0) == 1.0f);

    REQUIRE (publisher.publish (makeConstantModalSet (2.0f)));
    const DataBuffer* second = publisher.getCurrentForAudioThread();
    REQUIRE (second != nullptr);
    CHECK (second->at (0, 0) == 2.0f);
}

TEST_CASE ("DataPublisher::reclaim frees superseded slots but keeps whatever the audio thread last touched",
           "[engine][Data][DataPublisher]")
{
    DataPublisher publisher;

    REQUIRE (publisher.publish (makeConstantModalSet (1.0f)));
    publisher.getCurrentForAudioThread(); // epoch = generation 1

    REQUIRE (publisher.publish (makeConstantModalSet (2.0f)));
    publisher.reclaim();
    CHECK (publisher.getNumOccupiedSlots() == 2); // audio thread hasn't seen gen 2 yet

    publisher.getCurrentForAudioThread(); // epoch advances to generation 2
    publisher.reclaim();
    CHECK (publisher.getNumOccupiedSlots() == 1); // generation 1 now safe to free
}

TEST_CASE ("DataPublisher::publish fails once every slot is occupied and unreclaimed",
           "[engine][Data][DataPublisher]")
{
    DataPublisher publisher;

    for (int i = 1; i <= DataPublisher::numSlots; ++i)
        REQUIRE (publisher.publish (makeConstantModalSet ((float) i)));

    CHECK_FALSE (publisher.publish (makeConstantModalSet (99.0f)));
}

TEST_CASE ("Concurrent republish: the single audio-thread reader never sees a torn buffer or a freed one",
           "[engine][Data][DataPublisher][swap-under-load]")
{
    // Mirrors PlanSwapperTests.cpp's own swap-under-load test exactly:
    // DataPublisher is single-writer/single-reader, same as PlanSwapper and
    // for the same reason (see DataPublisher's own class comment on why an
    // earlier draft's "many concurrent readers" claim was actually unsound
    // — this test is what caught it, via a real crash, not a flaky
    // assertion, when it briefly used several reader threads).
    DataPublisher publisher;
    REQUIRE (publisher.publish (makeConstantModalSet (0.0f)));

    std::atomic<bool> stop { false };
    std::atomic<bool> tornOrFreedReadObserved { false };
    std::atomic<uint64_t> readsCompleted { 0 };

    std::thread audioThread ([&]
    {
        while (! stop.load (std::memory_order_relaxed))
        {
            const auto* buffer = publisher.getCurrentForAudioThread();
            if (buffer == nullptr)
            {
                tornOrFreedReadObserved.store (true, std::memory_order_relaxed);
                break;
            }

            const auto expected = buffer->at (0, 0);
            for (int i = 0; i < buffer->length(); ++i)
            {
                if (buffer->at (i, 0) != expected || buffer->at (i, 1) != expected || buffer->at (i, 2) != expected)
                {
                    tornOrFreedReadObserved.store (true, std::memory_order_relaxed);
                    break;
                }
            }

            readsCompleted.fetch_add (1, std::memory_order_relaxed);
        }
    });

    std::thread publisherThread ([&]
    {
        for (int generation = 1; generation <= 2000; ++generation)
        {
            while (! publisher.publish (makeConstantModalSet ((float) generation)))
                publisher.reclaim();

            publisher.reclaim();
        }
    });

    publisherThread.join();
    stop.store (true, std::memory_order_relaxed);
    audioThread.join();

    CHECK_FALSE (tornOrFreedReadObserved.load());
    CHECK (readsCompleted.load() > 0);
}

TEST_CASE ("dataTagAccepted matches exactly, with no wildcard for Unknown", "[engine][Data][dataTagAccepted]")
{
    CHECK (dataTagAccepted (DataTag::ModalSet, DataTag::ModalSet));
    CHECK_FALSE (dataTagAccepted (DataTag::Scale, DataTag::ModalSet)); // wiring a scale into a Modal Bank
    CHECK_FALSE (dataTagAccepted (DataTag::Unknown, DataTag::ModalSet));
    CHECK_FALSE (dataTagAccepted (DataTag::ModalSet, DataTag::Unknown));
    CHECK_FALSE (dataTagAccepted (DataTag::Unknown, DataTag::Unknown)); // no self-wildcard either
}
