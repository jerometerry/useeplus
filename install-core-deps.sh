#!/usr/bin/env bash
set -e

THIRD_PARTY_DIR="third_party"
mkdir -p "$THIRD_PARTY_DIR"

if [ ! -d "$THIRD_PARTY_DIR/uWebSockets" ]; then
    echo "Cloning uWebSockets..."
    git clone -c advice.detachedHead=false --depth 1 --branch v20.67.0 https://github.com/uNetworking/uWebSockets.git "$THIRD_PARTY_DIR/uWebSockets"
fi

if [ ! -d "$THIRD_PARTY_DIR/uSockets" ]; then
    echo "Cloning uSockets..."
    git clone -c advice.detachedHead=false --depth 1 --branch v0.8.8 https://github.com/uNetworking/uSockets.git "$THIRD_PARTY_DIR/uSockets"
fi

echo "Building uSockets with Zig..."
cd "$THIRD_PARTY_DIR/uSockets"
make clean
make CC="zig cc" CXX="zig c++" AR="zig ar" WITH_LTO=0
