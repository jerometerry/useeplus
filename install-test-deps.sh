#!/usr/bin/env bash
set -e

THIRD_PARTY_DIR="third_party/googletest"

if [ ! -d "$THIRD_PARTY_DIR" ]; then
    echo "Cloning GoogleTest..."
    git clone -c advice.detachedHead=false --depth 1 --branch v1.17.0 https://github.com/google/googletest.git "$THIRD_PARTY_DIR"

    echo "Building GoogleTest with Zig..."
    export CC="zig cc"
    export CXX="zig c++"

    cmake -B "$THIRD_PARTY_DIR/build" -S "$THIRD_PARTY_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$THIRD_PARTY_DIR/build"
fi
