#pragma once

#include <QString>

struct MusicDumpResult
{
    bool ok = false;
    int songs = 0;
    int songHeaders = 0;
    int tracks = 0;
    int voicegroups = 0;
    int relatedBlobs = 0;
    QString error;
};

MusicDumpResult exportMusicData(const QString& romPath, const QString& gameCode, const QString& outputFolder);
