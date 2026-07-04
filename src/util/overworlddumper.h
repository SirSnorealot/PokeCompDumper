#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

struct OverworldSpriteTableCandidate
{
    quint32 offset = 0;
    int count = 0;
    int score = 0;
    bool fromConfig = false;
};

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

QVector<OverworldSpriteTableCandidate> findOverworldSpriteTableCandidates(const QString& romPath,
                                                                          const QString& gameCode,
                                                                          QString* error = nullptr);

OverworldSpriteDumpResult exportOverworldSprites(const QString& romPath,
                                                 const QString& gameCode,
                                                 const QString& outputFolder,
                                                 quint32 tableOffset = 0,
                                                 int tableCount = 0);
