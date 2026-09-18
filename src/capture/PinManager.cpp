#include "PinManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QBuffer>

static constexpr int MAX_PERSISTENT_PINS = 20;

QString PinManager::pinsFilePath()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    return dataDir + "/pinned.json";
}

static QString pixmapToBase64(const QPixmap &pixmap)
{
    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");
    return ba.toBase64();
}

static QPixmap pixmapFromBase64(const QString &base64)
{
    QByteArray ba = QByteArray::fromBase64(base64.toUtf8());
    QPixmap pixmap;
    pixmap.loadFromData(ba, "PNG");
    return pixmap;
}

void PinManager::savePin(const QPixmap &pixmap, const QPoint &pos, double scale, double opacity)
{
    QFile file(pinsFilePath());
    QJsonArray pins;

    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isObject()) pins = doc.object()["pins"].toArray();
    }

    QJsonObject pin;
    pin["image"] = pixmapToBase64(pixmap);
    pin["x"] = pos.x();
    pin["y"] = pos.y();
    pin["scale"] = scale;
    pin["opacity"] = opacity;

    pins.append(pin);

        // Enforce limit
    while (pins.size() > MAX_PERSISTENT_PINS)
        pins.removeFirst();

    QJsonObject root;
    root["pins"] = pins;

    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}

void PinManager::removePin(int index)
{
    QFile file(pinsFilePath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return;
    QJsonArray pins = doc.object()["pins"].toArray();
    if (index < 0 || index >= pins.size()) return;

    pins.removeAt(index);

    QJsonObject root;
    root["pins"] = pins;

    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}

void PinManager::clearAllPins()
{
    QFile file(pinsFilePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{"pins", QJsonArray()}}).toJson());
        file.close();
    }
}

QList<PinManager::PinData> PinManager::loadPins()
{
    QList<PinData> result;

    QFile file(pinsFilePath());
    if (!file.open(QIODevice::ReadOnly)) return result;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return result;
    QJsonArray pins = doc.object()["pins"].toArray();

    for (const auto &pinVal : pins) {
        QJsonObject pin = pinVal.toObject();
        PinData data;
        data.pixmap = pixmapFromBase64(pin["image"].toString());
        data.position = QPoint(pin["x"].toInt(), pin["y"].toInt());
        data.scale = pin["scale"].toDouble(1.0);
        data.opacity = pin["opacity"].toDouble(1.0);
        if (!data.pixmap.isNull())
            result.append(data);
    }

    return result;
}
