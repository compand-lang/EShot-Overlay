#ifndef OCRDIALOG_H
#define OCRDIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QString>
#include <QPixmap>
#include <QRect>
#include <QVector>
#include "core/OcrEngine.h"

class OcrEngine;
class TranslationClient;

class OcrDialog : public QDialog {
    Q_OBJECT

public:
    explicit OcrDialog(const QPixmap &pixmap, const QRect &sourceDisplayRect = QRect(), QWidget *parent = nullptr);
    ~OcrDialog();

    void setLanguageTag(const QString &tag);

private slots:
    void onTextReady(const QString &text);
    void onOcrFailed(const QString &reason);
    void onCopyClicked();
    void onOverlayTranslateClicked();
    void onRetryClicked();
    void onLanguageChanged(int index);
    void onLinesReady(const QVector<OcrTextLine> &lines);
    void onTranslationReady(const QVector<OcrTextLine> &lines);
    void onTranslationFailed(const QString &reason);

private:
    void setBusy(bool busy);
    void translateUi();
    void runOcr();
    void populateLanguages();
    bool isLanguageInstalled(const QString &tag) const;
    int firstInstalledLanguageIndex() const;
    // Перевод распознанного текста прямо в нижней части окна (без внешнего сайта).
    void startInPlaceTranslation();

    QPixmap m_pixmap;
    OcrEngine *m_engine;
    QString m_languageTag = "auto";
    QString m_preferredLanguageTag = "en";

    QComboBox *m_langCombo;
    QLabel *m_statusLabel;
    QTextEdit *m_textEdit;
    QTextEdit *m_translatedEdit;
    QPushButton *m_copyBtn;
    QPushButton *m_overlayBtn;
    QComboBox *m_providerCombo = nullptr;
    TranslationClient *m_translator = nullptr;
    QVector<OcrTextLine> m_lines;
    QRect m_sourceDisplayRect;
    int m_ocrSeq = 0;
    int m_translateSeq = -1;
    bool m_wantOverlay = false; // overlay создаём только по кнопке, а не при авто-переводе
    QPushButton *m_retryBtn;
    QPushButton *m_closeBtn;
};

#endif
