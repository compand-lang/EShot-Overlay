#ifndef OCRENGINE_H
#define OCRENGINE_H

#include <QObject>
#include <QString>
#include <QPixmap>
#include <QSet>
#include <QVector>
#include <QRect>

struct OcrTextLine {
    QRect rect;
    QString text;
};

class QProcess;

class OcrEngine : public QObject {
    Q_OBJECT

public:
    explicit OcrEngine(QObject *parent = nullptr);
    ~OcrEngine() override;

    void recognize(const QPixmap &pixmap, const QString &languageTag = "auto",
                   const QString &preferredLanguageTag = QString());
    void recognizeWithLayout(const QPixmap &pixmap, const QString &languageTag = "auto",
                             const QString &preferredLanguageTag = QString());

    static QString tesseractPath();
    static QString tessdataDir();
    static QString mapLanguageTag(const QString &bcp47);

signals:
    void textReady(const QString &text);
    void linesReady(const QVector<OcrTextLine> &lines);
    void failed(const QString &reason);
    void languageResolved(const QString &languageArgument);

private:
    void startAutomaticRecognition(const QString &imagePath, const QString &tessdataDirectory,
                                   const QString &preferredLanguage);
    void startRecognitionProcess(const QString &imagePath, const QString &tessdataDirectory,
                                 const QString &languageArgument, bool withLayout);
    void failAndRemoveImage(const QString &imagePath, const QString &reason);
    void recognizeImpl(const QPixmap &pixmap, const QString &languageTag,
                       const QString &preferredLanguageTag, bool withLayout);

    QProcess *m_proc = nullptr;
    QSet<QString> m_pendingFiles;
};

#endif
