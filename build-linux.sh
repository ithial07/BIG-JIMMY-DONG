#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
echo "== ArdaBestClient Linux build =="
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"
echo "Built: build-linux/ardabest_client"
