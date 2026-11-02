#!/usr/bin/env bash
# Pathfinder (external/modular-robotics) をビルドする。
# 必要なもの: cmake, nlohmann-json3-dev, libboost-filesystem-dev, libboost-system-dev
#
#   ./external/build_pathfinder.sh
#
# できたものは external/modular-robotics/build/Pathfinder (git 管理外)。

set -eu

SOURCE=$(cd "$(dirname "${BASH_SOURCE[0]}")/modular-robotics" && pwd)

cmake -S "$SOURCE" -B "$SOURCE/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$SOURCE/build" --target Pathfinder -j "$(nproc)"
