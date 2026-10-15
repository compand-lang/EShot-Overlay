#ifndef TRANSLATORDIALOG_H
#define TRANSLATORDIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QString>
#include <QTimer>
#include <QVector>
#include "core/OcrEngine.h"

class TranslationClient;

// Окно-переводчик: редактируемый исходный текст сверху, перевод снизу.
// Перевод запускается автоматически (debounce) и вручную; провайдер
// выбирается комбобоксом и сохраняется в QSettings (те же ключи, что в настройках).
class TranslatorDialog : public QDialog {
    Q_OBJECT

public:
    explicit TranslatorDialog(QWidget *parent = nullptr);

    // Подставить текст (например, из буфера обмена) и сразу перевести.
    void setSourceText(const QString &text);
    // Подставить текст, только если поле ввода пустое (для повторного открытия по hotkey).
    void prefillIfEmpty(const QString &text);

private slots:
    void onTranslateClicked();
    void onCopyResultClicked();
    void onPasteClicked();
    void onProviderChanged(int index);
    void onTextChanged();
    void onTranslationReady(const QVector<OcrTextLine> &lines);
    void onTranslationFailed(const QString &reason);

private:
    void startTranslation();
    void finishTranslationRun();
    void applyProviderFromCombo();
    void populateProviders();

    QTextEdit *m_sourceEdit;
    QTextEdit *m_resultEdit;
    QLabel *m_statusLabel;
    QComboBox *m_providerCombo;
    QPushButton *m_translateBtn;
    QPushButton *m_copyResultBtn;
    QPushButton *m_pasteBtn;
    QPushButton *m_closeBtn;
    TranslationClient *m_translator;
    QTimer *m_debounce;
    bool m_translating = false;
    bool m_restartPending = false;
};

#endif
