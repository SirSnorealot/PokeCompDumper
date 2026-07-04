#include "romconfig.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

static QJsonObject s_config;

bool loadRomConfig(const QString& jsonPath)
{
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();

    if (err.error != QJsonParseError::NoError)
        return false;

    s_config = doc.object();
    return true;
}

static QJsonObject sectionFor(const QString& gameCode)
{
    return s_config.value(gameCode).toObject();
}

QString romConfigString(const QString& gameCode, const QString& key, const QString& def)
{
    return sectionFor(gameCode).value(key).toString(def);
}

QStringList romConfigStringList(const QString& gameCode, const QString& key)
{
    QStringList values;
    QJsonArray arr = sectionFor(gameCode).value(key).toArray();
    for (const QJsonValue& value : arr)
        values.append(value.toString());
    return values;
}

QMap<QString, QString> romConfigStringMap(const QString& gameCode, const QString& key)
{
    QMap<QString, QString> values;
    QJsonObject obj = sectionFor(gameCode).value(key).toObject();
    for (auto it = obj.begin(); it != obj.end(); ++it)
        values[it.key()] = it.value().toString();
    return values;
}

QString romBankPointer(const QString& gameCode, int index)
{
    QJsonArray arr = sectionFor(gameCode).value("bankPointers").toArray();
    if (index < 0 || index >= arr.size())
        return {};
    return arr[index].toString();
}

int romMapsInBank(const QString& gameCode, int index)
{
    QJsonArray arr = sectionFor(gameCode).value("mapsPerBank").toArray();
    if (index < 0 || index >= arr.size())
        return 0;
    return arr[index].toInt();
}

int romTilesetTileCount(const QString& gameCode, const QString& tilesetHex)
{
    QJsonObject obj = sectionFor(gameCode).value("tilesetSizes").toObject();
    return obj.value(tilesetHex).toString("0").toInt(nullptr, 16);
}

QString enumName(const QString& enumType, int value)
{
    QJsonObject table = s_config.value("enums").toObject().value(enumType).toObject();
    QString key = QString::number(value);
    if (table.contains(key))
        return table.value(key).toString();
    // fallback: return "0" entry or the raw number if no table found
    return table.value("0").toString(QString::number(value));
}

QString romEnumName(const QString& gameCode, const QString& enumType, int value)
{
    QString key = QString::number(value);
    QJsonObject gameTable = sectionFor(gameCode).value("enums").toObject().value(enumType).toObject();
    if (gameTable.contains(key))
        return gameTable.value(key).toString();
    return enumName(enumType, value);
}
