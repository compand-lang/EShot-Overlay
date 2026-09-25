#ifndef OCRTRANSLATECONTROLLER_H
#define OCRTRANSLATECONTROLLER_H

#include "core/OcrEngine.h"
#include <QObject>
#include <QPixmap>
#include <QRect>
#include <QVector>

class QLabel;
class TranslationClient;

// Одноразовый сценарий «OCR + перевод сразу в overlay» без окна OCR:
// recognizeWithLayout -> translateLines -> TranslatedOverlayDialog поверх выделенной области.
// Во время работы показывает лёгкое окно статуса. В любом исходе эмитит finished()
// и удаляется через deleteLater(); overlay живёт сам по себе (WA_DeleteOnClose).
class OcrTranslateController : public QObject {
    Q_OBJECT
public:
    enum class Mode {
        Translate, // OCR + перевод в overlay
        TextOnly   // только OCR, распознанный текст в overlay
    };

    OcrTranslateController(const QPixmap &pixmap, const QRect &sourceDisplayRect,
                           Mode mode = Mode::Translate, QObject *parent = nullptr);

    void start();

signals:
    void finished();

private:
    void showStatus(const QString &text);
    void hideStatus();
    void showOverlay(const QVector<OcrTextLine> &lines);
    void finishWithError(const QString &message);
    void complete();

    QPixmap m_pixmap;
    QRect m_sourceDisplayRect;
    Mode m_mode = Mode::Translate;
    OcrEngine *m_engine = nullptr;
    TranslationClient *m_translator = nullptr;
    QLabel *m_statusWindow = nullptr;
    QVector<OcrTextLine> m_lines;
    int m_seq = 0;
    bool m_translationStarted = false;
};

#endif
