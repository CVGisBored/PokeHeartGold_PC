# Pokémon HeartGold Native Port v0.42 — Asset-Free Distribution

This is the **distribution-only** v0.42 package. It is a separate copy of the development tree and intentionally excludes the original game's ROM-derived payloads.

## What is included

- Native C/C++ engine source
- Linux native executable from the v0.42 build
- CMake/build scripts
- Nitro/NARC/NSBMD/Nitro2D/SDAT loaders and other runtime format support
- ROM extraction tooling
- Tests, implementation notes, and v0.42 changes

## What is intentionally NOT included

- Nintendo DS ROM images
- NitroFS game files
- ARM9/ARM7 ROM binaries
- ROM overlays
- Original music or sound-effect data
- Original maps, models, textures, sprites, animation archives, message archives, or other ROM-extracted game assets

The `assets/` directory contains only a short setup note in this distribution.

## Supplying your own game data

You must provide your own legally obtained Pokémon HeartGold Nintendo DS ROM. From the package root:

```bash
./scripts/prepare_rom.sh "/path/to/your/Pokemon HeartGold.nds"
```

This locally creates the required `assets/` tree from your ROM.

You can also build and launch in one step:

```bash
./scripts/run_game.sh "/path/to/your/Pokemon HeartGold.nds"
```

Once assets have been prepared, future runs can use:

```bash
./scripts/run_game.sh
```

## Important distribution note

Do **not** repackage the locally generated `assets/nitrofs`, `assets/bin`, or `assets/overlays` directories into this asset-free archive unless you have permission to distribute that content. This package was specifically prepared so those original game payloads are absent.

## v0.42 code state

This package retains the v0.42 engine changes, including the opening-animation timing work, directional ledge jumping, F3 walk-through-walls debug control, dialogue spacing adjustments, door hold timing, trainer throw anchoring, and the earlier v0.41 performance/service-counter/battle-alignment work.
