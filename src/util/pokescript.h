#pragma once
#include <QString>
#include <QList>

// Decompile all scripts reachable from a map's script table and event pointers.
//
//   romPath        – path to the GBA ROM file (g_loadedROM)
//   mapName        – export name for this map (e.g. "PalletTown_0_0")
//   mapScriptsOff  – ROM file offset of the map scripts table (g_mapLevelScripts); pass -1 if none
//   eventScripts   – ROM file offsets of event script pointers (value already has 0x8000000
//                    subtracted, i.e. direct file offsets); invalid values are skipped
//   isFR           – true for FireRed/LeafGreen ROMs (different opcode table from Emerald)
//
// Returns the complete content of scripts.inc (empty if nothing found).
QString decompileMapScripts(
    const QString& romPath,
    const QString& mapName,
    int mapScriptsOff,
    const QList<int>& eventScripts,
    bool isFR = false
);
