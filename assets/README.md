# Runtime assets are not included

This distribution intentionally contains **no Pokémon HeartGold ROM data or extracted Nintendo assets**.

To use the native engine, provide your own legally obtained Pokémon HeartGold Nintendo DS ROM and extract its runtime data locally:

```bash
./scripts/prepare_rom.sh "/path/to/your/Pokemon HeartGold.nds"
```

That command creates `assets/nitrofs`, `assets/bin`, and `assets/overlays` on your own machine. Those generated files are not part of this distribution and should not be redistributed with this package unless you have the rights to do so.

After extraction, build/run normally:

```bash
./scripts/run_game.sh --no-controller
```

Or do both in one command:

```bash
./scripts/run_game.sh "/path/to/your/Pokemon HeartGold.nds" --no-controller
```
