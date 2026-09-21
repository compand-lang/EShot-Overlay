# Передача проекта EShot Overlay — краткий бриф

## Что это
Форк/ветка EShot 4.3.1 с добавленным экранным переводом выделенной области:
- OCR через Tesseract с layout/TSV (координаты строк);
- перевод через неофициальные endpoint'ы Google `gtx` и Yandex без API-ключа;
- overlay-окно поверх выделенной области;
- GitHub Actions workflow для сборки Windows x64.

## Репозиторий
Вставь ссылку: <https://github.com/ТВОЙ_ЛОГИН/EShot-Overlay>

## Как собирать
- Локально Windows 11 x64: Visual Studio 2022 Build Tools, CMake, Qt 6.8.2 `msvc2022_64`, затем `build.bat` с переменной `QT_DIR`.
- GitHub Actions: `.github/workflows/windows-esov-build.yml` (в файле может называться `windows-esov-build.yml` или `build overlay`).
- Артефакт при успехе: `EShot-overlay-windows-x64`.

## Важные файлы
- `src/core/OcrEngine.*` — OCR, TSV layout, поиск `.tsv`, языки.
- `src/core/TranslationClient.*` — Google/Yandex перевод без ключа.
- `src/ui/OcrDialog.*` — окно OCR, выбор Google/Yandex, кнопка Overlay translate, защита от старых async-ответов.
- `src/ui/TranslatedOverlayDialog.*` — overlay поверх выделенной области, `closeAll()`.
- `src/capture/CaptureOverlay.cpp` — передаёт `selectedDisplayRect()` в OCR/overlay.
- `third_party/tesseract/tessdata/configs/tsv` — нужен для Tesseract config `tsv`.
- `.github/workflows/windows-esov-build.yml` — CI.

## Что уже исправлено
- Сборка MSVC/Qt.
- `tsvPath` declaration/capture в лямбде.
- `tsv` config добавлен в `third_party/tesseract/tessdata/configs/tsv`.
- outputbase для TSV без `.png`, поиск `.tsv` по кандидатам/последнему temp-файлу.
- overlay привязан к окну OCR; добавлен `TranslatedOverlayDialog::closeAll()`.
- защита от поздних ответов OCR/перевода через `m_ocrSeq/m_translateSeq`.
- fallback: если Yandex падает — один retry через Google.

## Текущие проблемы
1. **Google/Yandex без ключа нестабильны**. Нужно сделать нормальную поддержку:
   - DeepL API key;
   - LibreTranslate self-hosted URL/key;
   - OpenAI-compatible / другие провайдеры;
   - хранение ключей в настройках;
   - честные сообщения об ошибках HTTP (код/тело ответа).
2. Возможны проблемы с DPR/масштабом Windows: overlay/координаты надо проверить на 100%/125%/150%.
3. Нужны тесты: OCR eng/rus, overlay geometry, повторный OCR, закрытие overlay, fallback Yandex→Google.

## Минимальное ТЗ для следующего исправления
- Сделать переводчик выбираемым: `google_free`, `yandex_free`, `deepl_api`, `libretranslate`, `custom_url`.
- Для платных API добавить поля key/url в настройки.
- Логировать HTTP status/body в `build-log`/debug log.
- Не показывать старые overlay/текст при новом OCR.
- Сохранить текущий минимализм UI.

## Как передать
### Вариант A — GitHub issue
Создай issue в репозитории и вставь этот файл + ссылку на failing workflow run.

### Вариант B — collaborator/агент
Добавь исполнителя как collaborator в репозиторий или дай публичную ссылку.

### Вариант C — локальный AI-кодер
Дай ему:
- путь к клону репозитория;
- этот файл;
- команду сборки: `build.bat`;
- workflow: `.github/workflows/windows-esov-build.yml`;
- последний `build-log.txt`/скрин ошибки.

## Быстрый промпт для кодера
```text
Проект: EShot 4.3.1 fork, добавлен overlay-перевод OCR-региона.
Задача: заменить нестабильные google_free/yandex_free на конфигурируемый translation provider: deepl_api, libretranslate (self-hosted), custom endpoint, оставить google/yandex как experimental. Добавить настройки key/url/target language, HTTP status/body в лог, fallback и тесты. Сборка: Windows/Qt6.8.2/CMake/build.bat, CI workflow windows-esov-build.yml. Не ломать MIT-лицензию, не копировать GPL-код.
```
