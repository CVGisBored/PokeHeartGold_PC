#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT/scripts/build_linux.sh"
"$ROOT/scripts/build_windows.sh"
