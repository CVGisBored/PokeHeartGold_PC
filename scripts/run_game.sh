#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Optional first argument: ROM path. Remaining arguments are forwarded to the native ROM-world executable.
if [[ $# -ge 1 && "${1:-}" != --* ]]; then
  "$ROOT/scripts/prepare_rom.sh" "$1"
  shift
fi

"$ROOT/scripts/build_linux.sh"
cd "$ROOT"
exec "$ROOT/build/heartgold_native" --assets "$ROOT/assets/nitrofs" "$@"
