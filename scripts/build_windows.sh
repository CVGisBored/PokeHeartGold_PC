#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-windows"
OUT="$ROOT/bin"

if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 && ! command -v x86_64-w64-mingw32-clang++ >/dev/null 2>&1; then
  echo "ERROR: Windows cross compiler not found."
  echo "Install MinGW-w64 or llvm-mingw so x86_64-w64-mingw32-g++ or x86_64-w64-mingw32-clang++ is in PATH."
  exit 2
fi

rm -rf "$BUILD"
cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/mingw64.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF
cmake --build "$BUILD" --config Release -j"$(nproc)"
mkdir -p "$OUT"
cp "$BUILD/heartgold_native.exe" "$OUT/HeartGoldNative-v0.37-Windows-x86_64.exe"
cp "$BUILD/heartgold_native.exe" "$ROOT/HeartGoldNative-v0.37-Windows-x86_64.exe"
file "$OUT/HeartGoldNative-v0.37-Windows-x86_64.exe" || true
printf '\nBuilt: %s\n' "$ROOT/HeartGoldNative-v0.37-Windows-x86_64.exe"
printf 'Mirror: %s\n' "$OUT/HeartGoldNative-v0.37-Windows-x86_64.exe"
