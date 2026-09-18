#include "TranslationClient.h"
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QDebug>

TranslationClient::TranslationClient(QObject *parent) : QObject(parent) {
    QSettings settings(QStringLiteral("EShot"), QStringLiteral("EShot"));
    m_provider = settings.value(QStringLiteral("translation/provider"), QStringLiteral("google")).toString()
                 == QStringLiteral("yandex") ? Provider::Yandex : Provider::Google;
    m_targetLanguage = settings.value(QStringLiteral("translation/targetLanguage"), QStringLiteral("ru")).toString();
    if (m_targetLanguage.trimmed().isEmpty()) m_targetLanguage = QStringLiteral("ru");
}

void TranslationClient::setProvider(Provider provider) {
    m_provider = provider;
    QSettings(QStringLiteral("EShot"), QStringLiteral("EShot"))
        .setValue(QStringLiteral("translation/provider"),
                  provider == Provider::Yandex ? QStringLiteral("yandex") : QStringLiteral("google"));
}

void TranslationClient::setTargetLanguage(const QString &bcp47) {
    m_targetLanguage = bcp47.trimmed().isEmpty() ? QStringLiteral("ru") : bcp47;
    QSettings(QStringLiteral("EShot"), QStringLiteral("EShot"))
        .setValue(QStringLiteral("translation/targetLanguage"), m_targetLanguage);
}

TranslationClient::Provider TranslationClient::provider() const { return m_provider; }
QString TranslationClient::targetLanguage() const { return m_targetLanguage; }

void TranslationClient::translateLines(const QVector<OcrTextLine> &lines) {
    if (m_running) {
        emit failed(QStringLiteral("translation already running"));
        return;
    }
    m_input = lines;
    m_output = lines;
    m_next = 0;
    m_running = true;
    startNext();
}

void TranslationClient::startNext() {
    while (m_next < m_input.size() && m_input[m_next].text.trimmed().isEmpty()) {
        ++m_next;
    }
    if (m_next >= m_input.size()) {
        m_running = false;
        emit translated(m_output);
        return;
    }

    const QString text = m_input[m_next].text;
    QUrl url(endpointFor(text));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("EShot/4.3.1 (+local translation; no API key)"));
    request.setRawHeader("Accept", "application/json,text/plain,*/*");
    QNetworkReply *reply = m_net.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onReplyFinished(reply); });
}

QString TranslationClient::endpointFor(const QString &text) const {
    if (m_provider == Provider::Yandex) {
        QUrl url(QStringLiteral("https://translate.yandex.net/api/v1/tr.json/translate"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("srv"), QStringLiteral("tr-text"));
        q.addQueryItem(QStringLiteral("lang"), QStringLiteral("auto-%1").arg(m_targetLanguage));
        q.addQueryItem(QStringLiteral("text"), text);
        q.addQueryItem(QStringLiteral("id"), QString::number(QRandomGenerator::global()->generate64()));
        url.setQuery(q);
        return url.toString();
    }

    QUrl url(QStringLiteral("https://translate.googleapis.com/translate_a/single"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client"), QStringLiteral("gtx"));
    q.addQueryItem(QStringLiteral("sl"), QStringLiteral("auto"));
    q.addQueryItem(QStringLiteral("tl"), m_targetLanguage);
    q.addQueryItem(QStringLiteral("dt"), QStringLiteral("t"));
    q.addQueryItem(QStringLiteral("q"), text);
    url.setQuery(q);
    return url.toString();
}

void TranslationClient::onReplyFinished(QNetworkReply *reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        m_running = false;
        emit failed(reply->errorString());
        return;
    }

    QString error;
    const QString translated = parseReply(reply->readAll(), &error);
    if (translated.isEmpty() && !error.isEmpty()) {
        m_running = false;
        emit failed(error);
        return;
    }

    if (m_next >= 0 && m_next < m_output.size()) m_output[m_next].text = translated;
    ++m_next;
    startNext();
}

QString TranslationClient::parseReply(const QByteArray &body, QString *error) const {
    *error = QString();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("JSON: %1").arg(parseError.errorString());
        return QString();
    }

    QStringList parts;
    if (m_provider == Provider::Google) {
        // Format: [[["translated","source",...],...],...]
        const QJsonArray root = doc.array();
        if (!root.isEmpty()) {
            const QJsonArray segments = root.first().toArray();
            for (const QJsonValue &seg : segments) {
                const QJsonArray item = seg.toArray();
                if (!item.isEmpty()) parts << item.first().toString();
            }
        }
    } else {
        const QJsonObject obj = doc.object();
        const QJsonArray arr = obj.value(QStringLiteral("text")).toArray();
        for (const QJsonValue &v : arr) parts << v.toString();
    }

    if (parts.isEmpty()) {
        *error = QStringLiteral("empty translation response");
        return QString();
    }
    return parts.join(QString());
}
