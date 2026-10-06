#include <QtTest/QtTest>

#include "core/TranslationClient.h"

// Проверяет id-провайдеров, парсеры ответов и fallback-политику.
// Сеть не используется — все парсеры статические и чистые.

class TranslationClientTests : public QObject
{
    Q_OBJECT

private slots:
    void providerIdsRoundTrip();
    void legacyProviderIdsMapToFreeProviders();
    void unknownProviderIdFallsBackToGoogle();
    void freeFallbackOnlyForYandex();
    void googleReplyParsing();
    void yandexReplyParsing();
    void deeplReplyParsing();
    void libreReplyParsing();
    void customReplyParsingOpenAiAndLibreShapes();
    void parseErrorsAreReported();
};

using Provider = TranslationClient::Provider;

void TranslationClientTests::providerIdsRoundTrip()
{
    const QStringList ids = TranslationClient::providerIds();
    QCOMPARE(ids.size(), 5);
    QCOMPARE(ids.at(0), QStringLiteral("google_free"));
    QCOMPARE(ids.at(1), QStringLiteral("yandex_free"));
    QCOMPARE(ids.at(2), QStringLiteral("deepl_api"));
    QCOMPARE(ids.at(3), QStringLiteral("libretranslate"));
    QCOMPARE(ids.at(4), QStringLiteral("custom_url"));

    for (const QString &id : ids) {
        QCOMPARE(TranslationClient::providerToId(TranslationClient::providerFromId(id)), id);
    }
}

void TranslationClientTests::legacyProviderIdsMapToFreeProviders()
{
    QCOMPARE(TranslationClient::providerFromId(QStringLiteral("google")), Provider::GoogleFree);
    QCOMPARE(TranslationClient::providerFromId(QStringLiteral("yandex")), Provider::YandexFree);
}

void TranslationClientTests::unknownProviderIdFallsBackToGoogle()
{
    QCOMPARE(TranslationClient::providerFromId(QStringLiteral("")), Provider::GoogleFree);
    QCOMPARE(TranslationClient::providerFromId(QStringLiteral("nonsense")), Provider::GoogleFree);
}

void TranslationClientTests::freeFallbackOnlyForYandex()
{
    QCOMPARE(TranslationClient::freeFallbackFor(Provider::YandexFree), Provider::GoogleFree);
    QCOMPARE(TranslationClient::freeFallbackFor(Provider::GoogleFree), Provider::GoogleFree);
    QCOMPARE(TranslationClient::freeFallbackFor(Provider::DeepLApi), Provider::DeepLApi);
    QCOMPARE(TranslationClient::freeFallbackFor(Provider::LibreTranslate), Provider::LibreTranslate);
    QCOMPARE(TranslationClient::freeFallbackFor(Provider::CustomUrl), Provider::CustomUrl);
}

void TranslationClientTests::googleReplyParsing()
{
    QString error;
    const QByteArray body = R"([[[ "Привет мир", "Hello world", null, null, 10 ]], null, "en"]])";
    QCOMPARE(TranslationClient::parseGoogleReply(body, &error), QStringLiteral("Привет мир"));
    QVERIFY(error.isEmpty());

    QCOMPARE(TranslationClient::parseGoogleReply("not json", &error), QString());
    QVERIFY(!error.isEmpty());

    QCOMPARE(TranslationClient::parseGoogleReply("[]", &error), QString());
    QVERIFY(!error.isEmpty());
}

void TranslationClientTests::yandexReplyParsing()
{
    QString error;
    const QByteArray ok = R"({"code":200,"lang":"en-ru","text":["Привет","мир"]})";
    QCOMPARE(TranslationClient::parseYandexReply(ok, &error), QStringLiteral("Привет мир"));
    QVERIFY(error.isEmpty());

    const QByteArray apiError = R"({"code":502,"text":[]})";
    QCOMPARE(TranslationClient::parseYandexReply(apiError, &error), QString());
    QVERIFY(error.contains(QStringLiteral("502")));
}

void TranslationClientTests::deeplReplyParsing()
{
    QString error;
    const QByteArray ok = R"({"translations":[{"detected_source_language":"EN","text":"Hallo Welt"}]})";
    QCOMPARE(TranslationClient::parseDeepLReply(ok, &error), QStringLiteral("Hallo Welt"));
    QVERIFY(error.isEmpty());

    const QByteArray authError = R"({"message":"Authorization failed"})";
    QCOMPARE(TranslationClient::parseDeepLReply(authError, &error), QString());
    QVERIFY(error.contains(QStringLiteral("Authorization failed")));
}

void TranslationClientTests::libreReplyParsing()
{
    QString error;
    const QByteArray ok = R"({"translatedText":"Bonjour le monde"})";
    QCOMPARE(TranslationClient::parseLibreReply(ok, &error), QStringLiteral("Bonjour le monde"));
    QVERIFY(error.isEmpty());

    const QByteArray apiError = R"({"error":"Invalid API key"})";
    QCOMPARE(TranslationClient::parseLibreReply(apiError, &error), QString());
    QVERIFY(error.contains(QStringLiteral("Invalid API key")));
}

void TranslationClientTests::customReplyParsingOpenAiAndLibreShapes()
{
    QString error;

    // OpenAI-compatible chat.completions
    const QByteArray openAi = R"({"choices":[{"message":{"content":"Hola mundo"}}]})";
    QCOMPARE(TranslationClient::parseCustomReply(openAi, &error), QStringLiteral("Hola mundo"));
    QVERIFY(error.isEmpty());

    // LibreTranslate-совместимый ответ
    const QByteArray libre = R"({"translatedText":"Hola mundo"})";
    QCOMPARE(TranslationClient::parseCustomReply(libre, &error), QStringLiteral("Hola mundo"));
    QVERIFY(error.isEmpty());

    // Неизвестная форма ответа
    QCOMPARE(TranslationClient::parseCustomReply(R"({"unexpected":true})", &error), QString());
    QVERIFY(!error.isEmpty());
}

void TranslationClientTests::parseErrorsAreReported()
{
    QString error;
    QVERIFY(TranslationClient::parseGoogleReply("###", &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(TranslationClient::parseYandexReply("###", &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(TranslationClient::parseDeepLReply("###", &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(TranslationClient::parseLibreReply("###", &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

QTEST_APPLESS_MAIN(TranslationClientTests)

#include "TranslationClientTests.moc"
