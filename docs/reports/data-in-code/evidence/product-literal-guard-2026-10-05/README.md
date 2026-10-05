# R42 — pełne dowody odczytowego strażnika

Pełny sprawdzian repo jest **negatywny**: exit 1, `valid:false`. Pozytywny wynik
41 sprawdzianów mechanizmu nie oznacza odbioru R42 dla produktu. Źródłowy HEAD
capture to `1b7379c80e62e30e58e97c4ba84c490d9ddbc6f4`; nowe, jeszcze niecommitowane
w chwili odczytu pliki są związane indywidualnymi hashami w receipt.

- `R42-primary-excerpt.md` i receipt: dokładny wymóg z commitu
  `3cd5848e7484d50d00f19f0855bc2cd228ad7f3b`, linie 483–515.
- `strict-report.json.gz`: **pełny** surowy raport 31 468 048 bajtów, gzip z
  `mtime=0` i bez nazwy pliku; rozpakowanie zachowuje dokładne bajty. Nie jest
  próbką ani skrótem. `strict.stdout`/`strict.stderr` są oryginalnymi strumieniami.
- `strict-manifest.json`: wszystkie 363 pliki, w tym 24 jawne wykluczenia
  tests/fixtures i 57 dodatkowych publicznych nagłówków; brakujące opcjonalne
  korzenie Androida są jawne. Każdy produktowy plik ma hash albo blokadę odczytu.
- `owner-debt.json`: liczby i komplet wierszy plików z właścicielem wyznaczonym
  najdłuższym prefiksem danych; fallback 9 oznacza potrzebę przydziału, nie
  rozstrzygnięcie własności współdzielonych handlerów.
- `actual-capture-receipt.json`: dokładna komenda, exit, hashe narzędzia,
  kontraktu, packa, generatora, wyników i źródeł tests.
- `source-snapshot-before.json`/`source-snapshot-after.json`: identyczne hashe
  390 plików produktu i wejść; zero zaobserwowanych zmian bajtów. Nie jest to
  ślad syscalli. Capture nie uruchamia sieci ani zapisu generatora.
- `synthetic-run-2.log`: oryginalny ujemny przebieg (33 przypadki, dwa błędy).
  `synthetic-run-3.log` do `synthetic-run-6.log`: oddzielne oryginalne przebiegi
  zielone; końcowy run 6 ma 41 przypadków i 39,901 s. Nie nadpisują wyniku ujemnego.
- `recovered-run1/`: literalne trzy kompletne decoded output chunks narzędzia
  (bez tool truncation), połączone do 40 897 bajtów. Nie są osobnymi stdout/stderr
  ani deklaracją oryginalnych bajtów PTY/pipe. Zachowują standardowe skróty repr
  samego unittest. Tymczasowe source/policy/schema oraz dynamiczny stan run 1
  nie zostały utrwalone i nie są rekonstruowane jako fałszywy snapshot.
- `negative-run-2-fixture/`, `replay_negative_run2.py`, `negative-run2-replay.stdout`:
  dokładne źródło `"preserved";\n` i błędny reason odtwarzają lukę dawnej gałęzi
  zapisu raportu na tymczasowym drzewie. Stara gałąź nadpisuje źródło, obecna
  zachowuje oryginalne bajty/wyjście 2. Dodatnia rewizja 2 w pustym zakresie jest
  poprawnym wejściem i daje exit 1, co ujawnia drugi błąd w oczekiwaniu starego
  testu. Replayer zawiera **dawną gałąź zapisu i obecną walidację**, nie
  odzyskany pełny historyczny plik strażnika. Oryginalne tymczasowe fixture
  runu 2 nie przetrwały; retained fixture ma identyczne źródło/kotwicę/mutacje,
  a pomocniczy rejestr i schemat są jawnie obecne, nie pozorowane jako stary snapshot.
- `historical-recovery/`: literalne odwrócenie późniejszych patchy odzyskało
  cały guard run 3 **dokładnie** zgodny z wcześniej zapisanym SHA `1e076884…b72`.
  Usunięcie jawnego patcha ochrony po run 2 wyprowadza pełny source run 2
  SHA `6278d048…ade2`; pierwotnego niezależnego hasha run 2 nie było.
  Stary schema też odpowiada zapisanemu wcześniej SHA `fbdffaf6…43d`.
  `source-recovery-receipt.json` zachowuje wszystkie literalne operacje.
  `replay_full_guard_run2.py` uruchamia **cały** zachowany guard przez subprocess
  w osobnych tymczasowych repo narzędzia i wejścia. Wynik
  `full-guard-run2-replay.stdout` odtwarza oba przypadki; jest nowym replay,
  a nie zastępstwem oryginalnego ujemnego logu. Nie zmienia produktu.
- `recovered-test-sources/`: cały plik testu run 3 odzyskany przez literalne
  odwrócenia i zweryfikowany wcześniejszym niezależnym SHA `d5436310…b3bca`,
  31 633 bajty. Receipt zachowuje dokładne operacje; bieżący plik testu pozostał
  bez zmian. Run 2 jest osobną kopią history-derived z pięcioma literalnymi
  inverse hunks, SHA `b9a7a1b0…f38ab`; nie ma starego niezależnego targetSHA,
  ale obie linie tracebacków zgadzają się z oryginalnym ujemnym logiem.
  Nie przedstawiamy nieutrwalonych dawnych tempfixtures jako snapshotu.
- `review-freeze.json` i `review-final-source-freeze.json`: kolejne tożsamości
  robocze przed audytem, nie wynik sprawdzianu repo.

Odtworzenie pełnego audytu na opublikowanej gałęzi:

```sh
python3 -B docs/reports/data-in-code/evidence/product-literal-guard-2026-10-05/capture_product_literals.py \
  --repo-root "$PWD" --output /tmp/product-literals-replay
python3 -B docs/reports/data-in-code/evidence/product-literal-guard-2026-10-05/replay_negative_run2.py
python3 -B docs/reports/data-in-code/evidence/product-literal-guard-2026-10-05/historical-recovery/replay_full_guard_run2.py
python3 -B loom/tests/compat/test_product_literal_guard.py -v
```

Skrypt capture zwraca 0, gdy rzeczywisty strażnik zwrócił oczekiwany **1** i
źródła pozostały identyczne; nie zmienia to negatywnego wyniku strażnika.
`--expected-source-head` jest opcjonalnym przypięciem konkretnego historycznego
HEAD. Wynik powtórki należy zapisać w osobnym katalogu; pierwszego negatywnego
wyniku nie wolno zastąpić późniejszym. Pełny JSON można odczytać przez
`gzip.decompress` albo `gzip -dc strict-report.json.gz`. Wszystkie dane są
syntetyczne lub pochodzą z publicznych źródeł repo. Nie odczytano holdoutu.

## Do wątku N

- **Do wątku 8:** fixtures są zielone; pełny CLI `--check` pozostaje blokadą
  repo do indywidualnej klasyfikacji przez właścicieli.
- **Do wątku 9:** zachowaj pełne negatywne logi/JSON/fixture i granicę między
  przeglądem mechanizmu, wynikami fixtures a odbiorem R42 całego produktu.
