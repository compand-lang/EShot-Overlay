#include <QtTest/QtTest>

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

QTEST_MAIN(TranslatedOverlayDialogTests)

#include "TranslatedOverlayDialogTests.moc"
