# Odtwarzanie audytu

Wszystkie komendy wykonuj na repozytoriach z `coverage-index.json`, przypiętych do wskazanych SHA main. Ścieżki poniżej są przykładami; ustaw własne `AUDIT`, `REPOS`, `RESULTS`. Nie używaj bieżącego ruchomego HEAD jako zamiennika bazy. Nie trzeba uruchamiać CI ani modeli.

## Skan i jego testy

`tools/ecosystem-audit-2026-10-09/README.md` podaje dokładny interfejs skanera. Dla każdego publicznego repo:

```sh
python3 tools/ecosystem-audit-2026-10-09/scan.py --repo "$REPOS/chatadhd" --repo-name klb-t/chatadhd --base-branch main --revision 9e20f99ab27e7cd45e1892f83bf60fbe60db3de9 --visibility public --output "$RESULTS/chatadhd"
python3 -m unittest discover -s tools/ecosystem-audit-2026-10-09/tests -v
```

Powtórz pierwszą komendę z repo i SHA z macierzy. Zestawienie sum kontrolnych jest w `*/scan/retention.json`. `inventory.jsonl.gz` to gzip z mtime=0; po rozpakowaniu hash odpowiada podanemu hash inventory. Kandydaty są wynikami pośrednimi do regeneracji. Skaner nie uznaje kandydata automatycznie za naruszenie ani nie czyta chronionych ścieżek. Python 3.10+ i Git wystarczają do skanu. W tej sesji: Python 3.12.14, 12 testów PASS; powtórzenie z katalogu publikacji również PASS.

## Kontrola raportów

```sh
python3 tools/ecosystem-audit-2026-10-09/validate_reports.py --repos-root "$REPOS" --reports-root "$AUDIT" --output "$RESULTS/report-validation"
```

`REPOS` ma zawierać klony sześciu publicznych repo pod ich nazwami. `AUDIT` wskazuje `docs/reports/ecosystem-audit-2026-10-09`. Walidator nie importuje produktu. Sprawdza lokatory źródeł na zapisanych SHA, wymagane pola, identyfikatory i hashe wersji skanera. Nie ocenia ponownie semantyki. Pierwsze uruchomienie przenośnej wersji ujawniło brak tworzenia katalogu output; naprawiono to w narzędziu audytowym, po czym otrzymano PASS (65 findings, 212 lokatorów, 0 błędów/ostrzeżeń).

## Próby zachowania

Testy audytu używają syntetycznych danych i atrap granic transportu. PASS może potwierdzać bieżący błąd; testy akceptacyjne przyszłych napraw są osobno w findings/backlog.

chatadhd — rzeczywiste jednostki Pythona, bez UI. Potrzebny `requests`; w audycie zainstalowano requests 2.34.2 w odrębnym katalogu scratch. Brakująca zależność została jawnie rozwiązana przed wykonaniem 10 prób. Nie podawaj kluczy modeli.

```sh
python3 "$AUDIT/chatadhd/probes/behavior_probe.py" "$REPOS/chatadhd" > "$RESULTS/chatadhd-behavior.json"
```

chatadhd — oryginalny natywny runner przy bazowym SHA, kompilator C++20, bez pełnego linkowania serwera. W cwd repo:

```sh
python3 loom/src/context/tests/run_runtime_preset_test.py /tmp/unused-audit-build --mode layers --standalone-utilities --w11-ref 9e20f99ab27e7cd45e1892f83bf60fbe60db3de9 --w12-ref 9e20f99ab27e7cd45e1892f83bf60fbe60db3de9 --evidence-dir "$RESULTS/chatadhd-native"
```

Manifest w `chatadhd/probes/native` dokumentuje faktyczne źródła/utilities; nie jest dowodem pełnego buildu CMake ani native store/UI E2E. Wynik: 9 przypadków/168 asercji oraz generator 13-string roundtrip.

Watchdog — uruchomiony lokalnie lint/typecheck i 53 istniejące testy; szczegóły `Watchdog-JH16/validation.json`. Po przygotowaniu zależności według lockfile (w sesji `npm ci --ignore-scripts --no-audit --no-fund`), z cwd repo:

```sh
WATCHDOG_REPO="$PWD" node --import tsx --test "$AUDIT/Watchdog-JH16/probes/behavior.test.ts"
```

AGEDS — potrzebne zależności testowe repo; 6 prób z atrapą Whisper i syntetycznymi wejściami. Zachowano również pierwszy receipt brakującego anyio.

```sh
python3 "$AUDIT/AGEDS/audit-tools/semantic_probes.py" --repo "$REPOS/AGEDS" --output "$RESULTS/ageds-probes.json"
```

Standalone loom — kompilatory C/C++, istniejąca amalgamacja SQLite z bazowego chatadhd (hash w `loom/verification.json`). Nie ma pobierania zależności w runnerze:

```sh
python3 "$AUDIT/loom/checks/run_native.py" --repo "$REPOS/loom" --sqlite-dir "$REPOS/chatadhd/loom/third_party/sqlite" > "$RESULTS/loom-native.json"
python3 "$AUDIT/loom/checks/reproduce_sql.py" "$REPOS/loom"
```

Natywna reprodukcja jest silniejszym dowodem sześciu wskazanych zachowań niż SQL probe.

CKP — walidator trzech JSON profili uruchomiony, ale nie dowodzi runtime. LEM i CKP — Android/Gradle/Kotlin nieuruchomione z powodu braku kompilatora; nie publikujemy fikcyjnych wyników.

Prywatny audit ma własne komendy i receipts w prywatnym repo. Ten pakiet nie czyta ani nie kopiuje jego zawartości.
