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

    // Сначала раскладываем блоки: текст переносится по ширине исходной
    // OCR-рамки, высота блока вычисляется по реальному документу. Если
    // перевод длиннее оригинала, блок растёт вниз, а следующие блоки
    // сдвигаются, чтобы не перекрываться.
    const bool hasTarget = m_targetDisplayRect.isValid();
    const QSize baseSize = hasTarget ? m_targetDisplayRect.size() : m_source.size();
    const double sx = hasTarget ? (double(baseSize.width()) / double(qMax(1, m_source.width()))) : 1.0;
    const double sy = hasTarget ? (double(baseSize.height()) / double(qMax(1, m_source.height()))) : 1.0;

    m_lineRects.fill(QRect(), m_lines.size());
    m_lineFonts.fill(QFont(), m_lines.size());
    QVector<int> order;
    order.reserve(m_lines.size());
    for (int i = 0; i < m_lines.size(); ++i) order.append(i);
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        if (m_lines[a].rect.top() != m_lines[b].rect.top())
            return m_lines[a].rect.top() < m_lines[b].rect.top();
        return m_lines[a].rect.left() < m_lines[b].rect.left();
    });

    int contentBottom = baseSize.height();
    for (int i : order) {
        const QString text = m_lines[i].text.trimmed();
        if (text.isEmpty()) continue;

        QRect r = m_lines[i].rect.normalized().adjusted(-2, -2, 2, 2);
        if (hasTarget) {
            r = QRect(QPoint(qRound(r.x() * sx), qRound(r.y() * sy)),
                      QSize(qMax(1, qRound(r.width() * sx)), qMax(1, qRound(r.height() * sy))));
        }
        if (r.isEmpty()) r = QRect(0, 0, baseSize.width(), baseSize.height());

        const int x = qBound(0, r.x(), qMax(0, baseSize.width() - 40));
        int w = qMax(40, r.width());
        if (x + w > baseSize.width()) w = baseSize.width() - x;
        w = qMax(24, w);

        const QFont lineFont = fontForLine(r, text, font());
        QTextDocument doc;
        doc.setDocumentMargin(0);
        doc.setDefaultFont(lineFont);
        QTextOption option = doc.defaultTextOption();
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        doc.setDefaultTextOption(option);
        doc.setPlainText(text);
        doc.setTextWidth(qMax(16, w - 8));
        const int textHeight = qCeil(doc.documentLayout()->documentSize().height());

        const int blockHeight = qMax(r.height(), textHeight + 4);
        int y = qMax(0, r.y());
        // Сдвигаем блок только под теми предыдущими блоками, с которыми
        // он действительно пересекается по горизонтали. Независимые
        // колонки остаются рядом, а не выстраиваются в одну ленту.
        for (int previous : order) {
            if (previous == i) break;
            const QRect &placed = m_lineRects[previous];
            if (!placed.isValid()) continue;
            const bool overlapsX = x <= placed.right() && placed.x() < x + w;
            if (overlapsX) y = qMax(y, placed.bottom() + 3);
        }
        m_lineRects[i] = QRect(x, y, w, blockHeight);
        m_lineFonts[i] = lineFont;
        contentBottom = qMax(contentBottom, y + blockHeight);
    }
    m_outputSize = QSize(baseSize.width(), contentBottom);

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
    for (int i = 0; i < m_lines.size(); ++i) {
        const QString text = m_lines[i].text.trimmed();
        if (text.isEmpty() || !m_lineRects[i].isValid()) continue;
        addTextEditForLine(m_lineRects[i].adjusted(4, 2, -4, -2), text, m_lineFonts[i]);
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
    edit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    edit->setPlainText(text);
    edit->move(r.topLeft());
    edit->resize(r.size());
    edit->installEventFilter(this);
    edit->show();
    m_textEdits.append(edit);
}

QFont TranslatedOverlayDialog::fontForLine(const QRect &r, const QString &text, QFont base) const {
    Q_UNUSED(text);
    // Размер берём из высоты исходной строки, но не ужимаем длинный перевод:
    // перенос строк решает проблему ширины, а высота блока считается отдельно.
    const int size = qBound(11, r.height() - 4, 28);
    base.setPointSize(size);
    base.setWeight(QFont::DemiBold); // читаемо, но не так тяжело, как Bold
    return base;
}

QPixmap TranslatedOverlayDialog::renderOverlay() const {
    if (m_source.isNull()) return QPixmap();

    // Исходный снимок остаётся наверху, область ниже него (если длинный
    // перевод раздвинул блоки) заливается тем же тёмным цветом.
    const bool hasTarget = m_targetDisplayRect.isValid();
    const QSize baseSize = hasTarget ? m_targetDisplayRect.size() : m_source.size();
    const QSize outSize = m_outputSize.isValid() ? m_outputSize : baseSize;

    QPixmap out(outSize);
    out.fill(QColor(28, 28, 28, 255));

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.drawPixmap(QRectF(0, 0, baseSize.width(), baseSize.height()), m_source,
                 QRectF(0, 0, m_source.width(), m_source.height()));

    // Рисуем только тёмные плашки — сам текст показывают QTextEdit поверх,
    // чтобы его можно было выделять и копировать. Плашки уже имеют высоту,
    // достаточную для многострочного перенесённого текста.
    for (const QRect &r : m_lineRects) {
        if (r.isValid() && !r.isEmpty())
            p.fillRect(r, QColor(28, 28, 28, 204));
    }
    return out;
}
