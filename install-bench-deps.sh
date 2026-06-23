#!/usr/bin/env bash
set -e

THIRD_PARTY_DIR="third_party/benchmark"

if [ ! -d "$THIRD_PARTY_DIR" ]; then
    echo "Cloning Google Benchmark..."
    git clone -c advice.detachedHead=false --depth 1 --branch v1.9.5 https://github.com/google/benchmark.git "$THIRD_PARTY_DIR"

    echo "Building Google Benchmark with Zig..."
    export CC="zig cc"
    export CXX="zig c++"

    cmake -B "$THIRD_PARTY_DIR/build" -S "$THIRD_PARTY_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBENCHMARK_ENABLE_TESTING=OFF \
        -DBENCHMARK_ENABLE_GTEST_TESTS=OFF
    cmake --build "$THIRD_PARTY_DIR/build"
fi
