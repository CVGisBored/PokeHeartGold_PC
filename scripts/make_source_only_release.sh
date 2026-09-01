#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="v0.46.5"
NAME="PokemonHeartGold_NativePort_${VERSION}_SOURCE_ONLY"
OUT_BASE="${1:-$ROOT/dist}"
OUT_TREE="$OUT_BASE/$NAME"
OUT_ZIP="$OUT_BASE/$NAME.zip"

rm -rf "$OUT_TREE" "$OUT_ZIP"
mkdir -p "$OUT_TREE" "$OUT_BASE"

# Whitelist only project-authored source/build material.  Do not copy the
# top-level assets/, decomp/, bin/, build/, mystery_gift_server/, executables,
# ROM extracts, screenshots, or historical binary/release artifacts.
cp "$ROOT/CMakeLists.txt" "$OUT_TREE/"
cp -a "$ROOT/src" "$OUT_TREE/src"
cp -a "$ROOT/cmake" "$OUT_TREE/cmake"
cp -a "$ROOT/tests" "$OUT_TREE/tests"
cp -a "$ROOT/tools" "$OUT_TREE/tools"
mkdir -p "$OUT_TREE/scripts"
for f in build_linux.sh prepare_rom.sh run_game.sh run_demo.sh make_source_only_release.sh; do
    cp "$ROOT/scripts/$f" "$OUT_TREE/scripts/$f"
done
chmod +x "$OUT_TREE/scripts/"*.sh

cat > "$OUT_TREE/README.md" <<'README'
# HG/SS Native PC Port v0.46.5 — source-only package

This package intentionally contains **source code and build/extraction tooling only**.
It does **not** contain a Pokémon HeartGold/SoulSilver ROM, extracted NitroFS files,
Nintendo/Game Freak art, music, models, maps, text, or other original game assets.

## Build

Linux prerequisites include a C++20 compiler, CMake, pkg-config, Vulkan development
files/runtime, and XCB development files.

```bash
./scripts/build_linux.sh
```

The executable needs game data supplied locally by the user. To extract data from a
legally obtained compatible ROM into a local `assets/` directory:

```bash
./scripts/prepare_rom.sh /path/to/your/HeartGold.nds
```

Then run:

```bash
./scripts/run_game.sh
```

Do not commit or redistribute the generated `assets/` directory.
README

cat > "$OUT_TREE/ATTRIBUTION.md" <<'ATTRIBUTION'
# Attribution and asset notice

This is a source-only distribution of the independent **HG/SS Native PC Port** project,
generated from version v0.46.5.

Pokémon, Pokémon HeartGold and Pokémon SoulSilver, and related names, characters,
artwork, music, game data, and trademarks are owned by their respective rights holders.
This project is not affiliated with or endorsed by Nintendo, Creatures, or GAME FREAK.

No original ROM or extracted original-game assets are included in this package. Users
must supply any required game data themselves from a copy they are legally entitled to
use. This notice is an attribution/asset notice only; it does not grant a license to
third-party game content.
ATTRIBUTION

cat > "$OUT_TREE/.gitignore" <<'GITIGNORE'
/build/
/assets/
/decomp/
/bin/
heartgold_native
heartgold_native.exe
*.nds
*.sav
*.zip
GITIGNORE

# Safety audit: the source-only tree may contain only source/text/script files.
# This deliberately prevents accidental ROM payloads from being added later even if
# new directories are created in the development tree.
BAD="$(find "$OUT_TREE" -type f ! \( \
    -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.py' -o \
    -name '*.sh' -o -name '*.cmake' -o -name '*.md' -o -name '*.txt' -o \
    -name '.gitignore' -o -name 'CMakeLists.txt' \
\) -print)"
if [[ -n "$BAD" ]]; then
    echo "ERROR: unexpected non-source files in source-only tree:" >&2
    printf '%s\n' "$BAD" >&2
    exit 2
fi

# Explicitly reject common Nintendo DS / extracted asset payload extensions.
if find "$OUT_TREE" -type f \( \
    -iname '*.nds' -o -iname '*.narc' -o -iname '*.nsbmd' -o -iname '*.nsbtx' -o \
    -iname '*.nsbca' -o -iname '*.nsbta' -o -iname '*.nsbma' -o -iname '*.nsbtp' -o \
    -iname '*.ncgr' -o -iname '*.nclr' -o -iname '*.ncer' -o -iname '*.nanr' -o \
    -iname '*.nscr' -o -iname '*.sdat' -o -iname '*.bin' -o -iname '*.dat' \
\) -print -quit | grep -q .; then
    echo "ERROR: ROM-derived asset payload detected; refusing to package." >&2
    exit 3
fi

(
    cd "$OUT_BASE"
    zip -qr "$(basename "$OUT_ZIP")" "$(basename "$OUT_TREE")"
)

echo "Created source-only tree: $OUT_TREE"
echo "Created source-only ZIP:  $OUT_ZIP"
sha256sum "$OUT_ZIP"
