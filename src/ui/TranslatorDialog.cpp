#include "TranslatorDialog.h"
#include "core/TranslationClient.h"
#include "core/TranslationManager.h"
#include <QGuiApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSettings>
#include <QIcon>
#include <QDebug>

TranslatorDialog::TranslatorDialog(QWidget *parent)
    : QDialog(parent), m_translator(new TranslationClient(this)) {
    setWindowTitle(TranslationManager::translatorTitle());
    resize(640, 520);

    auto *layout = new QVBoxLayout(this);

    auto *sourceLabel = new QLabel(TranslationManager::translatorSource(), this);
    layout->addWidget(sourceLabel);
    m_sourceEdit = new QTextEdit(this);
    m_sourceEdit->setPlaceholderText(TranslationManager::translatorAutoHint());
    layout->addWidget(m_sourceEdit, 1);

    auto *resultLabel = new QLabel(TranslationManager::translatorResult(), this);
    layout->addWidget(resultLabel);
    m_resultEdit = new QTextEdit(this);
    m_resultEdit->setReadOnly(true);
    layout->addWidget(m_resultEdit, 1);

    m_statusLabel = new QLabel(this);
    layout->addWidget(m_statusLabel);

    auto *optionsRow = new QHBoxLayout();
    auto *providerLabel = new QLabel(TranslationManager::translatorProvider(), this);
    m_providerCombo = new QComboBox(this);
    populateProviders();
    QSettings translationSettings(QStringLiteral("EShot"), QStringLiteral("EShot"));
    const QString provider = translationSettings.value(QStringLiteral("translation/provider"),
                                                       QStringLiteral("google_free")).toString();
    const int providerIdx = m_providerCombo->findData(provider);
    if (providerIdx >= 0) m_providerCombo->setCurrentIndex(providerIdx);
    optionsRow->addWidget(providerLabel);
    optionsRow->addWidget(m_providerCombo, 1);
    layout->addLayout(optionsRow);

    auto *btnRow = new QHBoxLayout();
    m_translateBtn = new QPushButton(TranslationManager::ocrTranslate(), this);
    m_copyResultBtn = new QPushButton(this);
    m_copyResultBtn->setIcon(QIcon(QStringLiteral(":/icons/copy.svg")));
    m_copyResultBtn->setToolTip(TranslationManager::ocrCopy());
    m_copyResultBtn->setEnabled(false);
    m_copyResultBtn->setFixedSize(32, 30);
    m_pasteBtn = new QPushButton(this);
    m_pasteBtn->setIcon(QIcon(QStringLiteral(":/icons/paste.svg")));
    m_pasteBtn->setToolTip(QStringLiteral("Paste from clipboard"));
    m_pasteBtn->setFixedSize(32, 30);
    m_closeBtn = new QPushButton(TranslationManager::ocrClose(), this);
    m_translateBtn->setFixedHeight(30);
    m_closeBtn->setFixedHeight(30);
    m_translateBtn->setStyleSheet(QStringLiteral("QPushButton { padding: 0 12px; }"));
    m_closeBtn->setStyleSheet(QStringLiteral("QPushButton { padding: 0 12px; }"));
    m_copyResultBtn->setStyleSheet(QStringLiteral("QPushButton { padding: 0 4px; }"));
    m_pasteBtn->setStyleSheet(QStringLiteral("QPushButton { padding: 0 4px; }"));
    btnRow->addWidget(m_translateBtn);
    btnRow->addWidget(m_copyResultBtn);
    btnRow->addWidget(m_pasteBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_closeBtn);
    layout->addLayout(btnRow);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(600);
    connect(m_debounce, &QTimer::timeout, this, &TranslatorDialog::startTranslation);

    connect(m_sourceEdit, &QTextEdit::textChanged, this, &TranslatorDialog::onTextChanged);
    connect(m_translateBtn, &QPushButton::clicked, this, &TranslatorDialog::onTranslateClicked);
    connect(m_copyResultBtn, &QPushButton::clicked, this, &TranslatorDialog::onCopyResultClicked);
    connect(m_pasteBtn, &QPushButton::clicked, this, &TranslatorDialog::onPasteClicked);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TranslatorDialog::onProviderChanged);

    connect(m_translator, &TranslationClient::translated,
            this, &TranslatorDialog::onTranslationReady);
    connect(m_translator, &TranslationClient::failed,
            this, &TranslatorDialog::onTranslationFailed);

    applyProviderFromCombo();
}

void TranslatorDialog::populateProviders() {
    m_providerCombo->clear();
    const QStringList ids = TranslationClient::providerIds();
    for (const QString &id : ids) {
        m_providerCombo->addItem(
            TranslationClient::providerDisplayName(TranslationClient::providerFromId(id)), id);
    }
}

void TranslatorDialog::applyProviderFromCombo() {
    const int index = m_providerCombo->currentIndex();
    if (m_translator && index >= 0) {
        m_translator->setProvider(TranslationClient::providerFromId(
            m_providerCombo->itemData(index).toString()));
    }
}

void TranslatorDialog::setSourceText(const QString &text) {
    m_sourceEdit->setPlainText(text);
    if (!text.trimmed().isEmpty())
        startTranslation();
}

void TranslatorDialog::prefillIfEmpty(const QString &text) {
    if (m_sourceEdit->toPlainText().trimmed().isEmpty() && !text.trimmed().isEmpty())
        setSourceText(text);
}

void TranslatorDialog::onTextChanged() {
    m_debounce->start();
}

void TranslatorDialog::onTranslateClicked() {
    startTranslation();
}

void TranslatorDialog::onProviderChanged(int index) {
    if (index < 0) return;
    const QString id = m_providerCombo->itemData(index).toString();
    // Сохраняем выбор — те же ключи, что редактируются в настройках.
    QSettings settings(QStringLiteral("EShot"), QStringLiteral("EShot"));
    settings.setValue(QStringLiteral("translation/provider"), id);
    applyProviderFromCombo();
    if (!m_sourceEdit->toPlainText().trimmed().isEmpty())
        startTranslation();
}

void TranslatorDialog::startTranslation() {
    const QString text = m_sourceEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        m_resultEdit->clear();
        m_copyResultBtn->setEnabled(false);
        m_statusLabel->clear();
        return;
    }
    // TranslationClient не принимает новый прогон, пока идёт текущий —
    // ставим перевод в очередь вместо ошибки "already running".
    if (m_translating) {
        m_restartPending = true;
        return;
    }
    m_translating = true;
    m_translateBtn->setEnabled(false);
    m_statusLabel->setText(TranslationManager::ocrProcessing());

    OcrTextLine line;
    line.text = text;
    QVector<OcrTextLine> lines;
    lines << line;
    m_translator->translateLines(lines);
}

void TranslatorDialog::finishTranslationRun() {
    m_translating = false;
    m_translateBtn->setEnabled(true);
    if (m_restartPending) {
        m_restartPending = false;
        startTranslation();
    }
}

void TranslatorDialog::onTranslationReady(const QVector<OcrTextLine> &lines) {
    QStringList parts;
    for (const OcrTextLine &line : lines) parts << line.text;
    m_resultEdit->setPlainText(parts.join(QLatin1Char('\n')));
    m_copyResultBtn->setEnabled(!parts.isEmpty());
    m_statusLabel->clear();
    finishTranslationRun();
}

void TranslatorDialog::onTranslationFailed(const QString &reason) {
    m_statusLabel->setText(reason.left(200));
    finishTranslationRun();
    qWarning() << "[EShot] Translator dialog: translation failed:" << reason;
}

void TranslatorDialog::onPasteClicked() {
    const QString text = QGuiApplication::clipboard()->text();
    if (!text.trimmed().isEmpty())
        setSourceText(text);
}

void TranslatorDialog::onCopyResultClicked() {
    QGuiApplication::clipboard()->setText(m_resultEdit->toPlainText());
}
