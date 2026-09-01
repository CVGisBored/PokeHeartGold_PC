#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ $# -ge 1 && "${1:-}" != --* ]]; then
  "$ROOT/scripts/prepare_rom.sh" "$1"
  shift
fi
cd "$ROOT"
exec "$ROOT/heartgold_native" --assets "$ROOT/assets/nitrofs" "$@"
