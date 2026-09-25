#ifndef TRANSLATEDOVERLAYDIALOG_H
#define TRANSLATEDOVERLAYDIALOG_H

#include "core/OcrEngine.h"
#include <QDialog>
#include <QVector>
#include <QPixmap>
#include <QRect>
#include <QSet>
#include <QPointer>

class QLabel;
class QTextEdit;
class QToolButton;

class TranslatedOverlayDialog : public QDialog {
    Q_OBJECT
public:
    TranslatedOverlayDialog(const QPixmap &source,
                            const QVector<OcrTextLine> &translatedLines,
                            const QRect &targetDisplayRect = QRect(),
                            QWidget *parent = nullptr);
    ~TranslatedOverlayDialog() override;

    // Закрывает все живые overlay-окна (например, перед новым OCR или новым переводом),
    // чтобы старый перевод не оставался поверх экрана.
    static void closeAll();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QPixmap renderOverlay() const;
    QFont fontForLine(const QRect &r, const QString &text, QFont base) const;
    void copyAllText() const;
    void addTextEditForLine(const QRect &r, const QString &text, const QFont &font);
    void placeCopyButton();

    QPixmap m_source;
    QVector<OcrTextLine> m_lines;
    QRect m_targetDisplayRect;
    QLabel *m_imageLabel = nullptr;
    QToolButton *m_copyButton = nullptr;
    QList<QPointer<QTextEdit>> m_textEdits;

    static QSet<TranslatedOverlayDialog *> s_liveOverlays;
};

#endif
