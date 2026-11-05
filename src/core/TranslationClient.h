#ifndef TRANSLATIONCLIENT_H
#define TRANSLATIONCLIENT_H

// Конфигурируемый клиент перевода OCR-строк.
//
// Провайдеры:
//   google_free     — неофициальный endpoint Google gtx (без ключа, experimental)
//   yandex_free     — неофициальный endpoint Yandex tr.json (без ключа, experimental)
//   mymemory_free   — MyMemory с автоопределением языка, резервный бесплатный перевод
//   deepl_api       — официальный DeepL API v2 (нужен ключ, Settings → Translation)
//   libretranslate  — self-hosted LibreTranslate (нужен base URL, ключ опционален)
//   custom_url      — произвольный endpoint: OpenAI-compatible chat/completions
//                     или LibreTranslate-совместимый /translate (URL + опциональный ключ)
//
// Ключи/URL хранятся в QSettings("EShot","EShot") и редактируются в настройках.
// Все HTTP-ответы (status + body) логируются в debug log (см. debugLog()).

#include "core/OcrEngine.h"
#include <QObject>
#include <QVector>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QUrl>
#include <QPair>

class TranslationClient : public QObject {
    Q_OBJECT
public:
    enum class Provider { GoogleFree, YandexFree, MyMemoryFree, DeepLApi, LibreTranslate, CustomUrl };

    static QString providerToId(Provider provider);
    static Provider providerFromId(const QString &id); // неизвестный/legacy id -> GoogleFree
    static QString providerDisplayName(Provider provider);
    static QStringList providerIds(); // порядок отображения в UI

    // Цепочка для нестабильных бесплатных провайдеров:
    // YandexFree -> GoogleFree -> MyMemoryFree.
    static Provider freeFallbackFor(Provider provider); // provider без fallback -> сам provider
    // Повторяем только временные ошибки сервера; 429 сразу уходит в fallback.
    static bool shouldRetryHttpStatus(int httpCode);

    explicit TranslationClient(QObject *parent = nullptr);

    void setProvider(Provider provider);
    Provider provider() const;
    QString providerId() const;
    void setTargetLanguage(const QString &bcp47);
    QString targetLanguage() const;
    // Ключ для текущего провайдера (DeepL / LibreTranslate / Custom). Пишется в QSettings.
    void setApiKey(const QString &key);
    QString apiKey() const;
    // Endpoint для текущего провайдера (LibreTranslate base URL / Custom URL). Пишется в QSettings.
    void setEndpointUrl(const QString &url);
    QString endpointUrl() const;

    void translateLines(const QVector<OcrTextLine> &lines);

    // Чистые парсеры ответов — независимы от сети, доступны тестам.
    static QString parseGoogleReply(const QByteArray &body, QString *error);
    static QString parseYandexReply(const QByteArray &body, QString *error);
    static QString parseMyMemoryReply(const QByteArray &body, QString *error);
    static QString parseDeepLReply(const QByteArray &body, QString *error);
    static QString parseLibreReply(const QByteArray &body, QString *error);
    static QString parseCustomReply(const QByteArray &body, QString *error);

signals:
    void translated(const QVector<OcrTextLine> &lines);
    void failed(const QString &reason);

private:
    struct BuiltRequest {
        bool ok = false;
        QString verb = QStringLiteral("GET");
        QUrl url;
        QByteArray body;
        QList<QPair<QByteArray, QByteArray>> headers;
        QString error; // заполнено, если ok == false
    };

    static void debugLog(const QString &message);

    void startNext();
    BuiltRequest buildRequest(const QString &text, Provider provider) const;
    QString languageCode() const; // двухбуквенный код из BCP47
    void reloadCredentials();     // перечитывает key/url для текущего провайдера из QSettings
    void onReplyFinished(QNetworkReply *reply, int generation);
    QString parseReplyFor(Provider provider, const QByteArray &body, QString *error) const;
    void handleFailure(const QString &reason); // fallback Yandex→Google, затем fail()
    void fail(const QString &reason);
    void scheduleNext(int delayMs, int generation); // отложенный startNext() с защитой generation

    QNetworkAccessManager m_net;
    Provider m_provider = Provider::GoogleFree;
    Provider m_runProvider = Provider::GoogleFree; // эффективный провайдер текущего прогона (fallback не меняет m_provider)
    QString m_targetLanguage = QStringLiteral("ru");
    QString m_apiKey;
    QString m_endpointUrl;
    QVector<OcrTextLine> m_input;
    QVector<OcrTextLine> m_output;
    int m_next = 0;
    bool m_running = false;
    bool m_fallbackUsed = false;
    int m_generation = 0;
    int m_retryCount = 0; // повторные попытки текущей строки при 5xx
};

#endif
