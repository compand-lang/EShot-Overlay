Исправленная сборка EShot Overlay (исходники).

Что исправлено после ошибок компиляции:
1) OcrEngine.h: добавлен recognizeWithLayout;
2) OcrEngine.cpp: исправлены QLatin1Char('\n') после ошибок C2001;
3) TranslatedOverlayDialog.cpp: переименован QAction close -> closeAction (ошибка C2064).

Этап 2 — конфигурируемый переводчик (по брифу EShot-overlay-handoff-ru.md):
1) TranslationClient: провайдеры google_free / yandex_free (experimental),
   deepl_api, libretranslate (self-hosted), custom_url (OpenAI-compatible
   или LibreTranslate-совместимый endpoint);
2) Настройки (Settings → General → Translation): провайдер, целевой язык,
   ключи DeepL / LibreTranslate / Custom, URL для LibreTranslate и Custom;
   ключи хранятся локально в QSettings("EShot","EShot"), экспорт/импорт JSON;
3) Честные ошибки HTTP: код статуса + тело ответа в сообщении и в debug log
   (файл translation-debug.log в AppDataLocation + qDebug);
4) Fallback: при сбое yandex_free — один retry текущей строки через google_free;
5) Защита overlay: TranslatedOverlayDialog::closeAll() закрывает старые overlay
   при новом OCR и перед показом нового перевода; защита от поздних ответов
   через m_ocrSeq/m_translateSeq и generation в TranslationClient;
6) Тесты: tests/TranslationClientTests.cpp (id провайдеров, парсеры ответов,
   fallback-политика) и tests/TranslatedOverlayDialogTests.cpp (геометрия
   overlay, closeAll).

Как обновить репозиторий:
- распакуйте ZIP;
- скопируйте содержимое папки EShot-4.3.1-overlay в локальный клон репозитория с заменой;
- commit + push;
- запустите workflow снова.
