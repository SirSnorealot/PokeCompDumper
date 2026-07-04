#pragma once

#include <QString>

struct OverworldSpriteDumpResult
{
    bool ok = false;
    QString error;
    int graphics = 0;
    int frames = 0;
    int palettes = 0;
    QString outputDir;
    bool usedConfiguredOffsets = false;
};

OverworldSpriteDumpResult exportOverworldSprites(const QString& romPath,
                                                 const QString& gameCode,
                                                 const QString& outputFolder);
