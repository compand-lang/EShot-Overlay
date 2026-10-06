#include "TranslatedOverlayDialog.h"
#include <QGuiApplication>
#include <QClipboard>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QAction>
#include <QTextEdit>
#include <QTextDocument>
#include <QFrame>
#include <QResizeEvent>
#include <QToolButton>
#include <QIcon>
#include <QDebug>

QSet<TranslatedOverlayDialog *> TranslatedOverlayDialog::s_liveOverlays;

TranslatedOverlayDialog::TranslatedOverlayDialog(const QPixmap &source,
                                                 const QVector<OcrTextLine> &translatedLines,
                                                 const QRect &targetDisplayRect,
                                                 QWidget *parent)
    : QDialog(parent), m_source(source), m_lines(translatedLines), m_targetDisplayRect(targetDisplayRect) {
    s_liveOverlays.insert(this);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose, false); // parent owns lifetime unless explicitly set by caller
    setModal(false);
    setWindowTitle(QStringLiteral("EShot translation overlay"));

    m_imageLabel = new QLabel(this);
    m_imageLabel->setPixmap(renderOverlay());
    m_imageLabel->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_imageLabel, &QLabel::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        QAction *copy = menu.addAction(QStringLiteral("Copy text"));
        QAction *closeAction = menu.addAction(QStringLiteral("Close"));
        QAction *chosen = menu.exec(m_imageLabel->mapToGlobal(pos));
        if (chosen == copy) {
            copyAllText();
        } else if (chosen == closeAction) {
            close();
        }
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_imageLabel);
    resize(m_imageLabel->pixmap().size());

    // Настоящий текст поверх плашек: можно выделить мышкой, Ctrl+C копирует.
    const bool hasTarget = m_targetDisplayRect.isValid();
    const double sx = hasTarget ? (double(width()) / double(qMax(1, m_source.width()))) : 1.0;
    const double sy = hasTarget ? (double(height()) / double(qMax(1, m_source.height()))) : 1.0;
    for (const OcrTextLine &line : m_lines) {
        QString text = line.text.trimmed();
        if (text.isEmpty()) continue;

        QRect r = line.rect.normalized().adjusted(-2, -2, 2, 2);
        if (hasTarget) {
            r = QRect(QPoint(qRound(r.x() * sx), qRound(r.y() * sy)),
                      QSize(qMax(1, qRound(r.width() * sx)), qMax(1, qRound(r.height() * sy))));
        }
        if (r.isEmpty()) r = QRect(0, 0, width(), height());
        r = r.intersected(QRect(0, 0, width(), height()));
        if (r.isEmpty()) continue;

        addTextEditForLine(r.adjusted(4, 2, -4, -2), text, fontForLine(r, text, font()));
    }

    // Кнопка «копировать весь текст» в правом верхнем углу оверлея.
    m_copyButton = new QToolButton(m_imageLabel);
    m_copyButton->setIcon(QIcon(QStringLiteral(":/icons/copy.svg")));
    m_copyButton->setIconSize(QSize(18, 18));
    m_copyButton->setFixedSize(30, 30);
    m_copyButton->setAutoRaise(false);
    m_copyButton->setCursor(Qt::ArrowCursor);
    m_copyButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: rgba(43,43,43,220); border: 1px solid #505050;"
        " border-radius: 6px; }"
        "QToolButton:hover { background: rgba(70,70,70,230); }"));
    m_copyButton->setToolTip(QStringLiteral("Copy text"));
    connect(m_copyButton, &QToolButton::clicked, this, [this]() { copyAllText(); });
    placeCopyButton();
}

void TranslatedOverlayDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    placeCopyButton();
}

void TranslatedOverlayDialog::placeCopyButton() {
    if (!m_copyButton || !m_imageLabel) return;
    m_copyButton->move(qMax(2, m_imageLabel->width() - m_copyButton->width() - 2), 2);
    m_copyButton->raise();
}

TranslatedOverlayDialog::~TranslatedOverlayDialog() {
    s_liveOverlays.remove(this);
}

void TranslatedOverlayDialog::closeAll() {
    const QSet<TranslatedOverlayDialog *> live = s_liveOverlays;
    for (TranslatedOverlayDialog *overlay : live) {
        if (overlay)
            overlay->close();
    }
}

void TranslatedOverlayDialog::copyAllText() const {
    QStringList text;
    for (const OcrTextLine &line : m_lines) text << line.text;
    QGuiApplication::clipboard()->setText(text.join(QLatin1Char('\n')));
}

void TranslatedOverlayDialog::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        copyAllText();
        return;
    }
    QDialog::keyPressEvent(event);
}

bool TranslatedOverlayDialog::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->matches(QKeySequence::Copy)) {
            auto *edit = qobject_cast<QTextEdit *>(watched);
            // Если в редакторе есть выделение — стандартное копирование выделения,
            // иначе копируем весь текст оверлея.
            if (edit && !edit->textCursor().hasSelection()) {
                copyAllText();
                return true;
            }
        }
        if (keyEvent->key() == Qt::Key_Escape) {
            close();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void TranslatedOverlayDialog::addTextEditForLine(const QRect &r, const QString &text, const QFont &font) {
    auto *edit = new QTextEdit(m_imageLabel);
    edit->setReadOnly(true);
    edit->setFrameStyle(QFrame::NoFrame);
    edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit->setStyleSheet(QStringLiteral(
        "QTextEdit { background: transparent; color: #f5f5f5; border: none; }"));
    if (QTextDocument *doc = edit->document())
        doc->setDocumentMargin(0);
    edit->setFont(font);
    edit->setPlainText(text);
    edit->move(r.topLeft());
    edit->resize(r.size());
    edit->installEventFilter(this);
    edit->show();
    m_textEdits.append(edit);
}

QFont TranslatedOverlayDialog::fontForLine(const QRect &r, const QString &text, QFont base) const {
    // Русский/длинный текст оборачивается — не даём шрифту ужиматься слишком сильно.
    int size = qMax(12, qMin(r.height() - 4, 32));
    base.setPointSize(size);
    base.setBold(true);

    QPixmap dummy(1, 1);
    QPainter p(&dummy);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    QRect box = r.adjusted(4, 2, -4, -2);
    p.setFont(base); // иначе первое измерение идёт шрифтом по умолчанию
    QRectF textRect = p.boundingRect(box, Qt::TextWordWrap, text);
    while ((textRect.height() > box.height() || textRect.width() > box.width()) && size > 12) {
        --size;
        base.setPointSize(size);
        p.setFont(base);
        textRect = p.boundingRect(box, Qt::TextWordWrap, text);
    }
    return base;
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

    // Рисуем только тёмные плашки — сам текст показывают QTextEdit поверх,
    // чтобы его можно было выделять и копировать.
    for (const OcrTextLine &line : m_lines) {
        QRect r = line.rect.normalized().adjusted(-2, -2, 2, 2);
        if (hasTarget) {
            r = QRect(QPoint(qRound(r.x() * sx), qRound(r.y() * sy)),
                      QSize(qMax(1, qRound(r.width() * sx)), qMax(1, qRound(r.height() * sy))));
        }
        if (r.isEmpty()) r = QRect(0, 0, out.width(), out.height());
        r = r.intersected(QRect(0, 0, out.width(), out.height()));
        if (r.isEmpty()) continue;

        p.fillRect(r, QColor(28, 28, 28, 204));
    }
    return out;
}
