#include <QtTest/QtTest>
#include <QTextEdit>
#include <QAbstractTextDocumentLayout>
#include <algorithm>

#include "ui/TranslatedOverlayDialog.h"

// Геометрия overlay и управление жизненным циклом (closeAll).

class TranslatedOverlayDialogTests : public QObject
{
    Q_OBJECT

private slots:
    void scalesToTargetDisplayRect();
    void keepsSourceSizeWithoutTargetRect();
    void closeAllClosesLiveOverlays();
    void closedOverlayIsRemovedFromLiveSet();
    void wrapsLongTextWithoutClipping();
    void keepsIndependentColumnsSideBySide();
};

namespace {

QPixmap makeSource()
{
    QPixmap pixmap(200, 100);
    pixmap.fill(Qt::white);
    return pixmap;
}

QVector<OcrTextLine> makeLines()
{
    QVector<OcrTextLine> lines;
    OcrTextLine line;
    line.rect = QRect(10, 10, 100, 20);
    line.text = QStringLiteral("Hello");
    lines << line;
    return lines;
}

} // namespace

void TranslatedOverlayDialogTests::scalesToTargetDisplayRect()
{
    TranslatedOverlayDialog dialog(makeSource(), makeLines(), QRect(300, 200, 400, 200));
    QCOMPARE(dialog.width(), 400);
    QCOMPARE(dialog.height(), 200);
}

void TranslatedOverlayDialogTests::keepsSourceSizeWithoutTargetRect()
{
    TranslatedOverlayDialog dialog(makeSource(), makeLines());
    QCOMPARE(dialog.width(), 200);
    QCOMPARE(dialog.height(), 100);
}

void TranslatedOverlayDialogTests::closeAllClosesLiveOverlays()
{
    auto *first = new TranslatedOverlayDialog(makeSource(), makeLines(), QRect(0, 0, 300, 150));
    auto *second = new TranslatedOverlayDialog(makeSource(), makeLines(), QRect(0, 0, 300, 150));
    first->show();
    second->show();
    QVERIFY(first->isVisible());
    QVERIFY(second->isVisible());

    TranslatedOverlayDialog::closeAll();

    QTRY_VERIFY(!first->isVisible());
    QTRY_VERIFY(!second->isVisible());
    delete first;
    delete second;
}

void TranslatedOverlayDialogTests::closedOverlayIsRemovedFromLiveSet()
{
    auto *dialog = new TranslatedOverlayDialog(makeSource(), makeLines());
    dialog->show();
    dialog->close();
    QVERIFY(!dialog->isVisible());

    // closeAll после закрытия не должен упасть и не должен затронуть другие окна.
    TranslatedOverlayDialog::closeAll();
    delete dialog;
}

void TranslatedOverlayDialogTests::wrapsLongTextWithoutClipping()
{
    QPixmap source(120, 60);
    source.fill(Qt::white);

    OcrTextLine first;
    first.rect = QRect(4, 4, 52, 14);
    first.text = QStringLiteral("Очень длинный русский перевод, который обязан целиком помещаться в узкой колонке");
    OcrTextLine second;
    second.rect = QRect(4, 24, 52, 14);
    second.text = QStringLiteral("И вторая строка тоже должна остаться полностью видимой без горизонтальной прокрутки");

    TranslatedOverlayDialog dialog(source, {first, second}, QRect(0, 0, 120, 60));
    QVERIFY(dialog.height() > 60); // блоки выросли вниз, а не обрезали текст

    const auto edits = dialog.findChildren<QTextEdit *>();
    QCOMPARE(edits.size(), 2);

    QList<QRect> rects;
    for (QTextEdit *edit : edits) {
        QCOMPARE(edit->font().weight(), QFont::DemiBold);
        edit->document()->setTextWidth(edit->width());
        QVERIFY(edit->document()->documentLayout()->documentSize().height() <= edit->height() + 1);
        QVERIFY(edit->x() >= 0 && edit->x() + edit->width() <= dialog.width());
        rects << QRect(edit->pos(), edit->size());
    }
    std::sort(rects.begin(), rects.end(), [](const QRect &a, const QRect &b) { return a.y() < b.y(); });
    QVERIFY(!rects[0].intersects(rects[1]));
}

void TranslatedOverlayDialogTests::keepsIndependentColumnsSideBySide()
{
    QPixmap source(120, 60);
    source.fill(Qt::white);

    OcrTextLine left;
    left.rect = QRect(4, 4, 52, 14);
    left.text = QStringLiteral("Длинный текст в левой колонке, который переносится на несколько строк");
    OcrTextLine right;
    right.rect = QRect(64, 4, 52, 14);
    right.text = QStringLiteral("Правая колонка");

    TranslatedOverlayDialog dialog(source, {left, right}, QRect(0, 0, 120, 60));
    const auto edits = dialog.findChildren<QTextEdit *>();
    QCOMPARE(edits.size(), 2);
    QCOMPARE(edits[0]->y(), edits[1]->y());
}

QTEST_MAIN(TranslatedOverlayDialogTests)

#include "TranslatedOverlayDialogTests.moc"
