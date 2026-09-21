#include "TranslationClient.h"
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QVariant>
#include <QTimer>
#include <QDebug>

namespace {

QSettings translationSettings() {
    return QSettings(QStringLiteral("EShot"), QStringLiteral("EShot"));
}

QString providerSettingsKey(TranslationClient::Provider provider) {
    using Provider = TranslationClient::Provider;
    switch (provider) {
    case Provider::DeepLApi:      return QStringLiteral("translation/deeplApiKey");
    case Provider::LibreTranslate: return QStringLiteral("translation/libreApiKey");
    case Provider::CustomUrl:     return QStringLiteral("translation/customApiKey");
    case Provider::GoogleFree:
    case Provider::YandexFree:    break;
    }
    return QString();
}

QString endpointSettingsKey(TranslationClient::Provider provider) {
    using Provider = TranslationClient::Provider;
    switch (provider) {
    case Provider::LibreTranslate: return QStringLiteral("translation/libreUrl");
    case Provider::CustomUrl:      return QStringLiteral("translation/customUrl");
    default: break;
    }
    return QString();
}

QString joinJsonStrings(const QJsonArray &array) {
    QStringList parts;
    for (const QJsonValue &v : array) {
        if (v.isString()) parts << v.toString();
    }
    return parts.join(QLatin1Char(' ')).trimmed();
}

} // namespace

QString TranslationClient::providerToId(Provider provider) {
    switch (provider) {
    case Provider::GoogleFree:     return QStringLiteral("google_free");
    case Provider::YandexFree:     return QStringLiteral("yandex_free");
    case Provider::DeepLApi:       return QStringLiteral("deepl_api");
    case Provider::LibreTranslate: return QStringLiteral("libretranslate");
    case Provider::CustomUrl:      return QStringLiteral("custom_url");
    }
    return QStringLiteral("google_free");
}

TranslationClient::Provider TranslationClient::providerFromId(const QString &id) {
    const QString normalized = id.trimmed().toLower();
    if (normalized == QStringLiteral("yandex_free") || normalized == QStringLiteral("yandex"))
        return Provider::YandexFree;
    if (normalized == QStringLiteral("deepl_api") || normalized == QStringLiteral("deepl"))
        return Provider::DeepLApi;
    if (normalized == QStringLiteral("libretranslate") || normalized == QStringLiteral("libre"))
        return Provider::LibreTranslate;
    if (normalized == QStringLiteral("custom_url") || normalized == QStringLiteral("custom"))
        return Provider::CustomUrl;
    // "google_free", "google", пустая/неизвестная строка -> GoogleFree
    return Provider::GoogleFree;
}

QString TranslationClient::providerDisplayName(Provider provider) {
    switch (provider) {
    case Provider::GoogleFree:     return QStringLiteral("Google (free, experimental)");
    case Provider::YandexFree:     return QStringLiteral("Yandex (free, experimental)");
    case Provider::DeepLApi:       return QStringLiteral("DeepL API");
    case Provider::LibreTranslate: return QStringLiteral("LibreTranslate (self-hosted)");
    case Provider::CustomUrl:      return QStringLiteral("Custom endpoint");
    }
    return providerToId(provider);
}

QStringList TranslationClient::providerIds() {
    return {providerToId(Provider::GoogleFree),     providerToId(Provider::YandexFree),
            providerToId(Provider::DeepLApi),       providerToId(Provider::LibreTranslate),
            providerToId(Provider::CustomUrl)};
}

TranslationClient::Provider TranslationClient::freeFallbackFor(Provider provider) {
    // Yandex нестабилен без ключа: один retry текущей строки через Google.
    if (provider == Provider::YandexFree)
        return Provider::GoogleFree;
    return provider;
}

TranslationClient::TranslationClient(QObject *parent) : QObject(parent) {
    QSettings settings = translationSettings();
    m_provider = providerFromId(settings.value(QStringLiteral("translation/provider"),
                                               QStringLiteral("google_free")).toString());
    m_targetLanguage = settings.value(QStringLiteral("translation/targetLanguage"),
                                      QStringLiteral("ru")).toString();
    if (m_targetLanguage.trimmed().isEmpty())
        m_targetLanguage = QStringLiteral("ru");
    reloadCredentials();
}

void TranslationClient::reloadCredentials() {
    const QSettings settings = translationSettings();
    const QString keyKey = providerSettingsKey(m_provider);
    m_apiKey = keyKey.isEmpty() ? QString() : settings.value(keyKey).toString();
    const QString urlKey = endpointSettingsKey(m_provider);
    m_endpointUrl = urlKey.isEmpty() ? QString() : settings.value(urlKey).toString();
}

void TranslationClient::setProvider(Provider provider) {
    m_provider = provider;
    translationSettings().setValue(QStringLiteral("translation/provider"), providerToId(provider));
    reloadCredentials();
}

TranslationClient::Provider TranslationClient::provider() const { return m_provider; }

QString TranslationClient::providerId() const { return providerToId(m_provider); }

void TranslationClient::setTargetLanguage(const QString &bcp47) {
    m_targetLanguage = bcp47.trimmed().isEmpty() ? QStringLiteral("ru") : bcp47.trimmed();
    translationSettings().setValue(QStringLiteral("translation/targetLanguage"), m_targetLanguage);
}

QString TranslationClient::targetLanguage() const { return m_targetLanguage; }

void TranslationClient::setApiKey(const QString &key) {
    m_apiKey = key.trimmed();
    const QString keyKey = providerSettingsKey(m_provider);
    if (!keyKey.isEmpty())
        translationSettings().setValue(keyKey, m_apiKey);
}

QString TranslationClient::apiKey() const { return m_apiKey; }

void TranslationClient::setEndpointUrl(const QString &url) {
    m_endpointUrl = url.trimmed();
    const QString urlKey = endpointSettingsKey(m_provider);
    if (!urlKey.isEmpty())
        translationSettings().setValue(urlKey, m_endpointUrl);
}

QString TranslationClient::endpointUrl() const { return m_endpointUrl; }

QString TranslationClient::languageCode() const {
    // DeepL принимает двухбуквенные коды (RU, EN...), остальным подходит BCP47-основа.
    return m_targetLanguage.left(2).toLower();
}

void TranslationClient::debugLog(const QString &message) {
    const QString line = QStringLiteral("[%1] %2")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODate), message);
    qDebug() << "[EShot translation]" << line;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    QFile file(QDir(dir).filePath(QStringLiteral("translation-debug.log")));
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << line << QLatin1Char('\n');
    }
}

void TranslationClient::translateLines(const QVector<OcrTextLine> &lines) {
    if (m_running) {
        emit failed(QStringLiteral("translation already running"));
        return;
    }
    m_input = lines;
    m_output = lines;
    m_next = 0;
    m_running = true;
    m_fallbackUsed = false;
    m_retryCount = 0;
    m_runProvider = m_provider;
    ++m_generation;
    debugLog(QStringLiteral("start provider=%1 target=%2 lines=%3")
                 .arg(providerToId(m_provider), m_targetLanguage)
                 .arg(m_input.size()));
    startNext();
}

TranslationClient::BuiltRequest TranslationClient::buildRequest(const QString &text,
                                                                 Provider provider) const {
    BuiltRequest request;

    auto failWith = [&request](const QString &reason) {
        request.ok = false;
        request.error = reason;
    };

    switch (provider) {
    case Provider::YandexFree: {
        QUrl url(QStringLiteral("https://translate.yandex.net/api/v1/tr.json/translate"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("srv"), QStringLiteral("tr-text"));
        q.addQueryItem(QStringLiteral("lang"),
                       QStringLiteral("auto-%1").arg(m_targetLanguage));
        q.addQueryItem(QStringLiteral("text"), text);
        q.addQueryItem(QStringLiteral("id"),
                       QString::number(QRandomGenerator::global()->generate64()));
        url.setQuery(q);
        request.url = url;
        request.ok = true;
        return request;
    }
    case Provider::GoogleFree: {
        QUrl url(QStringLiteral("https://translate.googleapis.com/translate_a/single"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("client"), QStringLiteral("gtx"));
        q.addQueryItem(QStringLiteral("sl"), QStringLiteral("auto"));
        q.addQueryItem(QStringLiteral("tl"), m_targetLanguage);
        q.addQueryItem(QStringLiteral("dt"), QStringLiteral("t"));
        q.addQueryItem(QStringLiteral("q"), text);
        url.setQuery(q);
        request.url = url;
        request.ok = true;
        return request;
    }
    case Provider::DeepLApi: {
        if (m_apiKey.isEmpty()) {
            failWith(QStringLiteral(
                "DeepL API key is missing. Add it in Settings → Translation."));
            return request;
        }
        // Ключи ":fx" — бесплатная подписка api-free.deepl.com, остальные — api.deepl.com.
        const bool freeKey = m_apiKey.endsWith(QLatin1String(":fx"));
        request.url = QUrl(QStringLiteral("https://%1/v2/translate")
                               .arg(freeKey ? QStringLiteral("api-free.deepl.com")
                                            : QStringLiteral("api.deepl.com")));
        request.verb = QStringLiteral("POST");
        request.headers.append(qMakePair(QByteArray("Content-Type"),
                                         QByteArray("application/x-www-form-urlencoded")));
        request.headers.append(qMakePair(
            QByteArray("Authorization"),
            QStringLiteral("DeepL-Auth-Key %1").arg(m_apiKey).toUtf8()));
        QUrlQuery form;
        form.addQueryItem(QStringLiteral("text"), text);
        form.addQueryItem(QStringLiteral("target_lang"), languageCode().toUpper());
        request.body = form.query(QUrl::FullyEncoded).toUtf8();
        request.ok = true;
        return request;
    }
    case Provider::LibreTranslate: {
        QString base = m_endpointUrl;
        if (base.isEmpty())
            base = QStringLiteral("http://localhost:5000");
        QUrl url(base);
        if (!url.isValid() || url.scheme().isEmpty()) {
            failWith(QStringLiteral("LibreTranslate URL is invalid: '%1'. "
                                    "Set it in Settings → Translation.").arg(base));
            return request;
        }
        const QString path = url.path();
        if (!path.endsWith(QLatin1String("/translate")))
            url.setPath(path + (path.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/"))
                        + QStringLiteral("translate"));
        request.url = url;
        request.verb = QStringLiteral("POST");
        request.headers.append(qMakePair(QByteArray("Content-Type"),
                                         QByteArray("application/json")));
        if (!m_apiKey.isEmpty())
            request.headers.append(qMakePair(
                QByteArray("Authorization"),
                QStringLiteral("Bearer %1").arg(m_apiKey).toUtf8()));
        QJsonObject payload;
        payload.insert(QStringLiteral("q"), text);
        payload.insert(QStringLiteral("source"), QStringLiteral("auto"));
        payload.insert(QStringLiteral("target"), languageCode());
        payload.insert(QStringLiteral("format"), QStringLiteral("text"));
        request.body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        request.ok = true;
        return request;
    }
    case Provider::CustomUrl: {
        if (m_endpointUrl.isEmpty()) {
            failWith(QStringLiteral(
                "Custom endpoint URL is missing. Set it in Settings → Translation."));
            return request;
        }
        const QUrl url(m_endpointUrl);
        if (!url.isValid() || url.scheme().isEmpty()) {
            failWith(QStringLiteral("Custom endpoint URL is invalid: '%1'.")
                         .arg(m_endpointUrl));
            return request;
        }
        request.url = url;
        request.verb = QStringLiteral("POST");
        request.headers.append(qMakePair(QByteArray("Content-Type"),
                                         QByteArray("application/json")));
        if (!m_apiKey.isEmpty())
            request.headers.append(qMakePair(
                QByteArray("Authorization"),
                QStringLiteral("Bearer %1").arg(m_apiKey).toUtf8()));
        QJsonObject payload;
        payload.insert(QStringLiteral("q"), text);
        payload.insert(QStringLiteral("source"), QStringLiteral("auto"));
        payload.insert(QStringLiteral("target"), languageCode());
        payload.insert(QStringLiteral("format"), QStringLiteral("text"));
        request.body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        request.ok = true;
        return request;
    }
    }
    failWith(QStringLiteral("unsupported provider"));
    return request;
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
    const BuiltRequest built = buildRequest(text, m_runProvider);
    if (!built.ok) {
        fail(built.error);
        return;
    }

    QNetworkRequest request(built.url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("EShot/4.3.1 (+screen translation)"));
    request.setRawHeader("Accept", "application/json,text/plain,*/*");
    for (const auto &header : built.headers)
        request.setRawHeader(header.first, header.second);

    debugLog(QStringLiteral("request provider=%1 line=%2 %3 %4")
                 .arg(providerToId(m_runProvider))
                 .arg(m_next)
                 .arg(built.verb, built.url.toString(QUrl::RemoveQuery)));

    QNetworkReply *reply = built.verb == QLatin1String("POST")
                               ? m_net.post(request, built.body)
                               : m_net.get(request);
    const int generation = m_generation;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation]() { onReplyFinished(reply, generation); });
}

void TranslationClient::fail(const QString &reason) {
    m_running = false;
    debugLog(QStringLiteral("failed provider=%1 reason=%2")
                 .arg(providerToId(m_provider), reason));
    emit failed(reason);
}

void TranslationClient::onReplyFinished(QNetworkReply *reply, int generation) {
    reply->deleteLater();
    if (generation != m_generation) {
        debugLog(QStringLiteral("stale reply ignored (generation %1, current %2)")
                     .arg(generation)
                     .arg(m_generation));
        return;
    }

    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const QByteArray body = reply->readAll();
    debugLog(QStringLiteral("reply provider=%1 line=%2 http=%3 error=%4 body=%5")
                 .arg(providerToId(m_runProvider))
                 .arg(m_next)
                 .arg(status.isValid() ? status.toInt() : -1)
                 .arg(reply->error())
                 .arg(QString::fromUtf8(body.left(600))));

    if (reply->error() != QNetworkReply::NoError) {
        const int httpCode = status.isValid() ? status.toInt() : 0;
        // 429/5xx — временная ошибка: ждём (Retry-After, если есть) и повторяем строку.
        if ((httpCode == 429 || httpCode >= 500) && m_retryCount < 2) {
            ++m_retryCount;
            int delaySec = 3;
            bool ok = false;
            const int retryAfter = reply->rawHeader("Retry-After").toInt(&ok);
            if (ok && retryAfter > 0)
                delaySec = qBound(1, retryAfter, 15);
            debugLog(QStringLiteral("retry %1/%2 in %3s (HTTP %4) line=%5")
                         .arg(m_retryCount)
                         .arg(2)
                         .arg(delaySec)
                         .arg(httpCode)
                         .arg(m_next));
            scheduleNext(delaySec * 1000, generation);
            return;
        }
        // Честное сообщение: код/тело ответа, если они есть.
        QString reason = reply->errorString();
        if (status.isValid())
            reason += QStringLiteral(" (HTTP %1)").arg(status.toInt());
        if (!body.isEmpty())
            reason += QStringLiteral(": ") + QString::fromUtf8(body.left(200));
        handleFailure(reason);
        return;
    }

    QString error;
    const QString translated = parseReplyFor(m_runProvider, body, &error);
    if (translated.isEmpty() && !error.isEmpty()) {
        handleFailure(error);
        return;
    }

    if (m_next >= 0 && m_next < m_output.size())
        m_output[m_next].text = translated;
    ++m_next;
    m_retryCount = 0;
    if (m_next < m_input.size()) {
        // Небольшая пауза между строками, чтобы не ловить 429 от бесплатных endpoint'ов.
        scheduleNext(150, generation);
    } else {
        startNext();
    }
}

void TranslationClient::scheduleNext(int delayMs, int generation) {
    QTimer::singleShot(delayMs, this, [this, generation]() {
        if (generation != m_generation || !m_running)
            return;
        startNext();
    });
}

void TranslationClient::handleFailure(const QString &reason) {
    const Provider fallback = freeFallbackFor(m_runProvider);
    if (!m_fallbackUsed && fallback != m_runProvider) {
        m_fallbackUsed = true;
        debugLog(QStringLiteral("fallback %1 -> %2: %3")
                     .arg(providerToId(m_runProvider), providerToId(fallback), reason));
        m_runProvider = fallback; // только для текущего прогона; QSettings не трогаем
        startNext();              // повторяем текущую строку через fallback-провайдер
        return;
    }
    fail(reason);
}

QString TranslationClient::parseReplyFor(Provider provider, const QByteArray &body,
                                          QString *error) const {
    switch (provider) {
    case Provider::GoogleFree:     return parseGoogleReply(body, error);
    case Provider::YandexFree:     return parseYandexReply(body, error);
    case Provider::DeepLApi:       return parseDeepLReply(body, error);
    case Provider::LibreTranslate: return parseLibreReply(body, error);
    case Provider::CustomUrl:      return parseCustomReply(body, error);
    }
    *error = QStringLiteral("unsupported provider");
    return QString();
}

QString TranslationClient::parseGoogleReply(const QByteArray &body, QString *error) {
    *error = QString();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("Google: JSON: %1").arg(parseError.errorString());
        return QString();
    }
    // Format: [[["translated","source",...],...],...]
    QStringList parts;
    const QJsonArray root = doc.array();
    if (!root.isEmpty()) {
        const QJsonArray segments = root.first().toArray();
        for (const QJsonValue &seg : segments) {
            const QJsonArray item = seg.toArray();
            if (!item.isEmpty()) parts << item.first().toString();
        }
    }
    if (parts.isEmpty()) {
        *error = QStringLiteral("Google: empty translation response");
        return QString();
    }
    return parts.join(QString());
}

QString TranslationClient::parseYandexReply(const QByteArray &body, QString *error) {
    *error = QString();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("Yandex: JSON: %1").arg(parseError.errorString());
        return QString();
    }
    // Format: {"code":200,"lang":"en-ru","text":["..."]}
    const QJsonObject obj = doc.object();
    const int code = obj.value(QStringLiteral("code")).toInt(200);
    if (code != 200) {
        *error = QStringLiteral("Yandex: API code %1").arg(code);
        return QString();
    }
    const QString joined = joinJsonStrings(obj.value(QStringLiteral("text")).toArray());
    if (joined.isEmpty()) {
        *error = QStringLiteral("Yandex: empty translation response");
        return QString();
    }
    return joined;
}

QString TranslationClient::parseDeepLReply(const QByteArray &body, QString *error) {
    *error = QString();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("DeepL: JSON: %1").arg(parseError.errorString());
        return QString();
    }
    // Format: {"translations":[{"detected_source_language":"EN","text":"..."}]}
    const QJsonObject obj = doc.object();
    const QJsonArray translations = obj.value(QStringLiteral("translations")).toArray();
    if (!translations.isEmpty()) {
        const QString text = translations.first()
                                 .toObject()
                                 .value(QStringLiteral("text"))
                                 .toString()
                                 .trimmed();
        if (!text.isEmpty())
            return text;
    }
    const QString message = obj.value(QStringLiteral("message")).toString();
    *error = message.isEmpty() ? QStringLiteral("DeepL: empty translation response")
                               : QStringLiteral("DeepL: %1").arg(message);
    return QString();
}

QString TranslationClient::parseLibreReply(const QByteArray &body, QString *error) {
    *error = QString();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("LibreTranslate: JSON: %1").arg(parseError.errorString());
        return QString();
    }
    // Format: {"translatedText":"..."}
    const QJsonObject obj = doc.object();
    const QString text = obj.value(QStringLiteral("translatedText")).toString().trimmed();
    if (text.isEmpty()) {
        const QString detail = obj.value(QStringLiteral("error")).toString();
        *error = detail.isEmpty() ? QStringLiteral("LibreTranslate: empty translation response")
                                  : QStringLiteral("LibreTranslate: %1").arg(detail);
        return QString();
    }
    return text;
}

QString TranslationClient::parseCustomReply(const QByteArray &body, QString *error) {
    *error = QString();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("Custom: JSON: %1").arg(parseError.errorString());
        return QString();
    }
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        // OpenAI-compatible chat.completions:
        // {"choices":[{"message":{"content":"..."}}]}
        const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            const QString text = choices.first()
                                     .toObject()
                                     .value(QStringLiteral("message"))
                                     .toObject()
                                     .value(QStringLiteral("content"))
                                     .toString()
                                     .trimmed();
            if (!text.isEmpty())
                return text;
        }
        // LibreTranslate-совместимый ответ.
        const QString libre = obj.value(QStringLiteral("translatedText")).toString().trimmed();
        if (!libre.isEmpty())
            return libre;
        const QString detail = obj.value(QStringLiteral("error")).toString();
        if (!detail.isEmpty()) {
            *error = QStringLiteral("Custom: %1").arg(detail);
            return QString();
        }
    }
    *error = QStringLiteral("Custom: unrecognized response shape");
    return QString();
}
