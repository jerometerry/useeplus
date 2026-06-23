#!/usr/bin/env bash
set -e

GTEST_DIR="build/googletest"

if [ ! -d "$GTEST_DIR" ]; then
    echo "Cloning GoogleTest..."
    git clone -c advice.detachedHead=false --depth 1 --branch v1.17.0 https://github.com/google/googletest.git "$GTEST_DIR"

    echo "Building GoogleTest with Zig..."
    export CC="zig cc"
    export CXX="zig c++"

    cmake -B "$GTEST_DIR/build" -S "$GTEST_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$GTEST_DIR/build"
fi
