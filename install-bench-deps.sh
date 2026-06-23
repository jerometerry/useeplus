#!/usr/bin/env bash
set -e

BENCHMARK_DIR="build/benchmark"

if [ ! -d "$BENCHMARK_DIR" ]; then
    echo "Cloning Google Benchmark..."
    git clone -c advice.detachedHead=false --depth 1 --branch v1.9.5 https://github.com/google/benchmark.git "$BENCHMARK_DIR"

    echo "Building Google Benchmark with Zig..."
    export CC="zig cc"
    export CXX="zig c++"

    cmake -B "$BENCHMARK_DIR/build" -S "$BENCHMARK_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBENCHMARK_ENABLE_TESTING=OFF \
        -DBENCHMARK_ENABLE_GTEST_TESTS=OFF
    cmake --build "$BENCHMARK_DIR/build"
fi
