#include <benchmark/benchmark.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include "disruptor.hpp"

struct alignas(disruptor::CACHE_LINE_SIZE) Event {
    int64_t id{0};
};

template <typename WaitStrategy>
static void BM_Disruptor_SustainedStream(benchmark::State& state) {
    constexpr size_t BufferSize = 65536;
    disruptor::Disruptor<Event, BufferSize, WaitStrategy> pipeline;

    std::jthread consumer([&pipeline]() {
        int64_t nextRead = 0;
        bool keepRunning = true;

        while (keepRunning) {
            int64_t available = pipeline.waitFor(nextRead);

            while (nextRead <= available) {
                Event& event = pipeline.getBySequence(nextRead);

                if (event.id == -1) {
                    keepRunning = false;
                    break;
                }

                benchmark::DoNotOptimize(event.id);
                nextRead++;
            }
            pipeline.markConsumed(nextRead - 1);
        }
    });

    int64_t currentSequence = 0;
    for (auto _ : state) {
        int64_t seq = pipeline.claim();
        Event& event = pipeline.getBySequence(seq);
        event.id = currentSequence++;
        pipeline.publish(seq);
    }

    int64_t shutdownSequence = pipeline.claim();
    pipeline.getBySequence(shutdownSequence).id = -1;
    pipeline.publish(shutdownSequence);

    state.SetItemsProcessed(state.iterations());
}

template <typename WaitStrategy>
static void BM_Disruptor_BackpressureBursts(benchmark::State& state) {
    constexpr size_t BufferSize = 1024;
    disruptor::Disruptor<Event, BufferSize, WaitStrategy> pipeline;
    
    const int64_t burstSize = state.range(0);

    std::jthread consumer([&pipeline, burstSize]() {
        int64_t nextRead = 0;
        bool keepRunning = true;

        while (keepRunning) {
            int64_t available = pipeline.waitFor(nextRead);

            if (nextRead % burstSize == 0) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(50));
            }

            while (nextRead <= available) {
                Event& event = pipeline.getBySequence(nextRead);
                if (event.id == -1) {
                    keepRunning = false;
                    break;
                }
                benchmark::DoNotOptimize(event.id);
                nextRead++;
            }
            pipeline.markConsumed(nextRead - 1);
        }
    });

    for (auto _ : state) {
        for (int64_t i = 0; i < burstSize; ++i) {
            int64_t seq = pipeline.claim();
            pipeline.getBySequence(seq).id = i;
            pipeline.publish(seq);
        }
    }

    int64_t shutdownSequence = pipeline.claim();
    pipeline.getBySequence(shutdownSequence).id = -1;
    pipeline.publish(shutdownSequence);

    state.SetItemsProcessed(state.iterations() * burstSize);
}

BENCHMARK(BM_Disruptor_SustainedStream<disruptor::YieldingWaitStrategy>)
    ->Name("BM_Disruptor_SustainedStream/Yielding")
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime();

BENCHMARK(BM_Disruptor_SustainedStream<disruptor::BlockingWaitStrategy>)
    ->Name("BM_Disruptor_SustainedStream/Blocking")
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime();

BENCHMARK(BM_Disruptor_BackpressureBursts<disruptor::YieldingWaitStrategy>)
    ->Name("BM_Disruptor_BackpressureBursts/Yielding")
    ->Arg(500)
    ->Arg(5000)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

BENCHMARK(BM_Disruptor_BackpressureBursts<disruptor::BlockingWaitStrategy>)
    ->Name("BM_Disruptor_BackpressureBursts/Blocking")
    ->Arg(500)
    ->Arg(5000)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

BENCHMARK_MAIN();
