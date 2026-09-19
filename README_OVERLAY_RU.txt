Исправленная сборка EShot Overlay (исходники).

Что исправлено после ошибок компиляции:
1) OcrEngine.h: добавлен recognizeWithLayout;
2) OcrEngine.cpp: исправлены QLatin1Char('\n') после ошибок C2001;
3) TranslatedOverlayDialog.cpp: переименован QAction close -> closeAction (ошибка C2064).

Как обновить репозиторий:
- распакуйте ZIP;
- скопируйте содержимое папки EShot-4.3.1-overlay в локальный клон репозитория с заменой;
- commit + push;
- запустите workflow снова.
