#include "OcrTranslateController.h"
#include "TranslatedOverlayDialog.h"
#include "core/TranslationClient.h"
#include "core/TranslationManager.h"
#include <QSettings>
#include <QLabel>
#include <QMessageBox>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>

OcrTranslateController::OcrTranslateController(const QPixmap &pixmap,
                                               const QRect &sourceDisplayRect,
                                               QObject *parent)
    : QObject(parent), m_pixmap(pixmap), m_sourceDisplayRect(sourceDisplayRect) {}

void OcrTranslateController::start() {
    if (m_pixmap.isNull()) {
        finishWithError(QStringLiteral("empty image"));
        return;
    }

    QSettings settings(QStringLiteral("EShot"), QStringLiteral("EShot"));
    const QString languageTag = settings.value(QStringLiteral("ocrLanguage"),
                                               QStringLiteral("auto")).toString();
    const QString preferred = settings.value(
        QStringLiteral("ocrPreferredLanguage"), TranslationManager::langCode()).toString();

    ++m_seq;
    m_translationStarted = false;
    m_lines.clear();

    showStatus(QStringLiteral("Распознавание текста (OCR)..."));

    m_translator = new TranslationClient(this);
    connect(m_translator, &TranslationClient::translated, this,
            [this](const QVector<OcrTextLine> &lines) { showOverlay(lines); });
    connect(m_translator, &TranslationClient::failed, this,
            [this](const QString &reason) { finishWithError(reason); });

    m_engine = new OcrEngine(this);
    connect(m_engine, &OcrEngine::linesReady, this, [this](const QVector<OcrTextLine> &lines) {
        if (lines.isEmpty()) {
            finishWithError(QStringLiteral("No text recognized"));
            return;
        }
        m_lines = lines;
        m_translationStarted = true;
        showStatus(QStringLiteral("Перевод..."));
        m_translator->translateLines(m_lines);
    });
    connect(m_engine, &OcrEngine::failed, this, [this](const QString &reason) {
        finishWithError(reason);
    });
    m_engine->recognizeWithLayout(m_pixmap, languageTag, preferred);
}

void OcrTranslateController::showStatus(const QString &text) {
    if (!m_statusWindow) {
        m_statusWindow = new QLabel(nullptr, Qt::Window | Qt::FramelessWindowHint
                                                  | Qt::WindowStaysOnTopHint | Qt::Tool);
        m_statusWindow->setStyleSheet(QStringLiteral(
            "QLabel { background: #2b2b2b; color: #e8e8e8; border: 1px solid #505050;"
            " border-radius: 8px; padding: 10px 16px; font-size: 13px; }"));
        m_statusWindow->setAttribute(Qt::WA_DeleteOnClose, false);
        m_statusWindow->adjustSize();
        if (m_sourceDisplayRect.isValid()) {
            m_statusWindow->move(m_sourceDisplayRect.center() - m_statusWindow->rect().center());
        } else if (QScreen *screen = QGuiApplication::primaryScreen()) {
            m_statusWindow->move(screen->availableGeometry().center() - m_statusWindow->rect().center());
        }
        m_statusWindow->show();
    }
    m_statusWindow->setText(text);
    m_statusWindow->adjustSize();
}

void OcrTranslateController::hideStatus() {
    if (m_statusWindow) {
        m_statusWindow->hide();
        m_statusWindow->deleteLater();
        m_statusWindow = nullptr;
    }
}

void OcrTranslateController::showOverlay(const QVector<OcrTextLine> &lines) {
    hideStatus();
    // overlay живёт сам по себе; закрывается Esc/правый клик и не блокирует захват.
    TranslatedOverlayDialog::closeAll();
    auto *overlay = new TranslatedOverlayDialog(m_pixmap, lines, m_sourceDisplayRect, nullptr);
    overlay->setAttribute(Qt::WA_DeleteOnClose, true);
    if (m_sourceDisplayRect.isValid()) {
        overlay->move(m_sourceDisplayRect.topLeft());
        overlay->resize(m_sourceDisplayRect.size());
    }
    overlay->show();
    complete();
}

void OcrTranslateController::finishWithError(const QString &message) {
    hideStatus();
    qWarning() << "[EShot] OCR translate failed:" << message;
    QMessageBox::warning(nullptr, QStringLiteral("Перевод текста (OCR)"),
                         message.left(400));
    complete();
}

void OcrTranslateController::complete() {
    emit finished();
    deleteLater();
}
