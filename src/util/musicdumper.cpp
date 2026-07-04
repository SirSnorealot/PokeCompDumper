#include "musicdumper.h"
#include "romconfig.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <algorithm>
#include <cstdint>

namespace
{
constexpr quint32 RomBase = 0x08000000;
constexpr int SongTableEntrySize = 8;
constexpr int MaxTrackSize = 0x4000;
constexpr int MaxVoicegroupEntries = 128;
constexpr quint16 MidiTicksPerQuarter = 24;

const int WaitTable[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 28, 30, 32, 36, 40,
    42, 44, 48, 52, 54, 56, 60, 64, 66, 68, 72, 76, 78, 80,
    84, 88, 90, 92, 96
};

const int NoteDurationTable[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 24, 24, 24, 28, 28,
    30, 30, 32, 32, 32, 32, 36, 36, 36, 36, 40, 40, 42, 42,
    44, 44, 44, 44, 48, 48, 48, 48, 52, 52, 54, 54, 56, 56,
    56, 56, 60, 60, 60, 60, 64, 64, 66, 66, 68, 68, 68, 68,
    72, 72, 72, 72, 76, 76, 78, 78, 80, 80, 80, 80, 84, 84,
    84, 84, 88, 88, 90, 90, 92, 92, 92, 92, 96
};

struct SongInfo
{
    int id = 0;
    quint32 tableEntryOffset = 0;
    quint32 headerOffset = 0;
    quint16 musicPlayer = 0;
    quint16 tablePriority = 0;
    quint8 trackCount = 0;
    quint8 blockCount = 0;
    quint8 priority = 0;
    quint8 reverb = 0;
    quint32 voicegroupOffset = 0;
    QVector<quint32> trackOffsets;
};

struct MidiEvent
{
    int tick = 0;
    QByteArray data;
};

struct TrackParseState
{
    QMap<int, int> tiedNotes;
    int keyShift = 0;
    int lastNote = -1;
    int lastVelocity = 64;
};

bool eventLess(const MidiEvent& a, const MidiEvent& b)
{
    if (a.tick != b.tick)
        return a.tick < b.tick;
    if (a.data.size() == 3 && b.data.size() == 3)
    {
        bool aNoteOff = (static_cast<uchar>(a.data[0]) & 0xF0) == 0x80;
        bool bNoteOff = (static_cast<uchar>(b.data[0]) & 0xF0) == 0x80;
        return aNoteOff && !bNoteOff;
    }
    return a.data.size() < b.data.size();
}

bool isRomPointer(quint32 value, int romSize)
{
    return value >= RomBase && value < RomBase + static_cast<quint32>(romSize);
}

quint32 toOffset(quint32 pointer)
{
    return pointer - RomBase;
}

quint16 readU16(const QByteArray& rom, quint32 offset)
{
    if (offset + 1 >= static_cast<quint32>(rom.size()))
        return 0;
    const auto* d = reinterpret_cast<const uchar*>(rom.constData());
    return static_cast<quint16>(d[offset] | (d[offset + 1] << 8));
}

quint32 readU32(const QByteArray& rom, quint32 offset)
{
    if (offset + 3 >= static_cast<quint32>(rom.size()))
        return 0;
    const auto* d = reinterpret_cast<const uchar*>(rom.constData());
    return static_cast<quint32>(d[offset] | (d[offset + 1] << 8) | (d[offset + 2] << 16) | (d[offset + 3] << 24));
}

QByteArray readRange(const QByteArray& rom, quint32 offset, int size)
{
    if (offset >= static_cast<quint32>(rom.size()) || size <= 0)
        return {};
    return rom.mid(static_cast<int>(offset), qMin(size, rom.size() - static_cast<int>(offset)));
}

QString hexOffset(quint32 offset)
{
    return QString("%1").arg(offset, 6, 16, QChar('0')).toUpper();
}

QString cleanSymbol(QString value)
{
    value = value.toLower();
    QString out;
    out.reserve(value.size());
    for (QChar ch : value)
    {
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
            out += ch;
        else
            out += '_';
    }
    while (out.contains("__"))
        out.replace("__", "_");
    if (out.startsWith('_'))
        out.remove(0, 1);
    if (out.endsWith('_'))
        out.chop(1);
    return out.isEmpty() ? "song" : out;
}

QString songName(const QString& gameCode, int id)
{
    QString name = romEnumName(gameCode, "music", id);
    QString zeroName = romEnumName(gameCode, "music", 0);
    if (name == QString::number(id) || name.isEmpty() || (id != 0 && name == zeroName))
        name = QString("song_%1").arg(id, 3, 10, QChar('0'));
    return cleanSymbol(name);
}

QString voicegroupName(quint32 offset)
{
    return "exported_" + hexOffset(offset).toLower();
}

QString directSoundName(quint32 offset)
{
    return "DirectSoundWaveData_exported_" + hexOffset(offset).toLower();
}

QString programmableWaveName(quint32 offset)
{
    return "ProgrammableWaveData_exported_" + hexOffset(offset).toLower();
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(data) == data.size();
}

bool writeTextFile(const QString& path, const QString& text)
{
    return writeFile(path, text.toUtf8());
}

void removeFiles(const QString& dirPath, const QStringList& filters)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return;

    const QFileInfoList files = dir.entryInfoList(filters, QDir::Files);
    for (const QFileInfo& file : files)
        QFile::remove(file.absoluteFilePath());
}

void appendU16BE(QByteArray& out, quint16 v)
{
    out.append(static_cast<char>((v >> 8) & 0xFF));
    out.append(static_cast<char>(v & 0xFF));
}

void appendU32BE(QByteArray& out, quint32 v)
{
    out.append(static_cast<char>((v >> 24) & 0xFF));
    out.append(static_cast<char>((v >> 16) & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
    out.append(static_cast<char>(v & 0xFF));
}

void appendU16LE(QByteArray& out, quint16 v)
{
    out.append(static_cast<char>(v & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
}

void appendU32LE(QByteArray& out, quint32 v)
{
    out.append(static_cast<char>(v & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
    out.append(static_cast<char>((v >> 16) & 0xFF));
    out.append(static_cast<char>((v >> 24) & 0xFF));
}

void appendVarLen(QByteArray& out, quint32 value)
{
    quint32 buffer = value & 0x7F;
    while ((value >>= 7) != 0)
    {
        buffer <<= 8;
        buffer |= ((value & 0x7F) | 0x80);
    }
    while (true)
    {
        out.append(static_cast<char>(buffer & 0xFF));
        if (buffer & 0x80)
            buffer >>= 8;
        else
            break;
    }
}

QByteArray midiMeta(int type, const QByteArray& payload)
{
    QByteArray out;
    out.append(static_cast<char>(0xFF));
    out.append(static_cast<char>(type));
    appendVarLen(out, static_cast<quint32>(payload.size()));
    out += payload;
    return out;
}

QByteArray makeMidiTrack(QVector<MidiEvent> events)
{
    std::sort(events.begin(), events.end(), eventLess);
    QByteArray body;
    int lastTick = 0;
    for (const MidiEvent& ev : events)
    {
        appendVarLen(body, static_cast<quint32>(qMax(0, ev.tick - lastTick)));
        body += ev.data;
        lastTick = ev.tick;
    }
    appendVarLen(body, 0);
    body += midiMeta(0x2F, {});

    QByteArray out = "MTrk";
    appendU32BE(out, static_cast<quint32>(body.size()));
    out += body;
    return out;
}

int noteDuration(quint8 command)
{
    if (command == 0xCF)
        return 96;
    int idx = command - 0xD0;
    if (idx >= 0 && idx < static_cast<int>(std::size(NoteDurationTable)))
        return NoteDurationTable[idx];
    return 1;
}

bool isLikelyCommand(quint8 value)
{
    return value >= 0x80;
}

void addMidiEvent(QVector<MidiEvent>& events, int tick, std::initializer_list<uchar> bytes)
{
    QByteArray data;
    for (uchar byte : bytes)
        data.append(static_cast<char>(byte));
    events.append({tick, data});
}

void addTempo(QVector<MidiEvent>& events, int tick, int gbaTempo)
{
    int bpm = qBound(1, gbaTempo * 2, 255);
    quint32 mpqn = static_cast<quint32>(60000000 / bpm);
    QByteArray payload;
    payload.append(static_cast<char>((mpqn >> 16) & 0xFF));
    payload.append(static_cast<char>((mpqn >> 8) & 0xFF));
    payload.append(static_cast<char>(mpqn & 0xFF));
    events.append({tick, midiMeta(0x51, payload)});
}

int commandParamCount(quint8 command)
{
    switch (command)
    {
    case 0xB2:
    case 0xB3:
        return 4;
    case 0xB5:
        return 5;
    case 0xB9:
        return 3;
    case 0xBA:
    case 0xBB:
    case 0xBC:
    case 0xBD:
    case 0xBE:
    case 0xBF:
    case 0xC0:
    case 0xC1:
    case 0xC2:
    case 0xC3:
    case 0xC4:
    case 0xC5:
    case 0xC8:
        return 1;
    case 0xCD:
        return 2;
    default:
        return 0;
    }
}

int midiPanFromMPlay(int pan)
{
    return qBound(0, pan, 127);
}

int midiPitchBendFromMPlay(int bend)
{
    int centered = qBound(-64, bend - 0x40, 63);
    return qBound(0, 8192 + centered * 128, 16383);
}

void addPitchBend(QVector<MidiEvent>& events, int tick, int channel, int bend)
{
    int value = midiPitchBendFromMPlay(bend);
    addMidiEvent(events, tick, {static_cast<uchar>(0xE0 | channel), static_cast<uchar>(value & 0x7F), static_cast<uchar>((value >> 7) & 0x7F)});
}

void parseTrackRange(const QByteArray& rom, int& pos, int end, int channel, QVector<MidiEvent>& events, int& tick,
                     TrackParseState& state, QSet<int>& visitedGotos, int depth)
{
    if (depth > 16)
        return;

    while (pos < end)
    {
        quint8 command = static_cast<quint8>(rom[pos++]);
        if (command >= 0x80 && command <= 0xB0)
        {
            tick += WaitTable[command - 0x80];
            continue;
        }
        if (command == 0xB1 || command == 0xB4)
            break;
        if (command == 0xB2)
        {
            quint32 target = readU32(rom, static_cast<quint32>(pos));
            pos += 4;
            if (!isRomPointer(target, rom.size()))
                break;
            int targetPos = static_cast<int>(toOffset(target));
            if (visitedGotos.contains(targetPos))
                break;
            visitedGotos.insert(targetPos);
            pos = targetPos;
            end = rom.size();
            continue;
        }
        if (command == 0xB3)
        {
            quint32 target = readU32(rom, static_cast<quint32>(pos));
            pos += 4;
            if (isRomPointer(target, rom.size()))
            {
                int patternPos = static_cast<int>(toOffset(target));
                QSet<int> patternVisited;
                parseTrackRange(rom, patternPos, rom.size(), channel, events, tick, state, patternVisited, depth + 1);
            }
            continue;
        }
        if (command == 0xB5)
        {
            int repeatCount = pos < end ? static_cast<quint8>(rom[pos++]) : 0;
            quint32 target = readU32(rom, static_cast<quint32>(pos));
            pos += 4;
            if (isRomPointer(target, rom.size()))
            {
                repeatCount = qBound(0, repeatCount, 32);
                for (int i = 0; i < repeatCount; ++i)
                {
                    int repeatPos = static_cast<int>(toOffset(target));
                    QSet<int> repeatVisited;
                    parseTrackRange(rom, repeatPos, rom.size(), channel, events, tick, state, repeatVisited, depth + 1);
                }
            }
            continue;
        }
        if (command >= 0xD0 || command == 0xCF)
        {
            if (pos >= end && state.lastNote < 0)
                break;

            int key = state.lastNote;
            if (pos < end && !isLikelyCommand(static_cast<quint8>(rom[pos])))
            {
                key = static_cast<quint8>(rom[pos++]);
                state.lastNote = key;
            }
            if (key < 0)
                break;
            key += state.keyShift;
            key = qBound(0, key, 127);

            quint8 velocity = static_cast<quint8>(qBound(0, state.lastVelocity, 127));
            if (pos < end && !isLikelyCommand(static_cast<quint8>(rom[pos])))
            {
                velocity = static_cast<quint8>(rom[pos++]);
                state.lastVelocity = velocity;
            }
            if (command == 0xCF)
            {
                if (!state.tiedNotes.contains(key))
                {
                    state.tiedNotes[key] = tick;
                    addMidiEvent(events, tick, {static_cast<uchar>(0x90 | channel), static_cast<uchar>(key), velocity});
                }
                continue;
            }
            if (pos < end && !isLikelyCommand(static_cast<quint8>(rom[pos])))
                ++pos;
            int dur = qMax(1, noteDuration(command));
            addMidiEvent(events, tick, {static_cast<uchar>(0x90 | channel), static_cast<uchar>(key), velocity});
            addMidiEvent(events, tick + dur, {static_cast<uchar>(0x80 | channel), static_cast<uchar>(key), 0});
            continue;
        }
        if (command == 0xCE)
        {
            int key = -1;
            if (pos < end && !isLikelyCommand(static_cast<quint8>(rom[pos])))
            {
                state.lastNote = static_cast<quint8>(rom[pos++]);
                key = state.lastNote;
            }
            else if (state.lastNote >= 0)
            {
                key = state.lastNote;
            }

            if (key >= 0)
            {
                key = qBound(0, key + state.keyShift, 127);
                if (state.tiedNotes.contains(key))
                {
                    addMidiEvent(events, tick, {static_cast<uchar>(0x80 | channel), static_cast<uchar>(key), 0});
                    state.tiedNotes.remove(key);
                }
            }
            else
            {
                const QList<int> keys = state.tiedNotes.keys();
                for (int key : keys)
                    addMidiEvent(events, tick, {static_cast<uchar>(0x80 | channel), static_cast<uchar>(key), 0});
                state.tiedNotes.clear();
            }
            continue;
        }
        if (command == 0xBB && pos < end)
        {
            addTempo(events, tick, static_cast<quint8>(rom[pos]));
            ++pos;
            continue;
        }
        if (command == 0xBC && pos < end)
        {
            state.keyShift = static_cast<qint8>(rom[pos++]);
            continue;
        }
        if (command == 0xBD && pos < end)
        {
            addMidiEvent(events, tick, {static_cast<uchar>(0xC0 | channel), static_cast<uchar>(static_cast<quint8>(rom[pos]) & 0x7F)});
            ++pos;
            continue;
        }
        if (command == 0xBE && pos < end)
        {
            addMidiEvent(events, tick, {static_cast<uchar>(0xB0 | channel), 7, static_cast<uchar>(static_cast<quint8>(rom[pos]) & 0x7F)});
            ++pos;
            continue;
        }
        if (command == 0xBF && pos < end)
        {
            addMidiEvent(events, tick, {static_cast<uchar>(0xB0 | channel), 10, static_cast<uchar>(midiPanFromMPlay(static_cast<quint8>(rom[pos])))});
            ++pos;
            continue;
        }
        if (command == 0xC0 && pos < end)
        {
            addPitchBend(events, tick, channel, static_cast<quint8>(rom[pos]));
            ++pos;
            continue;
        }
        if (command == 0xC1 && pos < end)
        {
            addMidiEvent(events, tick, {static_cast<uchar>(0xB0 | channel), 101, 0});
            addMidiEvent(events, tick, {static_cast<uchar>(0xB0 | channel), 100, 0});
            addMidiEvent(events, tick, {static_cast<uchar>(0xB0 | channel), 6, static_cast<uchar>(static_cast<quint8>(rom[pos]) & 0x7F)});
            ++pos;
            continue;
        }
        if (command == 0xC4 && pos < end)
        {
            addMidiEvent(events, tick, {static_cast<uchar>(0xB0 | channel), 1, static_cast<uchar>(static_cast<quint8>(rom[pos]) & 0x7F)});
            ++pos;
            continue;
        }
        pos += commandParamCount(command);
    }
}

QVector<MidiEvent> parseTrackToMidi(const QByteArray& rom, quint32 trackOffset, int trackSize, int channel)
{
    QVector<MidiEvent> events;
    int tick = 0;
    int pos = static_cast<int>(trackOffset);
    int end = qMin(pos + trackSize, rom.size());
    channel = qBound(0, channel, 15);
    TrackParseState state;
    QSet<int> visitedGotos;

    parseTrackRange(rom, pos, end, channel, events, tick, state, visitedGotos, 0);
    const QList<int> keys = state.tiedNotes.keys();
    for (int key : keys)
        addMidiEvent(events, tick, {static_cast<uchar>(0x80 | channel), static_cast<uchar>(key), 0});

    return events;
}

QByteArray buildMidi(const QByteArray& rom, const SongInfo& song, const QMap<quint32, int>& trackSizes)
{
    int midiTrackCount = qMax(1, song.trackOffsets.size());
    QByteArray out = "MThd";
    appendU32BE(out, 6);
    appendU16BE(out, 1);
    appendU16BE(out, static_cast<quint16>(midiTrackCount));
    appendU16BE(out, MidiTicksPerQuarter);

    if (song.trackOffsets.isEmpty())
    {
        QVector<MidiEvent> events;
        events.append({0, midiMeta(0x03, QString("song_%1").arg(song.id, 3, 10, QChar('0')).toUtf8())});
        out += makeMidiTrack(events);
        return out;
    }

    for (int i = 0; i < song.trackOffsets.size(); ++i)
    {
        QVector<MidiEvent> events = parseTrackToMidi(rom, song.trackOffsets[i], trackSizes.value(song.trackOffsets[i], MaxTrackSize), i % 16);
        if (i == 0)
            events.append({0, midiMeta(0x03, QString("song_%1").arg(song.id, 3, 10, QChar('0')).toUtf8())});
        out += makeMidiTrack(events);
    }
    return out;
}

void addUnique(QVector<quint32>& values, QSet<quint32>& seen, quint32 value)
{
    if (!seen.contains(value))
    {
        seen.insert(value);
        values.append(value);
    }
}

bool isPlausibleSongEntry(const QByteArray& rom, quint32 entryOffset)
{
    if (entryOffset + SongTableEntrySize > static_cast<quint32>(rom.size()))
        return false;

    quint32 headerPtr = readU32(rom, entryOffset);
    if (!isRomPointer(headerPtr, rom.size()))
        return false;

    quint16 player = readU16(rom, entryOffset + 4);
    if (player > 3)
        return false;

    quint32 headerOffset = toOffset(headerPtr);
    if (headerOffset + 3 >= static_cast<quint32>(rom.size()))
        return false;

    quint8 trackCount = static_cast<quint8>(rom[static_cast<int>(headerOffset)]);
    if (trackCount > 16)
        return false;
    if (trackCount == 0)
        return true;

    if (headerOffset + 8 + static_cast<quint32>(trackCount * 4) > static_cast<quint32>(rom.size()))
        return false;

    if (!isRomPointer(readU32(rom, headerOffset + 4), rom.size()))
        return false;

    for (int t = 0; t < trackCount; ++t)
        if (!isRomPointer(readU32(rom, headerOffset + 8 + static_cast<quint32>(t * 4)), rom.size()))
            return false;

    return true;
}

int songTableScore(const QByteArray& rom, quint32 tableOffset, int songCount, int maxEntries)
{
    if (tableOffset + static_cast<quint32>(songCount * SongTableEntrySize) > static_cast<quint32>(rom.size()))
        return 0;

    int score = 0;
    int entries = qMin(songCount, maxEntries);
    for (int i = 0; i < entries; ++i)
    {
        quint32 entry = tableOffset + static_cast<quint32>(i * SongTableEntrySize);
        if (isPlausibleSongEntry(rom, entry))
            ++score;
        else if (i < 8)
            return 0;
    }
    return score;
}

quint32 findSongTableOffset(const QByteArray& rom, int songCount)
{
    quint32 bestOffset = 0;
    int bestScore = 0;
    if (songCount <= 0 || static_cast<quint64>(songCount) * SongTableEntrySize > static_cast<quint64>(rom.size()))
        return 0;

    const int quickEntries = qMin(songCount, 32);
    const int quickThreshold = qMax(8, quickEntries - 4);
    const int fullThreshold = qMax(16, songCount / 2);
    const quint32 lastOffset = static_cast<quint32>(rom.size()) - static_cast<quint32>(songCount * SongTableEntrySize);

    for (quint32 offset = 0; offset <= lastOffset; offset += 4)
    {
        int quickScore = songTableScore(rom, offset, songCount, quickEntries);
        if (quickScore < quickThreshold)
            continue;

        int fullScore = songTableScore(rom, offset, songCount, songCount);
        if (fullScore > bestScore)
        {
            bestScore = fullScore;
            bestOffset = offset;
            if (bestScore == songCount)
                break;
        }
    }

    return bestScore >= fullThreshold ? bestOffset : 0;
}

QVector<SongInfo> readSongTable(const QByteArray& rom, quint32 songTableOffset, int songCount, QVector<quint32>& knownOffsets, QSet<quint32>& knownSet)
{
    QVector<SongInfo> songs;
    if (songTableOffset + static_cast<quint32>(songCount * SongTableEntrySize) > static_cast<quint32>(rom.size()))
        return songs;

    for (int i = 0; i < songCount; ++i)
    {
        quint32 entry = songTableOffset + static_cast<quint32>(i * SongTableEntrySize);
        quint32 headerPtr = readU32(rom, entry);
        if (!isRomPointer(headerPtr, rom.size()))
            continue;

        SongInfo song;
        song.id = i;
        song.tableEntryOffset = entry;
        song.headerOffset = toOffset(headerPtr);
        song.musicPlayer = readU16(rom, entry + 4);
        song.tablePriority = readU16(rom, entry + 6);
        if (!isPlausibleSongEntry(rom, entry))
            continue;

        addUnique(knownOffsets, knownSet, entry);
        addUnique(knownOffsets, knownSet, song.headerOffset);

        song.trackCount = static_cast<quint8>(rom[static_cast<int>(song.headerOffset)]);
        song.blockCount = static_cast<quint8>(rom[static_cast<int>(song.headerOffset + 1)]);
        song.priority = static_cast<quint8>(rom[static_cast<int>(song.headerOffset + 2)]);
        song.reverb = static_cast<quint8>(rom[static_cast<int>(song.headerOffset + 3)]);
        if (song.trackCount > 0)
        {
            quint32 voicegroupPtr = readU32(rom, song.headerOffset + 4);
            song.voicegroupOffset = toOffset(voicegroupPtr);
            addUnique(knownOffsets, knownSet, song.voicegroupOffset);

            for (int t = 0; t < qMin<int>(song.trackCount, 16); ++t)
            {
                quint32 trackOffset = toOffset(readU32(rom, song.headerOffset + 8 + static_cast<quint32>(t * 4)));
                song.trackOffsets.append(trackOffset);
                addUnique(knownOffsets, knownSet, trackOffset);
            }
        }
        songs.append(song);
    }
    return songs;
}

int nextKnownOffset(quint32 offset, const QVector<quint32>& sortedOffsets, int romSize, int maxSize)
{
    auto it = std::upper_bound(sortedOffsets.begin(), sortedOffsets.end(), offset);
    int end = (it == sortedOffsets.end()) ? qMin<int>(romSize, static_cast<int>(offset) + maxSize) : static_cast<int>(*it);
    if (end <= static_cast<int>(offset))
        return 0;
    return qMin(maxSize, end - static_cast<int>(offset));
}

int relatedBlobSize(const QByteArray& rom, quint32 offset, const QVector<quint32>& sortedOffsets)
{
    if (offset + 16 <= static_cast<quint32>(rom.size()))
    {
        quint32 sampleSize = readU32(rom, offset + 12);
        if (sampleSize > 0 && sampleSize < 0x200000 && offset + 16 + sampleSize <= static_cast<quint32>(rom.size()))
            return static_cast<int>(16 + sampleSize);
    }
    return nextKnownOffset(offset, sortedOffsets, rom.size(), 32);
}

QString byteList(const QByteArray& bytes)
{
    QStringList parts;
    for (uchar b : bytes)
        parts << QString("0x%1").arg(QString("%1").arg(b, 2, 16, QChar('0')).toUpper());
    return parts.join(", ");
}

QString byteLine(const QByteArray& bytes)
{
    return "\t.byte " + byteList(bytes) + "\n";
}

int decompPan(quint8 panSweep)
{
    return (panSweep & 0x80) ? (panSweep & 0x7F) : 0;
}

struct VoiceRefs
{
    QSet<quint32> directSamples;
    QSet<quint32> programmableWaves;
    QSet<quint32> voicegroups;
    QSet<quint32> keySplitTables;
    QMap<quint32, QString> directLabels;
    QMap<quint32, QString> directFiles;
    QMap<quint32, QString> programmableLabels;
    QMap<quint32, QString> programmableFiles;
    QMap<quint32, QString> voicegroupLabels;
    QMap<quint32, QString> voicegroupFiles;
    QMap<quint32, QString> keySplitLabels;
};

QString keySplitName(quint32 offset)
{
    return "exported_" + hexOffset(offset).toLower();
}

QString directSoundSymbol(quint32 offset, const VoiceRefs& refs)
{
    return refs.directLabels.value(offset, directSoundName(offset));
}

QString directSoundFile(quint32 offset, const VoiceRefs& refs)
{
    return refs.directFiles.value(offset, "exported/" + hexOffset(offset).toLower());
}

QString programmableWaveSymbol(quint32 offset, const VoiceRefs& refs)
{
    return refs.programmableLabels.value(offset, programmableWaveName(offset));
}

QString programmableWaveFile(quint32 offset, const VoiceRefs& refs)
{
    return refs.programmableFiles.value(offset, "exported/" + hexOffset(offset).toLower());
}

QString voicegroupSymbol(quint32 offset, const VoiceRefs& refs)
{
    return refs.voicegroupLabels.value(offset, "voicegroup_" + voicegroupName(offset));
}

QString voicegroupMacroName(quint32 offset, const VoiceRefs& refs)
{
    QString symbol = voicegroupSymbol(offset, refs);
    return symbol.startsWith("voicegroup_") ? symbol.mid(11) : symbol;
}

QString voicegroupIncludeFile(quint32 offset, const VoiceRefs& refs)
{
    return refs.voicegroupFiles.value(offset, voicegroupName(offset) + ".inc");
}

QString keySplitSymbol(quint32 offset, const VoiceRefs& refs)
{
    QString name = refs.keySplitLabels.value(offset, keySplitName(offset));
    return name.startsWith("keysplit_") ? name : "keysplit_" + name;
}

QString voiceEntryLine(const QByteArray& entry, VoiceRefs& refs, int romSize)
{
    if (entry.size() < 12)
        return {};

    const quint8 type = static_cast<quint8>(entry[0]);
    const quint8 key = static_cast<quint8>(entry[1]);
    const quint8 length = static_cast<quint8>(entry[2]);
    const quint8 panSweep = static_cast<quint8>(entry[3]);
    const quint8 attack = static_cast<quint8>(entry[8]);
    const quint8 decay = static_cast<quint8>(entry[9]);
    const quint8 sustain = static_cast<quint8>(entry[10]);
    const quint8 release = static_cast<quint8>(entry[11]);
    quint32 ptrA = static_cast<uchar>(entry[4]) | (static_cast<uchar>(entry[5]) << 8) |
                   (static_cast<uchar>(entry[6]) << 16) | (static_cast<uchar>(entry[7]) << 24);
    quint32 ptrB = static_cast<uchar>(entry[8]) | (static_cast<uchar>(entry[9]) << 8) |
                   (static_cast<uchar>(entry[10]) << 16) | (static_cast<uchar>(entry[11]) << 24);
    const bool ptrAIsRom = isRomPointer(ptrA, romSize);
    const bool ptrBIsRom = isRomPointer(ptrB, romSize);
    const quint32 ptrAOffset = ptrAIsRom ? toOffset(ptrA) : 0;
    const quint32 ptrBOffset = ptrBIsRom ? toOffset(ptrB) : 0;
    const int directPan = decompPan(panSweep);
    const int cgbPan = decompPan(length);

    if ((type == 0 || type == 8 || type == 16 || type == 0x20 || type == 0x30) && ptrAIsRom)
    {
        refs.directSamples.insert(ptrAOffset);
        QString macro = (type == 8) ? "voice_directsound_no_resample" : (type == 16 ? "voice_directsound_alt" : (type == 0x30 ? "cry_reverse" : (type == 0x20 ? "cry" : "voice_directsound")));
        if (type == 0x20 || type == 0x30)
            return QString("\t%1 %2\n").arg(macro, directSoundSymbol(ptrAOffset, refs));
        return QString("\t%1 %2, %3, %4, %5, %6, %7, %8\n")
            .arg(macro)
            .arg(key)
            .arg(directPan)
            .arg(directSoundSymbol(ptrAOffset, refs))
            .arg(attack)
            .arg(decay)
            .arg(sustain)
            .arg(release);
    }

    if ((type == 1 || type == 9))
    {
        QString macro = (type == 9) ? "voice_square_1_alt" : "voice_square_1";
        const quint8 duty = static_cast<quint8>(entry[4]) & 0x3;
        return QString("\t%1 %2, %3, %4, %5, %6, %7, %8, %9\n")
            .arg(macro)
            .arg(key)
            .arg(cgbPan)
            .arg(panSweep)
            .arg(duty)
            .arg(attack & 0x7)
            .arg(decay & 0x7)
            .arg(sustain & 0xF)
            .arg(release & 0x7);
    }

    if ((type == 2 || type == 10))
    {
        QString macro = (type == 10) ? "voice_square_2_alt" : "voice_square_2";
        const quint8 duty = static_cast<quint8>(entry[4]) & 0x3;
        return QString("\t%1 %2, %3, %4, %5, %6, %7, %8\n")
            .arg(macro)
            .arg(key)
            .arg(cgbPan)
            .arg(duty)
            .arg(attack & 0x7)
            .arg(decay & 0x7)
            .arg(sustain & 0xF)
            .arg(release & 0x7);
    }

    if ((type == 3 || type == 11) && ptrAIsRom)
    {
        refs.programmableWaves.insert(ptrAOffset);
        QString macro = (type == 11) ? "voice_programmable_wave_alt" : "voice_programmable_wave";
        return QString("\t%1 %2, %3, %4, %5, %6, %7, %8\n")
            .arg(macro)
            .arg(key)
            .arg(directPan)
            .arg(programmableWaveSymbol(ptrAOffset, refs))
            .arg(attack & 0x7)
            .arg(decay & 0x7)
            .arg(sustain & 0xF)
            .arg(release & 0x7);
    }

    if (type == 4 || type == 12)
    {
        QString macro = (type == 12) ? "voice_noise_alt" : "voice_noise";
        const quint8 period = static_cast<quint8>(entry[4]) & 0x1;
        return QString("\t%1 %2, %3, %4, %5, %6, %7, %8\n")
            .arg(macro)
            .arg(key)
            .arg(cgbPan)
            .arg(period)
            .arg(attack & 0x7)
            .arg(decay & 0x7)
            .arg(sustain & 0xF)
            .arg(release & 0x7);
    }

    if (type == 0x40 && ptrAIsRom && ptrBIsRom)
    {
        refs.voicegroups.insert(ptrAOffset);
        refs.keySplitTables.insert(ptrBOffset);
        return QString("\tvoice_keysplit %1, %2\n").arg(voicegroupSymbol(ptrAOffset, refs), keySplitSymbol(ptrBOffset, refs));
    }

    if (type == 0x80 && ptrAIsRom)
    {
        refs.voicegroups.insert(ptrAOffset);
        return QString("\tvoice_keysplit_all %1\n").arg(voicegroupSymbol(ptrAOffset, refs));
    }

    return byteLine(entry.left(4)) +
           QString("\t.4byte 0x%1\n").arg(ptrA, 8, 16, QChar('0')).toUpper() +
           byteLine(entry.mid(8, 4));
}

QByteArray makeDirectSoundWav(const QByteArray& rom, quint32 offset)
{
    if (offset + 16 > static_cast<quint32>(rom.size()))
        return {};

    quint32 agbPitch = readU32(rom, offset + 4);
    quint32 sampleSize = readU32(rom, offset + 12);
    if (agbPitch == 0 || sampleSize == 0 || sampleSize > 0x200000 ||
        offset + 16 + sampleSize > static_cast<quint32>(rom.size()))
        return {};

    quint32 sampleRate = qMax<quint32>(1, (agbPitch + 512) / 1024);
    QByteArray pcm;
    pcm.reserve(static_cast<int>(sampleSize));
    for (quint32 i = 0; i < sampleSize; ++i)
        pcm.append(static_cast<char>(static_cast<quint8>(rom[static_cast<int>(offset + 16 + i)]) ^ 0x80));

    const quint32 dataPad = sampleSize & 1;
    const quint32 riffSize = 4 + (8 + 16) + (8 + 4) + (8 + 4) + (8 + sampleSize + dataPad);
    QByteArray out = "RIFF";
    appendU32LE(out, riffSize);
    out += "WAVEfmt ";
    appendU32LE(out, 16);
    appendU16LE(out, 1);
    appendU16LE(out, 1);
    appendU32LE(out, sampleRate);
    appendU32LE(out, sampleRate);
    appendU16LE(out, 1);
    appendU16LE(out, 8);
    out += "agbp";
    appendU32LE(out, 4);
    appendU32LE(out, agbPitch);
    out += "agbl";
    appendU32LE(out, 4);
    appendU32LE(out, sampleSize);
    out += "data";
    appendU32LE(out, sampleSize);
    out += pcm;
    if (dataPad)
        out.append('\0');
    return out;
}

int keySplitStartingNote(const QByteArray& bytes)
{
    int maxStart = qMin(64, bytes.size() - 1);
    for (int start = 0; start <= maxStart; ++start)
    {
        int len = qMin(16, bytes.size() - start);
        bool plausible = len > 0;
        for (int i = 0; i < len; ++i)
        {
            quint8 value = static_cast<quint8>(bytes[start + i]);
            if (value >= MaxVoicegroupEntries)
            {
                plausible = false;
                break;
            }
        }
        if (plausible)
            return start;
    }
    return 0;
}

QString keySplitTableText(quint32 ptr, const QByteArray& bytes, const VoiceRefs& refs)
{
    if (bytes.isEmpty())
        return {};

    int start = keySplitStartingNote(bytes);
    QString label = keySplitSymbol(ptr, refs);
    if (label.startsWith("keysplit_"))
        label.remove(0, 9);

    QString out = QString("keysplit %1, %2\n").arg(label).arg(start);
    int note = start;
    int pos = start;
    while (pos < bytes.size())
    {
        quint8 value = static_cast<quint8>(bytes[pos]);
        int endPos = pos;
        while (endPos + 1 < bytes.size() && static_cast<quint8>(bytes[endPos + 1]) == value)
            ++endPos;
        out += QString("\tsplit %1, %2\n").arg(value).arg(note + (endPos - pos));
        note += (endPos - pos) + 1;
        pos = endPos + 1;
    }
    out += "\n";
    return out;
}

struct DecompSoundNames
{
    QMap<int, QString> songNames;
    QMap<QString, QString> songVoicegroups;
    QMap<QString, QString> songMidiOptions;
    QStringList directLabels;
    QStringList directFiles;
    QStringList programmableLabels;
    QStringList programmableFiles;
    QStringList voicegroupNames;
    QStringList voicegroupFiles;
    QStringList keySplitNames;
};

DecompSoundNames loadDecompSoundNames(const QString& gameCode)
{
    DecompSoundNames names;
    names.directLabels = romConfigStringList(gameCode, "directSoundLabels");
    names.directFiles = romConfigStringList(gameCode, "directSoundFiles");
    names.programmableLabels = romConfigStringList(gameCode, "programmableWaveLabels");
    names.programmableFiles = romConfigStringList(gameCode, "programmableWaveFiles");
    names.voicegroupNames = romConfigStringList(gameCode, "voicegroupNames");
    names.voicegroupFiles = romConfigStringList(gameCode, "voicegroupFiles");
    names.keySplitNames = romConfigStringList(gameCode, "keySplitLabels");
    names.songVoicegroups = romConfigStringMap(gameCode, "midiVoicegroups");
    names.songMidiOptions = romConfigStringMap(gameCode, "midiOptions");
    return names;
}

bool usesNumericVoicegroups(const QString& gameCode)
{
    Q_UNUSED(gameCode);
    return false;
}

bool isFireRedFamily(const QString& gameCode)
{
    return gameCode.startsWith("BPR") || gameCode.startsWith("BPG");
}

bool usesExactMidiOptions(const QString& gameCode)
{
    return gameCode == "BPEE";
}

QString voicegroupNameFromSong(QString song)
{
    if (song.startsWith("mus_"))
        song.remove(0, 4);
    else if (song.startsWith("se_"))
        song.remove(0, 3);
    return cleanSymbol(song);
}

int numericVoicegroupFromOptions(const QString& options)
{
    const QStringList parts = options.split(' ', Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        if (part.size() > 2 && part.startsWith("-G"))
        {
            bool ok = false;
            int value = part.mid(2).toInt(&ok, 10);
            if (ok)
                return value;
        }
    }
    return -1;
}

QString numericVoicegroupName(int group)
{
    return group >= 0 ? QString("frlg_%1").arg(group, 3, 10, QChar('0')) : QString();
}

QString fireRedFallbackName(quint32 offset)
{
    return "frlg_" + hexOffset(offset).toLower();
}

QString convertNumericVoicegroupOptions(QString options, const QString& voicegroupName)
{
    if (voicegroupName.isEmpty())
        return options;

    QStringList parts = options.split(' ', Qt::SkipEmptyParts);
    for (QString& part : parts)
    {
        if (part.size() > 2 && part.startsWith("-G"))
        {
            bool ok = false;
            part.mid(2).toInt(&ok, 10);
            if (ok)
                part = "-G_" + voicegroupName;
        }
    }
    return parts.isEmpty() ? QString() : " " + parts.join(' ');
}
}

MusicDumpResult exportMusicData(const QString& romPath, const QString& gameCode, const QString& outputFolder)
{
    MusicDumpResult result;

    bool ok = false;
    quint32 songTableOffset = romConfigString(gameCode, "songTableOffset").toUInt(&ok, 16);
    if (!ok || songTableOffset == 0)
    {
        result.error = "Missing songTableOffset in config.json for this ROM.";
        return result;
    }

    int songCount = romConfigString(gameCode, "songCount", "0").toInt(&ok, 10);
    if (!ok || songCount <= 0)
    {
        result.error = "Missing songCount in config.json for this ROM.";
        return result;
    }

    QFile romFile(romPath);
    if (!romFile.open(QIODevice::ReadOnly))
    {
        result.error = "Could not open ROM file.";
        return result;
    }
    QByteArray rom = romFile.readAll();
    romFile.close();

    if (songTableOffset + static_cast<quint32>(songCount * SongTableEntrySize) > static_cast<quint32>(rom.size()))
    {
        songTableOffset = 0;
    }

    QString soundDir = outputFolder + "/sound";
    QString midiDir = soundDir + "/songs/midi";
    QString voicegroupDir = soundDir + "/voicegroups";
    QString directDir = soundDir + "/direct_sound_samples/exported";
    QString waveDir = soundDir + "/programmable_wave_samples/exported";
    QDir().mkpath(midiDir);
    QDir().mkpath(voicegroupDir);
    removeFiles(directDir, {"*.bin", "*.wav"});
    removeFiles(waveDir, {"*.pcm"});
    QDir().rmdir(directDir);
    QDir().rmdir(waveDir);
    removeFiles(midiDir, {"*.mid", "midi.cfg"});
    removeFiles(voicegroupDir, {"*.inc"});
    QFile::remove(soundDir + "/export_manifest.json");
    DecompSoundNames decompNames = loadDecompSoundNames(gameCode);
    const bool numericVoicegroups = usesNumericVoicegroups(gameCode);

    QVector<SongInfo> songs;
    QVector<quint32> knownOffsets;
    QSet<quint32> knownSet;

    songs = readSongTable(rom, songTableOffset, songCount, knownOffsets, knownSet);

    if (songs.isEmpty())
    {
        QVector<quint32> scannedKnownOffsets;
        QSet<quint32> scannedKnownSet;
        quint32 scannedOffset = findSongTableOffset(rom, songCount);
        if (scannedOffset != 0)
        {
            songs = readSongTable(rom, scannedOffset, songCount, scannedKnownOffsets, scannedKnownSet);
            if (!songs.isEmpty())
            {
                songTableOffset = scannedOffset;
                knownOffsets = scannedKnownOffsets;
                knownSet = scannedKnownSet;
            }
        }
        if (songs.isEmpty())
        {
            result.error = QString("No valid song headers found at configured songTableOffset 0x%1, and automatic song table scan failed for this ROM.")
                               .arg(QString::number(songTableOffset, 16).toUpper());
            return result;
        }
    }

    QVector<quint32> voicegroups;
    QSet<quint32> voicegroupSet;
    for (const SongInfo& song : songs)
        if (song.voicegroupOffset)
            addUnique(voicegroups, voicegroupSet, song.voicegroupOffset);

    VoiceRefs refs;
    for (int vgIndex = 0; vgIndex < voicegroups.size(); ++vgIndex)
    {
        quint32 vg = voicegroups[vgIndex];
        for (int i = 0; i < MaxVoicegroupEntries; ++i)
        {
            quint32 off = vg + static_cast<quint32>(i * 12);
            if (off + 11 >= static_cast<quint32>(rom.size()))
                break;
            quint8 type = static_cast<quint8>(rom[static_cast<int>(off)]);
            quint32 ptrA = readU32(rom, off + 4);
            quint32 ptrB = readU32(rom, off + 8);
            if ((type == 0 || type == 8 || type == 16 || type == 0x20 || type == 0x30) && isRomPointer(ptrA, rom.size()))
            {
                quint32 sample = toOffset(ptrA);
                refs.directSamples.insert(sample);
                addUnique(knownOffsets, knownSet, sample);
            }
            else if ((type == 3 || type == 11) && isRomPointer(ptrA, rom.size()))
            {
                quint32 wave = toOffset(ptrA);
                refs.programmableWaves.insert(wave);
                addUnique(knownOffsets, knownSet, wave);
            }
            else if ((type == 0x40 || type == 0x80) && isRomPointer(ptrA, rom.size()))
            {
                quint32 nestedVg = toOffset(ptrA);
                addUnique(voicegroups, voicegroupSet, nestedVg);
                addUnique(knownOffsets, knownSet, nestedVg);
            }
            if (type == 0x40 && isRomPointer(ptrB, rom.size()))
            {
                quint32 keySplit = toOffset(ptrB);
                refs.keySplitTables.insert(keySplit);
                addUnique(knownOffsets, knownSet, keySplit);
            }
        }
    }

    std::sort(knownOffsets.begin(), knownOffsets.end());
    std::sort(voicegroups.begin(), voicegroups.end());

    QMap<quint32, int> trackSizes;
    for (const SongInfo& song : songs)
        for (quint32 track : song.trackOffsets)
            trackSizes[track] = nextKnownOffset(track, knownOffsets, rom.size(), MaxTrackSize);

    QVector<quint32> directSamples;
    QVector<quint32> programmableWaves;
    QVector<quint32> keySplitTables;
    for (quint32 ptr : refs.directSamples)
        directSamples.append(ptr);
    for (quint32 ptr : refs.programmableWaves)
        programmableWaves.append(ptr);
    for (quint32 ptr : refs.keySplitTables)
        keySplitTables.append(ptr);
    std::sort(directSamples.begin(), directSamples.end());
    std::sort(programmableWaves.begin(), programmableWaves.end());
    std::sort(keySplitTables.begin(), keySplitTables.end());

    for (int i = 0; i < voicegroups.size(); ++i)
    {
        if (numericVoicegroups)
        {
            refs.voicegroupLabels[voicegroups[i]] = QString("voicegroup%1").arg(i, 3, 10, QChar('0'));
        }
        else if (i < decompNames.voicegroupNames.size())
        {
            refs.voicegroupLabels[voicegroups[i]] = "voicegroup_" + decompNames.voicegroupNames[i];
            if (i < decompNames.voicegroupFiles.size())
                refs.voicegroupFiles[voicegroups[i]] = decompNames.voicegroupFiles[i];
        }
    }

    for (int i = 0; i < directSamples.size() && i < decompNames.directLabels.size(); ++i)
    {
        refs.directLabels[directSamples[i]] = decompNames.directLabels[i];
        if (i < decompNames.directFiles.size())
            refs.directFiles[directSamples[i]] = decompNames.directFiles[i];
    }
    for (int i = 0; i < programmableWaves.size() && i < decompNames.programmableLabels.size(); ++i)
    {
        refs.programmableLabels[programmableWaves[i]] = decompNames.programmableLabels[i];
        if (i < decompNames.programmableFiles.size())
            refs.programmableFiles[programmableWaves[i]] = decompNames.programmableFiles[i];
    }
    for (const SongInfo& song : songs)
    {
        if (!song.voicegroupOffset)
            continue;
        QString songLabel = decompNames.songNames.value(song.id, songName(gameCode, song.id));
        QString vgName = decompNames.songVoicegroups.value(songLabel);
        if (vgName.isEmpty())
            continue;
        refs.voicegroupLabels[song.voicegroupOffset] = "voicegroup_" + vgName;
        int fileIndex = decompNames.voicegroupNames.indexOf(vgName);
        if (fileIndex >= 0 && fileIndex < decompNames.voicegroupFiles.size())
            refs.voicegroupFiles[song.voicegroupOffset] = decompNames.voicegroupFiles[fileIndex];
    }
    for (const SongInfo& song : songs)
    {
        if (!song.voicegroupOffset || refs.voicegroupLabels.contains(song.voicegroupOffset))
            continue;
        QString songLabel = decompNames.songNames.value(song.id, songName(gameCode, song.id));
        int numericGroup = numericVoicegroupFromOptions(decompNames.songMidiOptions.value(songLabel));
        QString vgName = numericVoicegroupName(numericGroup);
        if (vgName.isEmpty())
            continue;
        refs.voicegroupLabels[song.voicegroupOffset] = "voicegroup_" + vgName;
        refs.voicegroupFiles[song.voicegroupOffset] = vgName + ".inc";
    }
    for (const SongInfo& song : songs)
    {
        if (!song.voicegroupOffset || refs.voicegroupLabels.contains(song.voicegroupOffset))
            continue;
        QString vgName = isFireRedFamily(gameCode)
                             ? fireRedFallbackName(song.voicegroupOffset)
                             : voicegroupNameFromSong(decompNames.songNames.value(song.id, songName(gameCode, song.id)));
        refs.voicegroupLabels[song.voicegroupOffset] = "voicegroup_" + vgName;
        refs.voicegroupFiles[song.voicegroupOffset] = vgName + ".inc";
    }
    if (isFireRedFamily(gameCode))
    {
        for (quint32 vg : voicegroups)
        {
            if (!refs.voicegroupLabels.contains(vg))
            {
                QString vgName = fireRedFallbackName(vg);
                refs.voicegroupLabels[vg] = "voicegroup_" + vgName;
                refs.voicegroupFiles[vg] = vgName + ".inc";
            }
        }
        for (quint32 keySplit : keySplitTables)
            if (!refs.keySplitLabels.contains(keySplit))
                refs.keySplitLabels[keySplit] = fireRedFallbackName(keySplit);
    }
    for (int i = 0; i < keySplitTables.size() && i < decompNames.keySplitNames.size(); ++i)
        refs.keySplitLabels[keySplitTables[i]] = decompNames.keySplitNames[i];

    QString songTable;
    songTable += "\t.equiv MUSIC_PLAYER_BGM,0\n\t.equiv MUSIC_PLAYER_SE1,1\n\t.equiv MUSIC_PLAYER_SE2,2\n\t.equiv MUSIC_PLAYER_SE3,3\n\n\t.align 2\n\ngSongTable::\n";
    QString midiCfg;
    QSet<quint32> uniqueHeaders;
    QSet<quint32> uniqueTracks;
    int exportedSongs = 0;

    for (const SongInfo& song : songs)
    {
        QString name = decompNames.songNames.value(song.id, songName(gameCode, song.id));
        const bool hasMusicData = song.trackCount > 0 && !song.trackOffsets.isEmpty();
        if (hasMusicData)
        {
            QByteArray midi = buildMidi(rom, song, trackSizes);
            writeFile(midiDir + "/" + name + ".mid", midi);
            QString options = usesExactMidiOptions(gameCode) ? decompNames.songMidiOptions.value(name) : QString();
            if (options.isEmpty() && !usesExactMidiOptions(gameCode))
                options = convertNumericVoicegroupOptions(decompNames.songMidiOptions.value(name), song.voicegroupOffset ? voicegroupMacroName(song.voicegroupOffset, refs) : QString());
            if (options.isEmpty())
            {
                options = " -E -R50";
                if (song.voicegroupOffset)
                {
                    if (numericVoicegroups)
                    {
                        const QString symbol = voicegroupSymbol(song.voicegroupOffset, refs);
                        options += " -G" + symbol.mid(QString("voicegroup").size());
                    }
                    else
                    {
                        options += " -G_" + voicegroupMacroName(song.voicegroupOffset, refs);
                    }
                }
                options += " -V080";
            }
            midiCfg += QString("%1.mid:%2\n").arg(name, options);
            songTable += QString("\tsong %1, %2, %3\n").arg(name).arg(song.musicPlayer).arg(song.tablePriority);
            ++exportedSongs;
        }
        else
        {
            songTable += QString("\tsong dummy_song_header, %1, %2\n").arg(song.musicPlayer).arg(song.tablePriority);
        }

        uniqueHeaders.insert(song.headerOffset);
        for (quint32 track : song.trackOffsets)
            uniqueTracks.insert(track);
    }
    writeTextFile(midiDir + "/midi.cfg", midiCfg);
    writeTextFile(soundDir + "/song_table.inc", songTable + "\n\t.align 2\n"
                  "dummy_song_header:\n\t.byte 0, 0, 0, 0\n");

    QString voicegroupsInc;
    for (quint32 vg : voicegroups)
    {
        QString name = voicegroupMacroName(vg, refs);
        QString fileText;
        if (numericVoicegroups)
            fileText = "\t.align 2\n" + voicegroupSymbol(vg, refs) + "::\n";
        else
            fileText = "voice_group " + name + "\n";
        int size = nextKnownOffset(vg, knownOffsets, rom.size(), MaxVoicegroupEntries * 12);
        int entries = qBound(1, size / 12, MaxVoicegroupEntries);
        for (int i = 0; i < entries; ++i)
            fileText += voiceEntryLine(readRange(rom, vg + static_cast<quint32>(i * 12), 12), refs, rom.size());

        if (numericVoicegroups)
        {
            voicegroupsInc += fileText + "\n";
        }
        else
        {
            QString rel = "sound/voicegroups/" + voicegroupIncludeFile(vg, refs);
            voicegroupsInc += ".include \"" + rel + "\"\n";
            QString voicegroupPath = voicegroupDir + "/" + voicegroupIncludeFile(vg, refs);
            QDir().mkpath(QFileInfo(voicegroupPath).absolutePath());
            writeTextFile(voicegroupPath, fileText);
        }
    }
    writeTextFile(soundDir + "/voice_groups.inc", voicegroupsInc);

    QString directInc;
    QString waveInc;
    int directCount = 0;
    int waveCount = 0;

    for (quint32 ptr : directSamples)
    {
        QByteArray wav = makeDirectSoundWav(rom, ptr);
        if (!wav.isEmpty())
        {
            QString name = directSoundSymbol(ptr, refs);
            QString stem = directSoundFile(ptr, refs);
            QString wavPath = soundDir + "/direct_sound_samples/" + stem + ".wav";
            QDir().mkpath(QFileInfo(wavPath).absolutePath());
            writeFile(wavPath, wav);
            directInc += "\t.align 2\n" + name + "::\n\t.incbin \"sound/direct_sound_samples/" + stem + ".bin\"\n\n";
            ++directCount;
        }
    }

    for (quint32 ptr : programmableWaves)
    {
        QString name = programmableWaveSymbol(ptr, refs);
        QString stem = programmableWaveFile(ptr, refs);
        QString pcmPath = soundDir + "/programmable_wave_samples/" + stem + ".pcm";
        QDir().mkpath(QFileInfo(pcmPath).absolutePath());
        writeFile(pcmPath, readRange(rom, ptr, 16));
        waveInc += name + "::\n\t.incbin \"sound/programmable_wave_samples/" + stem + ".pcm\"\n\n";
        ++waveCount;
    }

    writeTextFile(soundDir + "/direct_sound_data.inc", directInc);
    writeTextFile(soundDir + "/programmable_wave_data.inc", waveInc);

    QString keySplitText;
    for (quint32 ptr : keySplitTables)
    {
        auto nextKeySplit = std::upper_bound(keySplitTables.begin(), keySplitTables.end(), ptr);
        int size = nextKeySplit == keySplitTables.end() ? 0 : static_cast<int>(*nextKeySplit - ptr);
        if (size <= 0)
            size = nextKnownOffset(ptr, knownOffsets, rom.size(), 128);
        if (size <= 0)
            size = 128;
        QByteArray table = readRange(rom, ptr, qMin(size, 128));
        keySplitText += keySplitTableText(ptr, table, refs);
    }
    if (!keySplitText.isEmpty())
        writeTextFile(soundDir + "/keysplit_tables.inc", keySplitText);

    result.ok = true;
    result.songs = exportedSongs;
    result.songHeaders = uniqueHeaders.size();
    result.tracks = uniqueTracks.size();
    result.voicegroups = voicegroups.size();
    result.relatedBlobs = directCount + waveCount;
    return result;
}
