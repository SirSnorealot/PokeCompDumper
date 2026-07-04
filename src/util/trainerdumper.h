#pragma once

#include <QString>

struct TrainerSpriteDumpResult
{
    bool ok = false;
    QString error;
    int sprites = 0;
    QString outputDir;
    bool usedConfiguredOffsets = false;
};

TrainerSpriteDumpResult exportTrainerSprites(const QString& romPath,
                                             const QString& gameCode,
                                             const QString& outputFolder);
