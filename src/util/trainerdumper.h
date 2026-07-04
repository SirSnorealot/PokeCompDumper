#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

struct TrainerSpriteTableCandidate
{
    quint32 gfxOffset = 0;
    quint32 palOffset = 0;
    int count = 0;
    int score = 0;
    bool fromConfig = false;
};

struct TrainerSpriteDumpResult
{
    bool ok = false;
    QString error;
    int sprites = 0;
    QString outputDir;
    bool usedConfiguredOffsets = false;
};

QVector<TrainerSpriteTableCandidate> findTrainerSpriteTableCandidates(const QString& romPath,
                                                                      const QString& gameCode,
                                                                      QString* error = nullptr);

TrainerSpriteDumpResult exportTrainerSprites(const QString& romPath,
                                             const QString& gameCode,
                                             const QString& outputFolder,
                                             quint32 gfxTableOffset = 0,
                                             quint32 paletteTableOffset = 0,
                                             int spriteCount = 0);
