# PokeCompDumper

A Windows GUI tool for extracting map data from GBA Pokémon ROMs into a format compatible with the [pokeemerald](https://github.com/pret/pokeemerald) / [pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion) decompilation projects. Original concept was made by hand and code is expanded on and improved with github copilot/Codex.

---

## Supported ROMs

| Game | Code |
|---|---|
| Pokémon Ruby (English) | AXVE |
| Pokémon Sapphire (English) | AXPE |
| Pokémon FireRed (English) | BPRE |
| Pokémon LeafGreen (English) | BPGE |
| Pokémon Emerald (English) | BPEE |

ROM hacks based on these games are supported, but the offsets in `config.json` (e.g. `mapBankTableOffset`, `mapLabelsOffset`, `bankPointers`) will likely need to be updated to match the hack's relocated data.

---

## Features

- **Map preview** — browse all maps in the ROM with a live rendered tile preview
- **Tileset preview** — inspect primary and secondary tile sheets and palettes
- **Export Map** — export a single selected map and all its associated data
- **Export All** — export every map in the ROM in one pass. Dumps all events and scripts.
- **Export World Image** — render a stitched PNG of a map and all its connected neighbours
- **Export Music** — (Experimental- Pretty sure this doesn't work correctly.)

---

## Building

### Requirements

- Windows 10/11
- [Qt 6](https://www.qt.io/) (tested with 6.11.0, static MSVC build)
- Visual Studio 2022 Build Tools (MSVC + CMake + Ninja)

### Build steps

```
cmake -B build/debug -DCMAKE_PREFIX_PATH=<path/to/Qt6> -DCMAKE_BUILD_TYPE=Debug -G Ninja
cmake --build build/debug
```

---

## Usage

1. Launch `PokeCompDumper.exe`. `config.json` must be in the same directory.
2. Click **Load ROM** and open a supported `.gba` file.
3. The map tree on the left will populate with all banks and maps.
4. Click a map to preview its tiles, blocks, and rendered layout.
5. Use one of the three export buttons:
   - **Export Map** — exports only the currently selected map.
   - **Export All** — exports every map in the ROM (can take a while).
   - **Export World Image** — saves a PNG of the selected map stitched together with all connected maps.
   - **Export Music** — writes decomp-style `sound/songs/midi/*.mid`, `midi.cfg`, `song_table.inc`, `voice_groups.inc`, `sound/voicegroups/*.inc`, `direct_sound_data.inc` with `.wav` direct sound sources, `programmable_wave_data.inc` with 16-byte `.pcm` wave samples, and key split tables when referenced.

---

## config.json

ROM-specific settings live in `config.json` next to the executable. Each entry is keyed by the 4-character game code. Relevant fields:

| Key | Description |
|---|---|
| `mapBankTableOffset` | ROM offset of the map bank pointer table (hex) |
| `mapLabelsOffset` | ROM offset of the map label index table (hex) |
| `mapLabelCount` | Number of map labels |
| `bankPointers` | Pointer for each bank (used to detect bank boundaries) |
| `mapsPerBank` | Number of maps in each bank |
| `tilesetSizes` | Metatile counts per tileset address, keyed by hex address |
| `songTableOffset` | ROM offset of `gSongTable` for music exporting (hex) |
| `songCount` | Number of 8-byte song table entries to dump |
| `fastSmol` | `"true"` to emit `.4bpp.fastSmol` in `graphics.h` (expansion), `"false"` for `.4bpp.lz` (vanilla) |

---

## Notes

- **FireRed / LeafGreen**: tile data is reformatted from the FR split-palette layout into the standard Emerald layout during export.
- `headers.h`, `graphics.h`, and `metatiles.h` are **appended** on each export, so you can run Export All into a fresh folder and end up with one file containing all tilesets.
