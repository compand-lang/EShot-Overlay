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
    QPixmap source(240, 60);
    source.fill(Qt::white);

    OcrTextLine first;
    first.rect = QRect(4, 4, 230, 14);
    first.text = QStringLiteral("Очень длинный русский перевод, который обязан целиком помещаться в исходной рамке");
    OcrTextLine second;
    second.rect = QRect(4, 24, 230, 14);
    second.text = QStringLiteral("Вторая OCR-строка должна продолжить тот же текст без абзацного отступа");

    TranslatedOverlayDialog dialog(source, {first, second}, QRect(0, 0, 240, 60));
    QCOMPARE(dialog.width(), 240);
    QCOMPARE(dialog.height(), 60); // рамка не растёт и не закрывает панель инструментов

    const auto edits = dialog.findChildren<QTextEdit *>();
    QCOMPARE(edits.size(), 1);
    QTextEdit *edit = edits.first();
    QCOMPARE(edit->font().weight(), QFont::DemiBold);
    QVERIFY(!edit->toPlainText().contains(QLatin1Char('\n')));
    QVERIFY(edit->toPlainText().contains(QStringLiteral("рамке Вторая")));

    edit->document()->setTextWidth(edit->width());
    QVERIFY(edit->document()->documentLayout()->documentSize().height() <= edit->height() + 1);
    QVERIFY(edit->x() >= 0 && edit->x() + edit->width() <= dialog.width());
    QVERIFY(edit->y() >= 0 && edit->y() + edit->height() <= dialog.height());
}

QTEST_MAIN(TranslatedOverlayDialogTests)

#include "TranslatedOverlayDialogTests.moc"
