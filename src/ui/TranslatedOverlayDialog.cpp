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
#include <QTextOption>
#include <QAbstractTextDocumentLayout>
#include <algorithm>

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

    // Оверлей всегда остаётся в пределах исходной выделенной области.
    // OCR-строки склеиваются в один непрерывный текст, а общая рамка
    // объединяет их геометрию без пустых межабзацных промежутков.
    const bool hasTarget = m_targetDisplayRect.isValid();
    const QSize baseSize = hasTarget ? m_targetDisplayRect.size() : m_source.size();
    const double sx = hasTarget ? (double(baseSize.width()) / double(qMax(1, m_source.width()))) : 1.0;
    const double sy = hasTarget ? (double(baseSize.height()) / double(qMax(1, m_source.height()))) : 1.0;
    const QRect bounds(QPoint(0, 0), baseSize);

    QVector<int> order;
    order.reserve(m_lines.size());
    for (int i = 0; i < m_lines.size(); ++i) order.append(i);
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        if (m_lines[a].rect.top() != m_lines[b].rect.top())
            return m_lines[a].rect.top() < m_lines[b].rect.top();
        return m_lines[a].rect.left() < m_lines[b].rect.left();
    });

    QStringList parts;
    int preferredSize = 11;
    for (int i : order) {
        const QString text = m_lines[i].text.simplified();
        if (text.isEmpty()) continue;
        parts << text;

        QRect r = m_lines[i].rect.normalized().adjusted(-2, -2, 2, 2);
        if (hasTarget) {
            r = QRect(QPoint(qRound(r.x() * sx), qRound(r.y() * sy)),
                      QSize(qMax(1, qRound(r.width() * sx)), qMax(1, qRound(r.height() * sy))));
        }
        if (r.isEmpty()) r = bounds;
        r = r.intersected(bounds);
        if (r.isEmpty()) continue;

        m_textRect = m_textRect.isNull() ? r : m_textRect.united(r);
        preferredSize = qMax(preferredSize, qBound(8, r.height() - 4, 28));
    }
    m_text = parts.join(QLatin1Char(' '));
    if (!m_text.isEmpty()) {
        if (m_textRect.isEmpty()) m_textRect = bounds;
        const QRect editRect = m_textRect.adjusted(4, 2, -4, -2);
        m_textFont = fitFontToRect(editRect.size(), m_text, preferredSize, font());
    }

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

    // Один редактор на весь текст: нет отдельных абзацев и разрывов между
    // OCR-строками. Текст по-прежнему можно выделять и копировать.
    if (!m_text.isEmpty() && m_textRect.isValid())
        addTextEditForLine(m_textRect.adjusted(4, 2, -4, -2), m_text, m_textFont);

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
    QGuiApplication::clipboard()->setText(m_text);
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
    edit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    edit->setPlainText(text);
    edit->move(r.topLeft());
    edit->resize(r.size());
    edit->installEventFilter(this);
    edit->show();
    m_textEdits.append(edit);
}

QFont TranslatedOverlayDialog::fitFontToRect(const QSize &size, const QString &text,
                                             int preferredSize, QFont base) const {
    base.setWeight(QFont::DemiBold);
    const int firstSize = qBound(6, preferredSize, 28);
    for (int pointSize = firstSize; pointSize >= 6; --pointSize) {
        base.setPointSize(pointSize);
        QTextDocument doc;
        doc.setDocumentMargin(0);
        doc.setDefaultFont(base);
        QTextOption option = doc.defaultTextOption();
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        doc.setDefaultTextOption(option);
        doc.setPlainText(text);
        doc.setTextWidth(qMax(1, size.width()));
        const int textHeight = qCeil(doc.documentLayout()->documentSize().height());
        if (textHeight <= size.height() + 1) return base;
    }
    return base;
}

QPixmap TranslatedOverlayDialog::renderOverlay() const {
    if (m_source.isNull()) return QPixmap();

    const bool hasTarget = m_targetDisplayRect.isValid();
    const QSize baseSize = hasTarget ? m_targetDisplayRect.size() : m_source.size();
    QPixmap out(baseSize);
    out.fill(QColor(28, 28, 28, 255));

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.drawPixmap(QRectF(0, 0, baseSize.width(), baseSize.height()), m_source,
                 QRectF(0, 0, m_source.width(), m_source.height()));

    // Одна полупрозрачная плашка строго в исходной области. Сам текст
    // показывает QTextEdit поверх, чтобы его можно было выделять и копировать.
    if (m_textRect.isValid() && !m_textRect.isEmpty())
        p.fillRect(m_textRect, QColor(28, 28, 28, 204));
    return out;
}
