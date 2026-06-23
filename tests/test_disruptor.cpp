#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include "disruptor.hpp"

using namespace disruptor;

struct Event {
    int64_t id;
    double price;
};

TEST(DisruptorCorrectness, MaintainsFifoOrder) {
    Disruptor<Event, 4> pipeline;

    for (int64_t i = 0; i < 3; ++i) {
        int64_t seq = pipeline.claim();
        pipeline.getBySequence(seq).id = i;
        pipeline.publish(seq);
    }

    for (int64_t i = 0; i < 3; ++i) {
        int64_t available = pipeline.waitFor(i);
        EXPECT_GE(available, i);
        EXPECT_EQ(pipeline.getBySequence(i).id, i);
        pipeline.markConsumed(i);
    }
}

TEST(DisruptorCorrectness, HandlesBufferWraparound) {
    Disruptor<Event, 4> pipeline;

    for (int64_t i = 0; i < 6; ++i) {
        [[maybe_unused]] int64_t seq = pipeline.claim();
        pipeline.getBySequence(seq).id = i;
        pipeline.publish(seq);

        int64_t available = pipeline.waitFor(seq);
        EXPECT_GE(available, seq);
        pipeline.markConsumed(seq);
    }

    EXPECT_EQ(pipeline.getBySequence(5).id, 5);
}

TEST(DisruptorCorrectness, ProducerBlocksWhenFull) {
    Disruptor<Event, 4> pipeline;

    for (int i = 0; i < 4; ++i) {
        int64_t seq = pipeline.claim();
        pipeline.publish(seq);
    }

    std::atomic<bool> claimed{false};
    std::jthread producer([&pipeline, &claimed]() {
        int64_t available = pipeline.claim();
        EXPECT_GE(available, 0);
        claimed = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_FALSE(claimed.load());

    pipeline.markConsumed(0);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(claimed.load());
}

TEST(DisruptorTest, ConcurrentStressTestProcessesAllEventsWithoutDataLoss) {
    constexpr int64_t TOTAL_EVENTS = 10'000'000;

    constexpr int64_t EXPECTED_EVENT_ID_SUM = (TOTAL_EVENTS * (TOTAL_EVENTS - 1)) / 2;

    disruptor::Disruptor<Event, 65536> pipeline;
    std::atomic<int64_t> actual_id_sum{0};

    std::jthread consumer([&pipeline, &actual_id_sum]() {
        int64_t nextRead = 0;
        int64_t local_sum = 0;

        while (nextRead < TOTAL_EVENTS) {
            int64_t available = pipeline.waitFor(nextRead);

            while (nextRead <= available && nextRead < TOTAL_EVENTS) {
                Event& event = pipeline.getBySequence(nextRead);
                local_sum += event.id;
                nextRead++;
            }

            pipeline.markConsumed(nextRead - 1);
        }

        actual_id_sum.store(local_sum, std::memory_order_release);
    });

    for (int64_t i = 0; i < TOTAL_EVENTS; ++i) {
        int64_t seq = pipeline.claim();

        Event& event = pipeline.getBySequence(seq);
        event.id = i;
        event.price = 100.0 + (i % 10);

        pipeline.publish(seq);
    }

    if (consumer.joinable()) {
        consumer.join();
    }

    EXPECT_EQ(actual_id_sum.load(std::memory_order_acquire), EXPECTED_EVENT_ID_SUM)
        << "The consumer missed events or read corrupted data during the stress test.";
}
