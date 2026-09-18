#ifndef TRANSLATIONCLIENT_H
#define TRANSLATIONCLIENT_H

#include "core/OcrEngine.h"
#include <QObject>
#include <QVector>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

class TranslationClient : public QObject {
    Q_OBJECT
public:
    enum class Provider { Google, Yandex };
    explicit TranslationClient(QObject *parent = nullptr);

    void setProvider(Provider provider);
    void setTargetLanguage(const QString &bcp47);
    Provider provider() const;
    QString targetLanguage() const;

    void translateLines(const QVector<OcrTextLine> &lines);

signals:
    void translated(const QVector<OcrTextLine> &lines);
    void failed(const QString &reason);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    void startNext();
    QString endpointFor(const QString &text) const;
    QString parseReply(const QByteArray &body, QString *error) const;

    QNetworkAccessManager m_net;
    Provider m_provider = Provider::Google;
    QString m_targetLanguage = QStringLiteral("ru");
    QVector<OcrTextLine> m_input;
    QVector<OcrTextLine> m_output;
    int m_next = 0;
    bool m_running = false;
};

#endif
