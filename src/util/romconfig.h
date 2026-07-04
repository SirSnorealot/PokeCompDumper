#pragma once
#include <QString>
#include <QStringList>
#include <QMap>

// Load the ROM config JSON file. Must be called before any romConfig* function.
// Replaces the previous INI-based approach; no Win32 API required.
bool loadRomConfig(const QString& jsonPath);

// Get a flat string value for the given game code section.
QString romConfigString(const QString& gameCode, const QString& key, const QString& def = {});

// Get a string array value for the given game code section.
QStringList romConfigStringList(const QString& gameCode, const QString& key);

// Get a string object/map value for the given game code section.
QMap<QString, QString> romConfigStringMap(const QString& gameCode, const QString& key);

// Get OriginalBankPointers[index] for the given game code (hex string, e.g. "307F60").
QString romBankPointer(const QString& gameCode, int index);

// Get NumberOfMapsInBank[index] for the given game code.
int romMapsInBank(const QString& gameCode, int index);

// Get TilesetTileCounts[tilesetHex] for the given game code.
// tilesetHex must be uppercase without "0x" prefix (e.g. "286D0C").
// Returns the value parsed from the hex string stored in the JSON.
int romTilesetTileCount(const QString& gameCode, const QString& tilesetHex);

// Look up a pokeemerald enum name from the "enums" section of config.json.
// enumType is one of: "music", "weather", "map_type", "battle_scene", "mapsec".
// Falls back to the "0" entry if the value is not found.
QString enumName(const QString& enumType, int value);

// Look up a game-specific enum name, falling back to the global enum table.
QString romEnumName(const QString& gameCode, const QString& enumType, int value);
