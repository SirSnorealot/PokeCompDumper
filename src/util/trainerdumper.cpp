#include "trainerdumper.h"

#include "image.h"
#include "lz77.h"
#include "romconfig.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <array>

namespace
{
constexpr quint32 GbaRomBase = 0x08000000;
constexpr int SpriteEntrySize = 8;
constexpr int MinTrainerSprites = 20;
constexpr int MaxTrainerSprites = 200;
constexpr int DefaultSpriteWidth = 64;
constexpr int DefaultSpriteHeight = 64;

struct SpriteTable
{
    quint32 gfxOffset = 0;
    quint32 palOffset = 0;
    int count = 0;
    bool fromConfig = false;
};

quint32 readU32(const QByteArray& rom, int offset)
{
    if (offset < 0 || offset + 4 > rom.size())
        return 0;

    const auto b0 = static_cast<quint8>(rom[offset]);
    const auto b1 = static_cast<quint8>(rom[offset + 1]);
    const auto b2 = static_cast<quint8>(rom[offset + 2]);
    const auto b3 = static_cast<quint8>(rom[offset + 3]);
    return quint32(b0) | (quint32(b1) << 8) | (quint32(b2) << 16) | (quint32(b3) << 24);
}

quint16 readU16(const QByteArray& rom, int offset)
{
    if (offset < 0 || offset + 2 > rom.size())
        return 0;

    const auto b0 = static_cast<quint8>(rom[offset]);
    const auto b1 = static_cast<quint8>(rom[offset + 1]);
    return quint16(b0) | (quint16(b1) << 8);
}

bool romPointerToOffset(quint32 pointer, int romSize, quint32& offset)
{
    if (pointer < GbaRomBase)
        return false;

    quint32 value = pointer - GbaRomBase;
    if (value >= static_cast<quint32>(romSize))
        return false;

    offset = value;
    return true;
}

bool isLikelyLz77At(const QByteArray& rom, quint32 offset)
{
    if (offset + 4 > static_cast<quint32>(rom.size()))
        return false;

    if (static_cast<quint8>(rom[static_cast<int>(offset)]) != 0x10)
        return false;

    int size = static_cast<quint8>(rom[static_cast<int>(offset) + 1])
             | (static_cast<quint8>(rom[static_cast<int>(offset) + 2]) << 8)
             | (static_cast<quint8>(rom[static_cast<int>(offset) + 3]) << 16);
    return size >= 0x80 && size <= 0x8000;
}

bool isLikelySpriteEntry(const QByteArray& rom, int entryOffset)
{
    quint32 dataOffset = 0;
    if (!romPointerToOffset(readU32(rom, entryOffset), rom.size(), dataOffset))
        return false;

    if (!isLikelyLz77At(rom, dataOffset))
        return false;

    int declaredSize = readU16(rom, entryOffset + 4);
    return declaredSize >= 0x80 && declaredSize <= 0x4000 && declaredSize % 32 == 0;
}

int countSpriteEntries(const QByteArray& rom, quint32 tableOffset)
{
    int count = 0;
    while (tableOffset + static_cast<quint32>((count + 1) * SpriteEntrySize) <= static_cast<quint32>(rom.size()))
    {
        if (!isLikelySpriteEntry(rom, static_cast<int>(tableOffset) + count * SpriteEntrySize))
            break;

        ++count;
        if (count > MaxTrainerSprites)
            break;
    }

    return count;
}

QByteArray readPaletteData(const QByteArray& rom, quint32 dataOffset)
{
    if (dataOffset + 4 <= static_cast<quint32>(rom.size())
        && static_cast<quint8>(rom[static_cast<int>(dataOffset)]) == 0x10)
    {
        int size = static_cast<quint8>(rom[static_cast<int>(dataOffset) + 1])
                 | (static_cast<quint8>(rom[static_cast<int>(dataOffset) + 2]) << 8)
                 | (static_cast<quint8>(rom[static_cast<int>(dataOffset) + 3]) << 16);
        if (size >= 32 && size <= 0x400)
            return lz77DecompressBytes(rom.mid(static_cast<int>(dataOffset))).left(32);
    }

    if (dataOffset + 32 > static_cast<quint32>(rom.size()))
        return {};
    return rom.mid(static_cast<int>(dataOffset), 32);
}

bool paletteLooksUsable(const QByteArray& data)
{
    if (data.size() < 32)
        return false;

    bool anyNonZero = false;
    bool anyNonFf = false;
    for (int i = 0; i < 32; ++i)
    {
        quint8 byte = static_cast<quint8>(data[i]);
        anyNonZero = anyNonZero || byte != 0;
        anyNonFf = anyNonFf || byte != 0xFF;
    }
    return anyNonZero && anyNonFf;
}

bool isLikelyPaletteEntry(const QByteArray& rom, int entryOffset)
{
    quint32 dataOffset = 0;
    if (!romPointerToOffset(readU32(rom, entryOffset), rom.size(), dataOffset))
        return false;

    return paletteLooksUsable(readPaletteData(rom, dataOffset));
}

bool isLikelyPaletteTable(const QByteArray& rom, quint32 tableOffset, int count)
{
    if (count <= 0 || tableOffset + static_cast<quint32>(count * SpriteEntrySize) > static_cast<quint32>(rom.size()))
        return false;

    for (int i = 0; i < count; ++i)
    {
        if (!isLikelyPaletteEntry(rom, static_cast<int>(tableOffset) + i * SpriteEntrySize))
            return false;
    }
    return true;
}

quint32 findPaletteTable(const QByteArray& rom, quint32 afterOffset, int count)
{
    const quint32 searchEnd = qMin<quint32>(static_cast<quint32>(rom.size()), afterOffset + 0x8000);
    for (quint32 offset = afterOffset; offset + static_cast<quint32>(count * SpriteEntrySize) <= searchEnd; offset += 4)
    {
        if (isLikelyPaletteTable(rom, offset, count))
            return offset;
    }

    for (quint32 offset = 0; offset + static_cast<quint32>(count * SpriteEntrySize) <= static_cast<quint32>(rom.size()); offset += 4)
    {
        if (isLikelyPaletteTable(rom, offset, count))
            return offset;
    }

    return 0;
}

SpriteTable findTrainerSpriteTable(const QByteArray& rom)
{
    struct Candidate
    {
        quint32 offset = 0;
        int count = 0;
        int score = 0;
    };

    QVector<Candidate> candidates;
    for (quint32 offset = 0; offset + SpriteEntrySize <= static_cast<quint32>(rom.size()); offset += 4)
    {
        if (offset >= SpriteEntrySize && isLikelySpriteEntry(rom, static_cast<int>(offset - SpriteEntrySize)))
            continue;

        int count = countSpriteEntries(rom, offset);
        if (count < MinTrainerSprites || count > MaxTrainerSprites)
            continue;

        int common64 = 0;
        for (int i = 0; i < count; ++i)
        {
            if (readU16(rom, static_cast<int>(offset) + i * SpriteEntrySize + 4) == 0x800)
                ++common64;
        }

        int score = count + common64 * 2;
        if (count >= 50 && count <= 140)
            score += 200;

        candidates.append({offset, count, score});
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.score > b.score;
    });

    for (const Candidate& candidate : candidates)
    {
        quint32 paletteOffset = findPaletteTable(rom, candidate.offset + candidate.count * SpriteEntrySize, candidate.count);
        if (paletteOffset != 0)
            return {candidate.offset, paletteOffset, candidate.count, false};
    }

    return {};
}

QImage renderIndexedSprite(const QByteArray& tiles, const std::array<QColor, 16>& palette, int width, int height)
{
    QImage img = loadSprite(tiles, palette, width, height).convertToFormat(QImage::Format_ARGB32);
    const QRgb oldColor0 = palette[0].rgba();

    for (int y = 0; y < img.height(); ++y)
    {
        for (int x = 0; x < img.width(); ++x)
        {
            if (img.pixel(x, y) == oldColor0)
                img.setPixel(x, y, qRgba(palette[0].red(), palette[0].green(), palette[0].blue(), 0));
        }
    }
    return img;
}

bool parseConfiguredTable(const QByteArray& rom, const QString& gameCode, SpriteTable& table)
{
    bool gfxOk = false;
    bool palOk = false;
    bool countOk = false;
    quint32 gfxOffset = romConfigString(gameCode, "trainerFrontPicTableOffset").toUInt(&gfxOk, 16);
    quint32 palOffset = romConfigString(gameCode, "trainerFrontPaletteTableOffset").toUInt(&palOk, 16);
    int count = romConfigString(gameCode, "trainerSpriteCount").toInt(&countOk);

    if (!gfxOk || !palOk || !countOk || count <= 0 || count > MaxTrainerSprites)
        return false;

    if (countSpriteEntries(rom, gfxOffset) < count)
        return false;

    if (!isLikelyPaletteTable(rom, palOffset, count))
        return false;

    table = {gfxOffset, palOffset, count, true};
    return true;
}
}

TrainerSpriteDumpResult exportTrainerSprites(const QString& romPath,
                                             const QString& gameCode,
                                             const QString& outputFolder)
{
    TrainerSpriteDumpResult result;

    QFile file(romPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.error = "Could not open ROM file.";
        return result;
    }

    QByteArray rom = file.readAll();
    file.close();
    if (rom.isEmpty())
    {
        result.error = "ROM file is empty.";
        return result;
    }

    SpriteTable table;
    if (!parseConfiguredTable(rom, gameCode, table))
        table = findTrainerSpriteTable(rom);

    if (table.gfxOffset == 0 || table.palOffset == 0 || table.count <= 0)
    {
        result.error = "Could not locate trainer sprite tables. Add trainerFrontPicTableOffset, trainerFrontPaletteTableOffset, and trainerSpriteCount to config.json for this ROM.";
        return result;
    }

    const QString outDir = outputFolder + "/graphics/trainers";
    if (!QDir().mkpath(outDir))
    {
        result.error = "Could not create graphics/trainers output folder.";
        return result;
    }

    int exported = 0;
    for (int i = 0; i < table.count; ++i)
    {
        const int gfxEntry = static_cast<int>(table.gfxOffset) + i * SpriteEntrySize;
        const int palEntry = static_cast<int>(table.palOffset) + i * SpriteEntrySize;

        quint32 gfxDataOffset = 0;
        quint32 palDataOffset = 0;
        if (!romPointerToOffset(readU32(rom, gfxEntry), rom.size(), gfxDataOffset)
            || !romPointerToOffset(readU32(rom, palEntry), rom.size(), palDataOffset))
            continue;

        int declaredSize = readU16(rom, gfxEntry + 4);
        QByteArray tiles = lz77DecompressBytes(rom.mid(static_cast<int>(gfxDataOffset)));
        if (tiles.isEmpty())
            continue;

        if (declaredSize > 0 && tiles.size() > declaredSize)
            tiles = tiles.left(declaredSize);

        QByteArray paletteData = readPaletteData(rom, palDataOffset);
        if (paletteData.size() < 32)
            continue;

        int requiredBytes = DefaultSpriteWidth * DefaultSpriteHeight / 2;
        if (tiles.size() < requiredBytes)
            tiles.append(QByteArray(requiredBytes - tiles.size(), '\0'));
        else if (tiles.size() > requiredBytes)
            tiles = tiles.left(requiredBytes);

        auto palette = loadPalette(paletteData.left(32));
        QImage image = renderIndexedSprite(tiles, palette, DefaultSpriteWidth, DefaultSpriteHeight);

        const QString baseName = QString("trainer_%1").arg(i, 3, 10, QChar('0'));
        if (image.save(outDir + "/" + baseName + ".png", "PNG"))
        {
            QFile palFile(outDir + "/" + baseName + ".pal");
            if (palFile.open(QIODevice::WriteOnly | QIODevice::Text))
                palFile.write(colorToPalText(palette).toUtf8());
            ++exported;
        }
    }

    if (exported == 0)
    {
        result.error = "Trainer sprite tables were found, but no sprites could be exported.";
        return result;
    }

    result.ok = true;
    result.sprites = exported;
    result.outputDir = outDir;
    result.usedConfiguredOffsets = table.fromConfig;
    return result;
}
