#include <benchmark/benchmark.h>
#include <cstdint>
#include <thread>
#include "disruptor.hpp"

struct alignas(disruptor::CACHE_LINE_SIZE) Event {
    int64_t id;
};

static void BM_Disruptor_BatchThroughput(benchmark::State& state) {
    const int64_t itemsToProcess = state.range(0);
    disruptor::Disruptor<Event, 65536> pipeline;

    std::jthread consumer([&pipeline]() {
        int64_t nextRead = 0;
        while (true) {
            int64_t available = pipeline.waitFor(nextRead);

            while (nextRead <= available) {
                Event& event = pipeline.getBySequence(nextRead);
                benchmark::DoNotOptimize(event.id);

                if (event.id == -1) {
                    pipeline.markConsumed(nextRead); 
                    return; 
                }
                nextRead++;
            }
            pipeline.markConsumed(nextRead - 1);
        }
    });

    for (auto _ : state) {
        for (int64_t i = 0; i < itemsToProcess; ++i) {
            int64_t seq = pipeline.claim();
            Event& event = pipeline.getBySequence(seq);
            event.id = i;
            pipeline.publish(seq);
        }
    }

    int64_t seq = pipeline.claim();
    pipeline.getBySequence(seq).id = -1;
    pipeline.publish(seq);

    state.SetItemsProcessed(state.iterations() * itemsToProcess);
}

BENCHMARK(BM_Disruptor_BatchThroughput)
    ->RangeMultiplier(10)
    ->Range(100, 10000)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

BENCHMARK_MAIN();