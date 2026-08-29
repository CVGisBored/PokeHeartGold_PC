#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ROM="${1:?usage: prepare_rom.sh /path/to/heartgold.nds}"
python3 "$ROOT/tools/nds_extract.py" "$ROM" "$ROOT/assets"
echo "Extracted assets to $ROOT/assets"
echo "Optional full reference disassembly:"
echo "  python3 $ROOT/tools/disassemble.py $ROOT/assets $ROOT/decomp/generated"
