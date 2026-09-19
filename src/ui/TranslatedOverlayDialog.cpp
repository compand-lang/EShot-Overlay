#include "TranslatedOverlayDialog.h"
#include <QGuiApplication>
#include <QClipboard>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include <QDebug>

TranslatedOverlayDialog::TranslatedOverlayDialog(const QPixmap &source,
                                                 const QVector<OcrTextLine> &translatedLines,
                                                 const QRect &targetDisplayRect,
                                                 QWidget *parent)
    : QDialog(parent), m_source(source), m_lines(translatedLines), m_targetDisplayRect(targetDisplayRect) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose, false); // parent owns lifetime unless explicitly set by caller
    setModal(false);
    setWindowTitle(QStringLiteral("EShot translation overlay"));

    m_imageLabel = new QLabel(this);
    m_imageLabel->setPixmap(renderOverlay());
    m_imageLabel->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_imageLabel, &QLabel::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        QAction *copy = menu.addAction(QStringLiteral("Copy translation"));
        QAction *closeAction = menu.addAction(QStringLiteral("Close"));
        QAction *chosen = menu.exec(m_imageLabel->mapToGlobal(pos));
        if (chosen == copy) {
            QStringList text;
            for (const OcrTextLine &line : m_lines) text << line.text;
            QGuiApplication::clipboard()->setText(text.join(QLatin1Char('\n')));
        } else if (chosen == closeAction) {
            close();
        }
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_imageLabel);
    resize(m_imageLabel->pixmap().size());
}

void TranslatedOverlayDialog::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QDialog::keyPressEvent(event);
}

QPixmap TranslatedOverlayDialog::renderOverlay() const {
    if (m_source.isNull()) return QPixmap();

    // Координаты Tesseract в пикселях исходного снимка. Если известен целевой
    // экранный rect, масштабируем подписи из снимка в экранные координаты.
    const bool hasTarget = m_targetDisplayRect.isValid();
    const QSize outSize = hasTarget ? m_targetDisplayRect.size() : m_source.size();
    const double sx = hasTarget ? (double(outSize.width()) / double(qMax(1, m_source.width()))) : 1.0;
    const double sy = hasTarget ? (double(outSize.height()) / double(qMax(1, m_source.height()))) : 1.0;

    QPixmap out(outSize);
    out.fill(QColor(255, 255, 255, 255));

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    if (hasTarget) {
        p.drawPixmap(QRectF(0, 0, outSize.width(), outSize.height()), m_source, QRectF(0, 0, m_source.width(), m_source.height()));
    } else {
        p.drawPixmap(0, 0, m_source);
    }

    for (const OcrTextLine &line : m_lines) {
        QRect r = line.rect.normalized().adjusted(-2, -2, 2, 2);
        if (hasTarget) {
            r = QRect(QPoint(qRound(r.x() * sx), qRound(r.y() * sy)),
                      QSize(qMax(1, qRound(r.width() * sx)), qMax(1, qRound(r.height() * sy))));
        }
        if (r.isEmpty()) r = QRect(0, 0, out.width(), out.height());
        r = r.intersected(QRect(0, 0, out.width(), out.height()));
        if (r.isEmpty()) continue;

        p.fillRect(r, QColor(255, 255, 255, 235));

        QString text = line.text.trimmed();
        if (text.isEmpty()) continue;

        QFont font = p.font();
        int size = qMax(8, qMin(r.height() - 4, 24));
        font.setPointSize(size);
        p.setFont(font);
        p.setPen(QColor(20, 20, 20));

        QRectF textRect = p.boundingRect(r.adjusted(4, 2, -4, -2), Qt::TextWordWrap, text);
        if (textRect.height() > r.height() - 4 || textRect.width() > r.width() - 8) {
            for (int s = size; s >= 7; --s) {
                font.setPointSize(s);
                p.setFont(font);
                textRect = p.boundingRect(r.adjusted(4, 2, -4, -2), Qt::TextWordWrap, text);
                if (textRect.height() <= r.height() - 4 && textRect.width() <= r.width() - 8) break;
            }
        }
        p.drawText(r.adjusted(4, 2, -4, -2), Qt::TextWordWrap, text);
    }
    return out;
}
