#!/usr/bin/env bash
# port-upstream.sh — перенос наших доработок на новые версии апстрима Benoks/EShot.
#
# Логика:
#   .port-base            — файл в корне репо: тег апстрима, на котором основан main
#   git remote upstream   — github.com/Benoks/EShot
#
#   1. Ищет новый тег апстрима (vX.Y.Z, семантическое сравнение).
#   2. Если тег == .port-base  -> exit 0 (новых версий нет).
#   3. Иначе: ветка port-<тег> от нового тега, туда переносятся наши изменения
#      (three-way merge-file по списку diff .port-base..main).
#      - файлы, добавленные нами (есть в main, не было в base) -> копируются как есть
#      - файлы, изменённые нами -> git merge-file (ours=main, base=.port-base, theirs=новый тег)
#      - файлы, удалённые нами -> остаются удалёнными
#   4. Проверяются маркеры (те же, что в CI workflow).
#   5. Ветка пушится, ждётся CI; при успехе main fast-forward'ится, .port-base
#      обновляется, всё пушится. При конфликтах/падении CI ветка остаётся
#      для ручного разбора.
#
# Коды выхода: 0 = актуально, 1 = перенесено и влито, 2 = конфликты (ручной разбор),
#              3 = CI упал, 4 = ошибка окружения.
set -u
cd "$(dirname "$0")/.." || exit 4

REMOTE=${REMOTE:-upstream}
BASE_FILE=.port-base

[ -f "$BASE_FILE" ] || { echo "ERROR: $BASE_FILE not found"; exit 4; }
git remote get-url "$REMOTE" >/dev/null 2>&1 || {
  git remote add "$REMOTE" https://github.com/Benoks/EShot.git || exit 4
}
echo "== fetch $REMOTE =="
git fetch "$REMOTE" main --prune || exit 4
# всегда стартуем с main: .port-base читаем из main, а не из рабочего дерева
git checkout main >/dev/null 2>&1 || exit 4
BASE_TAG=$(sed -n '1p' "$BASE_FILE" | tr -d ' \r')
BASE_SHA=$(sed -n '2p' "$BASE_FILE" | tr -d ' \r')

# последний семантический тег вида vN.N.N из апстрима (по ls-remote, без клоббера локальных тегов)
NEW=$(git ls-remote --tags "$REMOTE" 'refs/tags/v[0-9]*' \
      | awk -F/ '{print $3}' | grep -v '\^' | sort -t. -k1,1V -k2,2V -k3,3V | tail -1)
[ -n "$NEW" ] || { echo "ERROR: no upstream tags found"; exit 4; }
# peel annotated tag -> commit sha (GitHub отдаёт и сам тег, и <tag>^{})
NEW_SHA=$(git ls-remote --tags "$REMOTE" "refs/tags/$NEW^{}" | awk '{print $1}')
[ -n "$NEW_SHA" ] || NEW_SHA=$(git ls-remote --tags "$REMOTE" "refs/tags/$NEW" | awk '{print $1}')
[ -n "$NEW_SHA" ] || { echo "ERROR: cannot resolve $NEW to sha"; exit 4; }

echo "base=$BASE_TAG($BASE_SHA)  latest=$NEW($NEW_SHA)"

if [ "$NEW" = "$BASE_TAG" ]; then
  echo "UP-TO-DATE: наш main уже основан на $BASE_TAG"
  exit 0
fi

git cat-file -e "$NEW_SHA^{commit}" 2>/dev/null || git fetch "$REMOTE" "$NEW_SHA" || exit 4

# откат на случай повторного запуска после неудачи
git rev-parse -q --verify "port-$NEW" >/dev/null && git branch -D "port-$NEW" >/dev/null
git checkout -B "port-$NEW" "$NEW_SHA" >/dev/null 2>&1 || { echo "ERROR: cannot create branch port-$NEW"; exit 4; }
echo "== branch port-$NEW created from $NEW =="

TMP=.porttmp
rm -rf "$TMP"; mkdir -p "$TMP"

# --- 1. файлы, добавленные нами (A) и переименования (R) -------------------
git diff --name-status --diff-filter=A "$BASE_SHA" main | awk '$1=="A"{print $2}' > "$TMP/added.txt"
git diff --name-status --diff-filter=R "$BASE_SHA" main | awk '{print $3}'  >> "$TMP/added.txt"
while IFS= read -r f; do
  [ -n "$f" ] || continue
  mkdir -p "$(dirname "$f")"
  if git cat-file -e "main:$f" 2>/dev/null; then
    git checkout main -- "$f" && echo "restored(added): $f"
  fi
done < "$TMP/added.txt"

# --- 2. файлы, изменённые нами (M) ------------------------------------------
CONFLICTS=0
git diff --name-status --diff-filter=M "$BASE_SHA" main | awk '$1=="M"{print $2}' > "$TMP/modified.txt"
while IFS= read -r f; do
  [ -n "$f" ] || continue
  mkdir -p "$(dirname "$f")"
  git show "main:$f"       > "$TMP/ours"
  git show "$BASE_SHA:$f"      > "$TMP/base"
  if git cat-file -e "$NEW_SHA:$f" 2>/dev/null; then
    git show "$NEW_SHA:$f"   > "$TMP/theirs"
    if git merge-file -q -L ours -L base -L theirs "$TMP/ours" "$TMP/base" "$TMP/theirs"; then
      cp "$TMP/ours" "$f"
      echo "merged clean: $f"
    else
      cp "$TMP/ours" "$f"
      echo "CONFLICT: $f"
      CONFLICTS=$((CONFLICTS+1))
    fi
  else
    # апстрим удалил файл, который мы меняли — оставляем нашу версию
    cp "$TMP/ours" "$f"
    echo "kept ours (deleted upstream): $f"
  fi
done < "$TMP/modified.txt"

# --- 3. файлы, удалённые нами (D), но живые в новой версии — остаются удалёнными
git diff --name-status --diff-filter=D "$BASE_SHA" main | awk '$1=="D"{print $2}' > "$TMP/deleted.txt"
while IFS= read -r f; do
  [ -n "$f" ] || continue
  if git cat-file -e "$NEW_SHA:$f" 2>/dev/null; then
    rm -f "$f"
    git rm -q --cached "$f" 2>/dev/null
    echo "stays deleted: $f"
  fi
done < "$TMP/deleted.txt"

# --- 4. маркеры -------------------------------------------------------------
echo "== marker checks =="
FAIL=0
grep -q "recognizeWithLayout" src/core/OcrEngine.h || { echo "FAIL marker: recognizeWithLayout"; FAIL=1; }
grep -q "tsvPath" src/core/OcrEngine.cpp || { echo "FAIL marker: tsvPath"; FAIL=1; }
! grep -q "ui/OcrDialog.h" src/capture/CaptureOverlay.cpp || { echo "FAIL marker: OcrDialog.h included"; FAIL=1; }
[ -f src/ui/TranslatorDialog.cpp ] || { echo "FAIL marker: TranslatorDialog.cpp missing"; FAIL=1; }
grep -q "copyAllText" src/ui/TranslatedOverlayDialog.cpp || { echo "FAIL marker: copyAllText"; FAIL=1; }
grep -rn "^<<<<<<<\|^>>>>>>>" src/ CMakeLists.txt >/dev/null 2>&1 && { echo "FAIL: conflict markers in tree"; FAIL=1; }

if [ "$CONFLICTS" -gt 0 ] || [ "$FAIL" -eq 1 ]; then
  git add -A
  git commit -q -m "WIP port to $NEW (conflicts, needs manual merge)" || true
  if [ "${SKIP_PUSH:-0}" != "1" ]; then
    git push -q -f origin "port-$NEW" || true
  fi
  rm -rf "$TMP"
  echo "NEEDS-MANUAL: конфликтов=$CONFLICTS, маркеров FAIL=$FAIL. Ветка: port-$NEW"
  exit 2
fi

# --- 6. публикация релиза (тег + ассет из CI) --------------------------------
# Вызывается после успешного влива в main. Не прерывает пайплайн при ошибке.
publish_release() {
  RUN_ID=$1
  echo "== publish release $NEW =="
  git tag -f -a "$NEW" -m "EShot Overlay $NEW" >/dev/null 2>&1
  git push -q -f origin "$NEW" || { echo "WARN: не удалось запушить тег $NEW"; return 1; }

  ART_ID=$(curl -s -H "Authorization: token $TOKEN" \
    "https://api.github.com/repos/compand-lang/EShot-Overlay/actions/runs/$RUN_ID/artifacts" \
    | python -c "import json,sys
arts=json.load(sys.stdin).get('artifacts',[])
m=[a for a in arts if a['name']=='EShot-overlay-windows-x64']
print(m[0]['id'] if m else '')" 2>/dev/null)
  [ -n "$ART_ID" ] || { echo "WARN: артефакт не найден"; return 1; }

  ZIP="$TMP/release-$NEW.zip"
  mkdir -p "$TMP"
  for i in $(seq 1 15); do
    curl -s -L -C - -H "Authorization: token $TOKEN" \
      "https://api.github.com/repos/compand-lang/EShot-Overlay/actions/artifacts/$ART_ID/zip" -o "$ZIP" && break
    sleep 3
  done
  python -c "import zipfile,sys; sys.exit(0 if zipfile.ZipFile('$ZIP').testzip() is None else 1)" || { echo "WARN: архив повреждён"; return 1; }

  # удаляем предыдущий релиз на этом теге, если есть
  OLD_REL=$(curl -s -H "Authorization: token $TOKEN" "https://api.github.com/repos/compand-lang/EShot-Overlay/releases/tags/$NEW" \
    | python -c "import json,sys; print(json.load(sys.stdin).get('id',''))" 2>/dev/null)
  if [ -n "$OLD_REL" ]; then
    curl -s -X DELETE -H "Authorization: token $TOKEN" "https://api.github.com/repos/compand-lang/EShot-Overlay/releases/$OLD_REL" -o /dev/null
  fi

  REL_ID=$(REL_NEW="$NEW" python - "$TOKEN" <<'PYEOF'
import json, os, sys, urllib.request
token, new = sys.argv[1], os.environ["REL_NEW"]
body = (
    f"## EShot Overlay {new}\n\n"
    f"Сборка на базе апстрима **Benoks/EShot {new}** с нашими доработками.\n\n"
    "### Наши функции\n"
    "- **OCR в оверлей** — кнопка «Распознать текст» на нижней панели: текст на тёмной полупрозрачной плашке, копируется кнопкой в углу области\n"
    "- **Перевод текста (OCR overlay)** — вторая кнопка: распознаёт и сразу переводит текст оверлея\n"
    "- **Переводчик** — в трее и на горячую клавишу (задаётся в настройках)\n"
    "- **Автоопределение языка OCR** — все 13 языковых паков\n\n"
    "**Скачайте `EShot-overlay-windows-x64.zip`, распакуйте в новую папку и запустите `EShot.exe`.**"
)
req = urllib.request.Request(
    "https://api.github.com/repos/compand-lang/EShot-Overlay/releases",
    data=json.dumps({"tag_name": new, "name": f"EShot Overlay {new}", "body": body}).encode(),
    headers={"Authorization": f"token {token}", "Accept": "application/vnd.github+json"},
)
try:
    with urllib.request.urlopen(req) as r:
        print(json.load(r)["id"])
except Exception:
    print("")
PYEOF
)
  [ -n "$REL_ID" ] || { echo "WARN: не удалось создать релиз"; return 1; }

  curl -s -L -X POST -H "Authorization: token $TOKEN" -H "Content-Type: application/zip" \
    --data-binary @"$ZIP" \
    "https://uploads.github.com/repos/compand-lang/EShot-Overlay/releases/$REL_ID/assets?name=EShot-overlay-windows-x64.zip" \
    | python -c "import json,sys; a=json.load(sys.stdin); print('asset:', a.get('name'), a.get('size'), a.get('state',''))" 2>/dev/null
  echo "RELEASED: https://github.com/compand-lang/EShot-Overlay/releases/tag/$NEW"
}


# --- 5. коммит, push, ждём CI ------------------------------------------------
git add -A
git commit -q -m "Port our overlay OCR/translate features to upstream $NEW

Auto-ported by tools/port-upstream.sh (base $BASE_TAG -> $NEW)." || true
printf '%s\n%s\n' "$NEW" "$NEW_SHA" > "$BASE_FILE"
git add "$BASE_FILE"
git commit -q -m "Bump .port-base to $NEW" || true

if [ "${SKIP_PUSH:-0}" = "1" ]; then
  echo "SKIP_PUSH=1: ветка port-$NEW готова локально, push/CI пропущены"
  git checkout main >/dev/null 2>&1
  rm -rf "$TMP"
  exit 1
fi

git push -q -f origin "port-$NEW"

TOKEN=$(printf "protocol=https\nhost=github.com\n\n" | git credential fill 2>/dev/null | grep '^password=' | cut -d= -f2)
[ -n "$TOKEN" ] || { echo "ERROR: no github token"; exit 4; }

# ждём появления прогона для нашей ветки (до ~3 мин); если не появился —
# пробуем workflow_dispatch (актуально, когда workflow на ветке ещё без триггера port-*)
RUN_ID=""
find_run() {
  curl -s -H "Authorization: token $TOKEN" "https://api.github.com/repos/compand-lang/EShot-Overlay/actions/runs?per_page=5" \
    | python -c "import json,sys
rs=json.load(sys.stdin)['workflow_runs']
m=[r for r in rs if r['head_branch']=='port-$NEW' and r['status']!='completed']
c=[r for r in rs if r['head_branch']=='port-$NEW' and r['status']=='completed']
print((m or c)[0]['id'] if (m or c) else '')" 2>/dev/null
}
for i in $(seq 1 18); do
  sleep 10
  RUN_ID=$(find_run)
  [ -n "$RUN_ID" ] && break
done
if [ -z "$RUN_ID" ]; then
  echo "== no run yet, dispatching workflow manually =="
  curl -s -X POST -H "Authorization: token $TOKEN" -H "Accept: application/vnd.github+json" \
    -d "{\"ref\":\"port-$NEW\"}" \
    "https://api.github.com/repos/compand-lang/EShot-Overlay/actions/workflows/windows-esov-build.yml/dispatches" -o /dev/null -w "dispatch: %{http_code}\n"
  for i in $(seq 1 12); do
    sleep 10
    RUN_ID=$(find_run)
    [ -n "$RUN_ID" ] && break
  done
fi
[ -n "$RUN_ID" ] || { echo "ERROR: CI run not found for port-$NEW"; exit 4; }
echo "== CI run $RUN_ID, waiting =="

CONCLUSION=""
for i in $(seq 1 90); do
  sleep 10
  CONCLUSION=$(curl -s -H "Authorization: token $TOKEN" "https://api.github.com/repos/compand-lang/EShot-Overlay/actions/runs/$RUN_ID" \
    | python -c "import json,sys; r=json.load(sys.stdin); print(r['status'], r['conclusion'] or '-')" 2>/dev/null)
  case "$CONCLUSION" in completed*) break;; esac
done
echo "CI: $CONCLUSION"

if [ "$CONCLUSION" = "completed success" ]; then
  git checkout main >/dev/null 2>&1
  git merge --ff-only "port-$NEW" >/dev/null 2>&1 || { echo "ERROR: ff-merge failed"; exit 4; }
  git push -q origin main
  git branch -D "port-$NEW" >/dev/null 2>&1
  rm -rf "$TMP"
  echo "PORTED: main обновлён до $NEW и запушен"
  publish_release "$RUN_ID" || echo "WARN: релиз не опубликован (сборка в main всё равно готова)"
  exit 1
else
  echo "CI-FAILED: см. https://github.com/compand-lang/EShot-Overlay/actions/runs/$RUN_ID (ветка port-$NEW сохранена)"
  rm -rf "$TMP"
  exit 3
fi
