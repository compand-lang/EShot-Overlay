#ifndef PINMANAGER_H
#define PINMANAGER_H

#include <QString>
#include <QPixmap>
#include <QPoint>
#include <QJsonObject>
#include <QJsonArray>

class PinManager {
public:
    static QString pinsFilePath();

    static void savePin(const QPixmap &pixmap, const QPoint &pos, double scale, double opacity);
    static void removePin(int index);
    static void clearAllPins();

    struct PinData {
        QPixmap pixmap;
        QPoint position;
        double scale;
        double opacity;
    };

    static QList<PinData> loadPins();
};

#endif
