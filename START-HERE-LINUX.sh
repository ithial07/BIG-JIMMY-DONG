#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

echo "============================================================"
echo "  ArdaBestClient Linux Setup + Build"
echo "============================================================"
echo
echo "This builds the Qt 6 / C++23 client on Linux."
echo "It will try to install build tools on apt-based Linux if they are missing."
echo

need_install=0
for cmd in cmake g++; do
  if ! command -v "$cmd" >/dev/null 2>&1; then need_install=1; fi
done
if ! pkg-config --exists Qt6Widgets 2>/dev/null; then need_install=1; fi

if [ "$need_install" = "1" ]; then
  if command -v apt >/dev/null 2>&1; then
    echo "Some build tools or Qt 6 packages are missing."
    read -r -p "Use sudo apt to install them now? [y/N] " answer
    case "$answer" in
      y|Y|yes|YES)
        sudo apt update
        sudo apt install -y build-essential cmake ninja-build pkg-config qt6-base-dev qt6-base-dev-tools
        ;;
      *)
        echo "Install these manually, then run this again:"
        echo "  build-essential cmake ninja-build pkg-config qt6-base-dev qt6-base-dev-tools"
        exit 1
        ;;
    esac
  else
    echo "Missing build tools or Qt 6. Install your distro's Qt 6 Widgets/Network dev packages, CMake, and a C++ compiler."
    exit 1
  fi
fi

echo
echo "[1/2] Configuring..."
if command -v ninja >/dev/null 2>&1; then
  cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
else
  cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
fi

echo
echo "[2/2] Building..."
cmake --build build-linux -j"$(nproc 2>/dev/null || echo 2)"

echo
echo "============================================================"
echo "SUCCESS!"
echo "Run the client with:"
echo "  ./build-linux/ardabest_client"
echo "============================================================"
echo
read -r -p "Press Enter to close..." _
