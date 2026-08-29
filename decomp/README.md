# Native decompilation / reverse-engineering workspace

This directory is for semantic C/C++ recovery notes and generated disassembly references. The shipping PC runtime does **not** execute ARM instructions.

v0.6 adds a higher-level recovered data layer in `src/assets/overworld_data.*`: the original 540-entry `MapHeader` table is located in the BLZ-decoded ARM9 image and converted into native `HgMapHeader` records. The native world then follows those records into matrix, area, land, event, script and message archives.

`arm9_boot.c` is an early manual boot-path reconstruction. To generate broader ARM/Thumb reference disassembly from your own ROM extraction:

```bash
python3 tools/disassemble.py assets decomp/generated
```

Generated ROM-derived binaries/disassembly are intentionally not shipped in the release archive.
