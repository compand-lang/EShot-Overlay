#ifndef TRANSLATEDOVERLAYDIALOG_H
#define TRANSLATEDOVERLAYDIALOG_H

#include "core/OcrEngine.h"
#include <QDialog>
#include <QVector>
#include <QPixmap>
#include <QRect>

class QLabel;

class TranslatedOverlayDialog : public QDialog {
    Q_OBJECT
public:
    TranslatedOverlayDialog(const QPixmap &source,
                            const QVector<OcrTextLine> &translatedLines,
                            const QRect &targetDisplayRect = QRect(),
                            QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    QPixmap renderOverlay() const;

    QPixmap m_source;
    QVector<OcrTextLine> m_lines;
    QRect m_targetDisplayRect;
    QLabel *m_imageLabel = nullptr;
};

#endif
