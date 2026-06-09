#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
echo "== ArdaBestClient macOS build =="
QT_PREFIX="${QT_PREFIX:-}"
if [[ -z "$QT_PREFIX" ]] && command -v brew >/dev/null 2>&1; then
  QT_PREFIX="$(brew --prefix qt 2>/dev/null || true)"
fi
if [[ -n "$QT_PREFIX" ]]; then
  cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_PREFIX"
else
  cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release
fi
cmake --build build-macos -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 2)"
echo "Built: build-macos/ardabest_client"
