#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$BUILD" --target heartgold_native --clean-first -j"${HG_BUILD_JOBS:-2}"
cp -f "$BUILD/heartgold_native" "$ROOT/heartgold_native"
mkdir -p "$ROOT/bin"
cp -f "$BUILD/heartgold_native" "$ROOT/bin/heartgold_native-linux-x86_64"
echo "Built and installed: $ROOT/heartgold_native"
