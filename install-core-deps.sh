#!/usr/bin/env bash
set -e

SOCKETS_DIR="build/sockets"
mkdir -p "$SOCKETS_DIR"

if [ ! -d "$SOCKETS_DIR/uWebSockets" ]; then
    echo "Cloning uWebSockets..."
    git clone -c advice.detachedHead=false --depth 1 --branch v20.67.0 https://github.com/uNetworking/uWebSockets.git "$SOCKETS_DIR/uWebSockets"
fi

if [ ! -d "$SOCKETS_DIR/uSockets" ]; then
    echo "Cloning uSockets..."
    git clone -c advice.detachedHead=false --depth 1 --branch v0.8.8 https://github.com/uNetworking/uSockets.git "$SOCKETS_DIR/uSockets"
fi

echo "Building uSockets with Zig..."
cd "$SOCKETS_DIR/uSockets"
make clean
make CC="zig cc" CXX="zig c++" AR="zig ar" WITH_LTO=0
