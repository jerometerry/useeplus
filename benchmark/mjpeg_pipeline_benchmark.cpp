#include <benchmark/benchmark.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "constants.hpp"
#include "video_frame_fragment.hpp"
#include "video_frame_buffer.hpp"
#include "useeplus_video_stream.hpp"
#include "http_response_builder.hpp"

namespace {
    std::string fileName_ = "./test_data/camera_stream.mjpeg";
}

static std::vector<uint8_t> readBinaryFile(const std::string& fileName) {
    std::ifstream file(fileName, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: camera_stream.mjpeg");
    }

    std::streamsize size = file.tellg();
    std::vector<uint8_t> buffer = std::vector<uint8_t>(size);

    file.seekg(0, std::ios::beg);

    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw std::runtime_error("Error reading data from stream.");
    }

    return buffer;
}

static std::vector<std::span<const uint8_t>> splitPages(const std::vector<uint8_t>& data) {
    std::vector<std::span<const uint8_t>> packets;
    size_t chunkSize = Units::FOUR_KILOBYTES;
    auto reservationSize = (data.size() + chunkSize - 1) / chunkSize;
    packets.reserve(reservationSize);

    for (size_t i = 0; i < data.size(); i += chunkSize) {
        auto endIndex = std::min(i + chunkSize, data.size());
        packets.emplace_back(data.begin() + i, data.begin() + endIndex);
    }
    return packets;
}

static void BM_Pipeline_Throughput(benchmark::State& state) {
    static auto data = readBinaryFile(fileName_);
    static auto pages = splitPages(data);

    std::atomic<bool> producerRunning{true};

    VideoFrameBuffer ringBuffer;
    ringBuffer.preAllocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);

    std::jthread consumer([&ringBuffer, &producerRunning]() {
        int64_t nextRead = 0;

        while (producerRunning.load(std::memory_order_acquire)) {
            int64_t available = ringBuffer.waitFor(nextRead);

            while (nextRead <= available) {
                VideoFrameFragment& slot = ringBuffer.getBySequence(nextRead);

                if (slot.contentSize() > 0) {
                    HttpResponseBuilder::build(slot);
                    benchmark::DoNotOptimize(slot.contentSize());
                    benchmark::ClobberMemory();
                }
                nextRead++;
            }
            ringBuffer.markConsumed(nextRead - 1);
        }
    });

    UseeplusVideoStream stream(ringBuffer);

    size_t pageIndex = 0;
    for (auto _ : state) {
        const auto& page = pages[pageIndex % pages.size()];
        stream.send(page);
        pageIndex++;
    }

    producerRunning.store(false, std::memory_order_release);

    int64_t seq = ringBuffer.claim();
    ringBuffer.getBySequence(seq).clear();
    ringBuffer.publish(seq);

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * data.size() / pages.size());
}

static void BM_Pipeline_DiskBound(benchmark::State& state) {
    std::ifstream file(fileName_, std::ios::binary);
    if (!file.is_open()) state.SkipWithError("Could not open file.");

    std::atomic<bool> producerRunning{true};

    VideoFrameBuffer ringBuffer;
    ringBuffer.preAllocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);

    std::jthread consumer([&ringBuffer, &producerRunning]() {
        int64_t nextRead = 0;
        while (producerRunning.load(std::memory_order_acquire)) {
            int64_t available = ringBuffer.waitFor(nextRead);

            while (nextRead <= available) {
                VideoFrameFragment& slot = ringBuffer.getBySequence(nextRead);

                if (slot.contentSize() > 0) {
                    HttpResponseBuilder::build(slot);
                    benchmark::DoNotOptimize(slot.contentSize());
                    benchmark::ClobberMemory();
                }
                nextRead++;
            }
            ringBuffer.markConsumed(nextRead - 1);
        }
    });

    UseeplusVideoStream stream(ringBuffer);
    std::vector<uint8_t> chunk(4096);

    for (auto _ : state) {
        file.read(reinterpret_cast<char*>(chunk.data()), chunk.size());
        std::streamsize bytes_read = file.gcount();

        if (bytes_read > 0) {
            stream.send(std::span<const uint8_t>(chunk.data(), bytes_read));
        }

        if (file.eof()) {
            file.clear();
            file.seekg(0, std::ios::beg);
        }
    }

    producerRunning.store(false, std::memory_order_release);
    int64_t seq = ringBuffer.claim();
    ringBuffer.getBySequence(seq).clear();
    ringBuffer.publish(seq);

    state.SetBytesProcessed(state.iterations() * 4096);
}

BENCHMARK(BM_Pipeline_Throughput)
    ->Unit(benchmark::kMillisecond)
    ->Threads(1)
    ->Threads(4)
    ->Threads(10);

BENCHMARK(BM_Pipeline_DiskBound)
    ->Unit(benchmark::kMillisecond)
    ->Threads(1)
    ->Threads(4)
    ->Threads(10);

int main(int argc, char* argv[]) {
    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "--file" && i + 1 < argc) {
                fileName_ = argv[++i];
            }
        }

        benchmark::Initialize(&argc, argv);
        benchmark::RunSpecifiedBenchmarks();
        benchmark::Shutdown();
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unhandled exception in application core: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}