#include "overworlddumper.h"

#include "image.h"
#include "romconfig.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <array>

namespace
{
constexpr quint32 GbaRomBase = 0x08000000;
constexpr int GraphicsInfoSize = 0x24;
constexpr int SpriteFrameImageSize = 8;
constexpr int SpritePaletteSize = 8;
constexpr int MaxObjectEventGraphics = 400;
constexpr int MaxFramesPerGraphic = 64;

struct GraphicsInfo
{
    quint32 infoOffset = 0;
    quint16 tileTag = 0;
    quint16 paletteTag = 0;
    quint16 reflectionPaletteTag = 0;
    quint16 size = 0;
    qint16 width = 0;
    qint16 height = 0;
    quint8 paletteSlot = 0;
    quint8 shadowSize = 0;
    quint8 inanimate = 0;
    quint8 disableReflectionPaletteLoad = 0;
    quint8 tracks = 0;
    quint32 imagesOffset = 0;
};

struct FrameInfo
{
    quint32 dataOffset = 0;
    quint16 size = 0;
    QString fileName;
};

struct PaletteInfo
{
    quint32 dataOffset = 0;
    quint16 tag = 0;
};

struct PaletteTable
{
    QMap<quint16, PaletteInfo> palettes;
    quint32 offset = 0;
};

struct PointerTable
{
    quint32 offset = 0;
    int count = 0;
    int score = 0;
    bool fromConfig = false;
};

quint16 readU16(const QByteArray& rom, int offset)
{
    if (offset < 0 || offset + 2 > rom.size())
        return 0;

    return quint16(static_cast<quint8>(rom[offset]))
         | (quint16(static_cast<quint8>(rom[offset + 1])) << 8);
}

qint16 readS16(const QByteArray& rom, int offset)
{
    return static_cast<qint16>(readU16(rom, offset));
}

quint32 readU32(const QByteArray& rom, int offset)
{
    if (offset < 0 || offset + 4 > rom.size())
        return 0;

    return quint32(static_cast<quint8>(rom[offset]))
         | (quint32(static_cast<quint8>(rom[offset + 1])) << 8)
         | (quint32(static_cast<quint8>(rom[offset + 2])) << 16)
         | (quint32(static_cast<quint8>(rom[offset + 3])) << 24);
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

QString hex32(quint32 value)
{
    return QString("0x%1").arg(value, 8, 16, QChar('0')).toUpper();
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

bool isObjectEventPaletteTag(quint16 tag)
{
    return tag >= 0x1100 && tag < 0x11FF;
}

bool parseGraphicsInfo(const QByteArray& rom, quint32 offset, GraphicsInfo& info)
{
    if (offset + GraphicsInfoSize > static_cast<quint32>(rom.size()))
        return false;

    GraphicsInfo candidate;
    candidate.infoOffset = offset;
    candidate.tileTag = readU16(rom, offset + 0x00);
    candidate.paletteTag = readU16(rom, offset + 0x02);
    candidate.reflectionPaletteTag = readU16(rom, offset + 0x04);
    candidate.size = readU16(rom, offset + 0x06);
    candidate.width = readS16(rom, offset + 0x08);
    candidate.height = readS16(rom, offset + 0x0A);

    quint8 flags = static_cast<quint8>(rom[static_cast<int>(offset + 0x0C)]);
    candidate.paletteSlot = flags & 0x0F;
    candidate.shadowSize = (flags >> 4) & 0x03;
    candidate.inanimate = (flags >> 6) & 0x01;
    candidate.disableReflectionPaletteLoad = (flags >> 7) & 0x01;
    candidate.tracks = static_cast<quint8>(rom[static_cast<int>(offset + 0x0D)]);

    quint32 imagesOffset = 0;
    if (!romPointerToOffset(readU32(rom, offset + 0x1C), rom.size(), imagesOffset))
        return false;
    candidate.imagesOffset = imagesOffset;

    if (candidate.width <= 0 || candidate.height <= 0 || candidate.width > 128 || candidate.height > 128)
        return false;
    if (candidate.width % 8 != 0 || candidate.height % 8 != 0)
        return false;
    if (candidate.size == 0 || candidate.size > 0x4000)
        return false;

    quint32 firstFrameData = 0;
    if (!romPointerToOffset(readU32(rom, imagesOffset), rom.size(), firstFrameData))
        return false;

    quint16 firstFrameSize = readU16(rom, imagesOffset + 4);
    int expectedFrameSize = candidate.width * candidate.height / 2;
    if (firstFrameSize == 0 || firstFrameSize > 0x4000)
        return false;
    if (firstFrameSize != expectedFrameSize)
        return false;

    info = candidate;
    return true;
}

int countPointerTableEntries(const QByteArray& rom, quint32 tableOffset)
{
    int count = 0;
    while (tableOffset + static_cast<quint32>((count + 1) * 4) <= static_cast<quint32>(rom.size())
           && count < MaxObjectEventGraphics)
    {
        quint32 infoOffset = 0;
        if (!romPointerToOffset(readU32(rom, static_cast<int>(tableOffset) + count * 4), rom.size(), infoOffset))
            break;

        GraphicsInfo info;
        if (!parseGraphicsInfo(rom, infoOffset, info))
            break;

        ++count;
    }
    return count;
}

QVector<PointerTable> findGraphicsPointerTables(const QByteArray& rom)
{
    QVector<PointerTable> candidates;
    for (quint32 offset = 0; offset + 4 <= static_cast<quint32>(rom.size()); offset += 4)
    {
        if (offset >= 4)
        {
            quint32 previousInfoOffset = 0;
            if (romPointerToOffset(readU32(rom, static_cast<int>(offset - 4)), rom.size(), previousInfoOffset))
            {
                GraphicsInfo previousInfo;
                if (parseGraphicsInfo(rom, previousInfoOffset, previousInfo))
                    continue;
            }
        }

        int count = countPointerTableEntries(rom, offset);
        if (count < 50)
            continue;

        int score = count;
        if (count >= 100 && count <= 260)
            score += 200;
        candidates.append({offset, count, score, false});
    }

    std::sort(candidates.begin(), candidates.end(), [](const PointerTable& a, const PointerTable& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.offset < b.offset;
    });

    return candidates;
}

bool parseConfiguredPointerTable(const QByteArray& rom, const QString& gameCode, PointerTable& table)
{
    bool offOk = false;
    bool countOk = false;
    quint32 offset = romConfigString(gameCode, "overworldGraphicsInfoPointersOffset").toUInt(&offOk, 16);
    int count = romConfigString(gameCode, "overworldGraphicsCount").toInt(&countOk);

    if (!offOk || !countOk || count <= 0 || count > MaxObjectEventGraphics)
        return false;
    if (countPointerTableEntries(rom, offset) < count)
        return false;

    table = {offset, count, count + 1000, true};
    return true;
}

bool makePointerTable(const QByteArray& rom, quint32 offset, int count, bool fromConfig, PointerTable& table)
{
    if (offset == 0 || count <= 0 || count > MaxObjectEventGraphics)
        return false;

    quint32 firstInfoOffset = 0;
    if (!romPointerToOffset(readU32(rom, offset), rom.size(), firstInfoOffset))
        return false;

    GraphicsInfo firstInfo;
    if (!parseGraphicsInfo(rom, firstInfoOffset, firstInfo))
        return false;

    table = {offset, count, count + (fromConfig ? 1000 : 0), fromConfig};
    return true;
}

QVector<FrameInfo> readFrames(const QByteArray& rom, const GraphicsInfo& info, quint32 imageTableEnd)
{
    QVector<FrameInfo> frames;
    QSet<quint32> seen;
    quint16 expectedSize = static_cast<quint16>(info.width * info.height / 2);

    for (int i = 0; i < MaxFramesPerGraphic; ++i)
    {
        quint32 entry = info.imagesOffset + i * SpriteFrameImageSize;
        if (imageTableEnd != 0 && entry >= imageTableEnd)
            break;

        quint32 dataOffset = 0;
        if (!romPointerToOffset(readU32(rom, entry), rom.size(), dataOffset))
            break;

        quint16 size = readU16(rom, entry + 4);
        if (size != expectedSize || dataOffset + size > static_cast<quint32>(rom.size()))
            break;

        if (!seen.contains(dataOffset))
        {
            frames.append({dataOffset, size, {}});
            seen.insert(dataOffset);
        }
    }

    return frames;
}

PaletteTable findPaletteTable(const QByteArray& rom, const QSet<quint16>& referencedTags)
{
    struct Candidate
    {
        quint32 offset = 0;
        int count = 0;
        int matches = 0;
        int score = 0;
    };

    QVector<Candidate> candidates;
    for (quint32 offset = 0; offset + SpritePaletteSize <= static_cast<quint32>(rom.size()); offset += 4)
    {
        int count = 0;
        int matches = 0;
        while (offset + static_cast<quint32>((count + 1) * SpritePaletteSize) <= static_cast<quint32>(rom.size()) && count < 256)
        {
            quint32 entry = offset + count * SpritePaletteSize;
            quint32 pointer = readU32(rom, entry);
            quint16 tag = readU16(rom, entry + 4);

            if (tag == 0x11FF || (pointer == 0 && tag == 0))
                break;
            if (!isObjectEventPaletteTag(tag))
                break;

            quint32 palOffset = 0;
            if (!romPointerToOffset(pointer, rom.size(), palOffset))
                break;
            if (palOffset + 32 > static_cast<quint32>(rom.size()))
                break;
            if (!paletteLooksUsable(rom.mid(static_cast<int>(palOffset), 32)))
                break;

            if (referencedTags.contains(tag))
                ++matches;
            ++count;
        }

        if (count >= 4 && matches > 0)
            candidates.append({offset, count, matches, matches * 1000 + count});
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.score > b.score;
    });

    PaletteTable table;
    if (candidates.isEmpty())
        return table;

    Candidate best = candidates.first();
    for (int i = 0; i < best.count; ++i)
    {
        quint32 entry = best.offset + i * SpritePaletteSize;
        quint32 palOffset = 0;
        if (!romPointerToOffset(readU32(rom, entry), rom.size(), palOffset))
            continue;
        quint16 tag = readU16(rom, entry + 4);
        table.palettes[tag] = {palOffset, tag};
    }

    table.offset = best.offset;
    return table;
}

QImage renderFrame(const QByteArray& data, const std::array<QColor, 16>& palette, int width, int height)
{
    QImage img = loadSprite(data, palette, width, height).convertToFormat(QImage::Format_ARGB32);
    QRgb transparentSource = palette[0].rgba();
    QRgb transparentDest = qRgba(palette[0].red(), palette[0].green(), palette[0].blue(), 0);

    for (int y = 0; y < img.height(); ++y)
    {
        for (int x = 0; x < img.width(); ++x)
        {
            if (img.pixel(x, y) == transparentSource)
                img.setPixel(x, y, transparentDest);
        }
    }
    return img;
}

QImage renderContactSheet(const QVector<QImage>& frames, int frameWidth, int frameHeight)
{
    if (frames.isEmpty())
        return {};

    int columns = qMin(8, frames.size());
    int rows = (frames.size() + columns - 1) / columns;
    QImage sheet(columns * frameWidth, rows * frameHeight, QImage::Format_ARGB32);
    sheet.fill(Qt::transparent);

    for (int i = 0; i < frames.size(); ++i)
    {
        int x = (i % columns) * frameWidth;
        int y = (i / columns) * frameHeight;
        for (int py = 0; py < frameHeight; ++py)
            for (int px = 0; px < frameWidth; ++px)
                sheet.setPixel(x + px, y + py, frames[i].pixel(px, py));
    }

    return sheet;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(data);
    return true;
}
}

QVector<OverworldSpriteTableCandidate> findOverworldSpriteTableCandidates(const QString& romPath,
                                                                          const QString& gameCode,
                                                                          QString* error)
{
    QFile file(romPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = "Could not open ROM file.";
        return {};
    }

    QByteArray rom = file.readAll();
    file.close();
    if (rom.isEmpty())
    {
        if (error)
            *error = "ROM file is empty.";
        return {};
    }

    QVector<OverworldSpriteTableCandidate> result;

    PointerTable configured;
    if (parseConfiguredPointerTable(rom, gameCode, configured))
        result.append({configured.offset, configured.count, configured.score, configured.fromConfig});

    QVector<PointerTable> found = findGraphicsPointerTables(rom);
    for (const PointerTable& table : found)
    {
        bool duplicate = false;
        for (const OverworldSpriteTableCandidate& existing : result)
        {
            if (existing.offset == table.offset && existing.count == table.count)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            result.append({table.offset, table.count, table.score, table.fromConfig});
    }

    if (result.isEmpty() && error)
        *error = "Could not locate overworld graphics tables. Add overworldGraphicsInfoPointersOffset and overworldGraphicsCount to config.json for this ROM.";

    return result;
}

OverworldSpriteDumpResult exportOverworldSprites(const QString& romPath,
                                                 const QString& gameCode,
                                                 const QString& outputFolder,
                                                 quint32 tableOffset,
                                                 int tableCount)
{
    OverworldSpriteDumpResult result;

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

    PointerTable pointerTable;
    if (tableOffset != 0 || tableCount != 0)
    {
        PointerTable configured;
        bool selectedFromConfig = parseConfiguredPointerTable(rom, gameCode, configured)
                               && configured.offset == tableOffset
                               && configured.count == tableCount;
        if (!makePointerTable(rom, tableOffset, tableCount, selectedFromConfig, pointerTable))
        {
            result.error = "Selected overworld graphics table is no longer valid for this ROM.";
            return result;
        }
    }
    else if (!parseConfiguredPointerTable(rom, gameCode, pointerTable))
    {
        QVector<PointerTable> tables = findGraphicsPointerTables(rom);
        if (!tables.isEmpty())
            pointerTable = tables.first();
    }

    if (pointerTable.offset == 0 || pointerTable.count <= 0)
    {
        result.error = "Could not locate overworld graphics table. Add overworldGraphicsInfoPointersOffset and overworldGraphicsCount to config.json for this ROM.";
        return result;
    }

    const QString outDir = outputFolder + "/graphics/object_events";
    const QString picsDir = outDir + "/pics";
    const QString frameDir = outDir + "/pics/frames";
    const QString previewDir = outDir + "/previews";
    const QString paletteDir = outDir + "/palettes";
    if (!QDir().mkpath(picsDir) || !QDir().mkpath(frameDir) || !QDir().mkpath(previewDir) || !QDir().mkpath(paletteDir))
    {
        result.error = "Could not create graphics/object_events output folders.";
        return result;
    }

    QVector<GraphicsInfo> graphicsInfos(pointerTable.count);
    QVector<bool> validGraphics(pointerTable.count, false);
    QVector<quint32> imageTableOffsets;
    QSet<quint16> referencedPaletteTags;

    for (int i = 0; i < pointerTable.count; ++i)
    {
        quint32 infoOffset = 0;
        if (!romPointerToOffset(readU32(rom, static_cast<int>(pointerTable.offset) + i * 4), rom.size(), infoOffset))
            continue;

        GraphicsInfo info;
        if (!parseGraphicsInfo(rom, infoOffset, info))
            continue;

        graphicsInfos[i] = info;
        validGraphics[i] = true;
        if (isObjectEventPaletteTag(info.paletteTag))
            referencedPaletteTags.insert(info.paletteTag);
        if (isObjectEventPaletteTag(info.reflectionPaletteTag))
            referencedPaletteTags.insert(info.reflectionPaletteTag);
        if (!imageTableOffsets.contains(info.imagesOffset))
            imageTableOffsets.append(info.imagesOffset);
    }
    std::sort(imageTableOffsets.begin(), imageTableOffsets.end());

    PaletteTable paletteTable = findPaletteTable(rom, referencedPaletteTags);
    if (paletteTable.palettes.isEmpty())
    {
        result.error = "Could not locate the object-event palette table, so PNG colors would be wrong.";
        return result;
    }

    QSet<quint16> writtenPalettes;
    QJsonArray graphicsJson;
    int exportedGraphics = 0;
    int exportedFrames = 0;

    for (int i = 0; i < pointerTable.count; ++i)
    {
        if (!validGraphics[i])
            continue;

        GraphicsInfo info = graphicsInfos[i];

        quint32 imageTableEnd = 0;
        auto nextIt = std::upper_bound(imageTableOffsets.begin(), imageTableOffsets.end(), info.imagesOffset);
        if (nextIt != imageTableOffsets.end())
            imageTableEnd = *nextIt;

        QVector<FrameInfo> frames = readFrames(rom, info, imageTableEnd);
        if (frames.isEmpty())
            continue;

        if (!paletteTable.palettes.contains(info.paletteTag))
            continue;

        PaletteInfo pal = paletteTable.palettes.value(info.paletteTag);
        QByteArray palBytes = rom.mid(static_cast<int>(pal.dataOffset), 32);
        std::array<QColor, 16> palette = loadPalette(palBytes);
        QString paletteFile = QString("palettes/palette_%1.pal").arg(info.paletteTag, 4, 16, QChar('0')).toLower();
        if (!writtenPalettes.contains(info.paletteTag))
        {
            writeFile(outDir + "/" + paletteFile, colorToPalText(palette).toUtf8());
            writtenPalettes.insert(info.paletteTag);
        }

        QVector<QImage> previewFrames;
        QJsonArray frameJson;
        QString baseName = QString("object_%1").arg(i, 3, 10, QChar('0'));
        for (int f = 0; f < frames.size(); ++f)
        {
            QString frameName = QString("pics/frames/%1_frame_%2.png")
                                    .arg(baseName)
                                    .arg(f, 2, 10, QChar('0'));
            QByteArray frameData = rom.mid(static_cast<int>(frames[f].dataOffset), frames[f].size);

            QImage frameImage = renderFrame(frameData, palette, info.width, info.height);
            if (!frameImage.save(outDir + "/" + frameName, "PNG"))
                continue;
            previewFrames.append(frameImage);

            QJsonObject frameObj;
            frameObj["data_offset"] = hex32(frames[f].dataOffset);
            frameObj["size"] = frames[f].size;
            frameObj["png"] = frameName;
            frameJson.append(frameObj);
            ++exportedFrames;
        }

        QImage preview = renderContactSheet(previewFrames, info.width, info.height);
        QString previewFile = QString("previews/%1.png").arg(baseName);
        QString sheetFile = QString("pics/%1.png").arg(baseName);
        if (!preview.isNull())
        {
            preview.save(outDir + "/" + previewFile, "PNG");
            preview.save(outDir + "/" + sheetFile, "PNG");
        }

        QJsonObject obj;
        obj["graphics_id"] = i;
        obj["info_offset"] = hex32(info.infoOffset);
        obj["tile_tag"] = info.tileTag;
        obj["palette_tag"] = info.paletteTag;
        obj["reflection_palette_tag"] = info.reflectionPaletteTag;
        obj["size"] = info.size;
        obj["width"] = info.width;
        obj["height"] = info.height;
        obj["palette_slot"] = info.paletteSlot;
        obj["shadow_size"] = info.shadowSize;
        obj["inanimate"] = info.inanimate != 0;
        obj["disable_reflection_palette_load"] = info.disableReflectionPaletteLoad != 0;
        obj["tracks"] = info.tracks;
        obj["images_offset"] = hex32(info.imagesOffset);
        obj["palette_file"] = paletteFile;
        obj["sheet"] = sheetFile;
        obj["preview"] = previewFile;
        obj["frames"] = frameJson;
        graphicsJson.append(obj);

        ++exportedGraphics;
    }

    if (exportedGraphics == 0)
    {
        result.error = "Overworld graphics table was found, but no sprites could be exported.";
        return result;
    }

    QJsonObject root;
    root["graphics_info_pointers_offset"] = hex32(pointerTable.offset);
    root["object_event_sprite_palettes_offset"] = hex32(paletteTable.offset);
    root["graphics_count"] = pointerTable.count;
    root["used_configured_offsets"] = pointerTable.fromConfig;
    root["graphics"] = graphicsJson;
    writeFile(outDir + "/object_event_graphics_info.json", QJsonDocument(root).toJson(QJsonDocument::Indented));

    result.ok = true;
    result.graphics = exportedGraphics;
    result.frames = exportedFrames;
    result.palettes = writtenPalettes.size();
    result.outputDir = outDir;
    result.usedConfiguredOffsets = pointerTable.fromConfig;
    return result;
}
