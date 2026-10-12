# B-ECO-02 — Strażnik drugiej pamięci (G1/G5), analog strażnika literałów R42

| Pole | Wartość |
|---|---|
| Repo / baza | `klb-t/chatadhd` (narzędzie i pierwsze zastosowanie); potem konfiguracja per repo |
| Fala / priorytet | W3 / P2 |
| Ustalenia | wzorzec C z `ECOSYSTEM_INTEGRATION` §1.2: WD-004, EA-AGEDS-007, CH-009, LEM-007, EA-AGEDS-006, CKP-A-006, A4-CH-MOD001 — strażnik ma je wykrywać mechanicznie |
| Niezmienniki EITG | G1, G5, G6; D-G4 |
| Rozmiar / model | M / Sonnet 5.5 high |
| Zależności | T5 (inwentarz magazynów = allowlista początkowa) |
| Worktree | `t9-eco-02` |

## Zakres
Test, który skanuje kod produktu na wywołania trwałego zapisu/odczytu i wymaga, żeby każde było
zadeklarowane w packu magazynów z rolą (`primary` / `cache` / `projection`) i, dla ról innych niż
`primary`, z przepisem odbudowy. Deklaracja, nie zakaz (R43: inicjator `owner_rule` R15).

## Pliki
- Wzór: `loom/src/util/product_literal_guard.py`, `loom/data/validation/product_literals.pack`,
  `loom/tests/compat/test_product_literal_guard.py`.
- Nowe: `loom/src/util/second_memory_guard.py`, `loom/data/validation/storages.pack`
  (schemat `loom.storage_declarations/1`: skanery per język jako dane — JS/TS
  `localStorage|sessionStorage|indexedDB`, Kotlin `SharedPreferences|Room|openFileOutput`,
  Python `open(...,'w'|'a')|sqlite3.connect|json.dump`, C++ `std::ofstream|sqlite3_open|fopen`;
  allowlista: ścieżka, zakres, id węzła magazynu z inwentarza T5, rola, przepis odbudowy, powód),
  `loom/tests/compat/test_second_memory_guard.py`.
- Pierwsze trafienia do sklasyfikowania w `chatadhd` (z audytu): `loom/web/src/profiles/workflow-checkpoint.ts:10–47`
  (localStorage), `workflow-snapshot.ts:7–31` (klucz config), `engine/models.py:28–81` (cache
  modeli), `engine/memory_engine.py` (`memory.json`), `engine/config.py` (`config.json`,
  `secrets.json`), BlobStore, packet store, logi.

## Mechanizm docelowy (istniejący)
Ten sam szkielet co strażnik literałów: skanery jako dane, allowlista z kategorią i powodem,
tryb `--check` i pełny raport z liczbami `classified / unclassified / blocked`. Węzły magazynów
z T5 (EITG §6) są słownikiem dozwolonych celów.

## Kroki
**Rozważenie.** Zebrać inwentarz T5; ustalić listę wzorców per język (dane); zdecydować, czy
pierwsze uruchomienie jest `negative` jak strażnik literałów (raport bez bramy) — tak, do czasu
sklasyfikowania wszystkich trafień `chatadhd`; zapisać liczby z mianownikiem.

**Implementacja.** Skaner + pack + test; klasyfikacja trafień `chatadhd` wpisami allowlisty
z rolą i przepisem; `localStorage` web jako `cache` z przepisem „odtwórz z węzła perspektywy
po ID” (to jest kryterium CH-009, które B-CH-05 realizuje — tu tylko deklaracja).

**Optymalizacja.** Deduplikacja wzorców między językami; raport jako wejście do raportu
ekonomii T4 (gdzie są kopie).

**Iteracja.** Konfiguracja per repo (`storages.pack` w każdym repo, ten sam schemat) jako część
briefów W3 innych repozytoriów; nowe trafienia po każdej fali.

## Kryterium zamknięcia
- `test_second_memory_guard.py` zielony na fixtures (pozytywne i negatywne, min. 20 przypadków).
- Pełny raport `chatadhd`: każde trafienie sklasyfikowane albo jawnie w `unclassified` z liczbą;
  brama w trybie `--check` przechodzi tylko dla sklasyfikowanych zakresów (jak R42).
- Żaden wpis allowlisty bez roli, węzła magazynu i powodu.

## Czego nie robić
- Nie blokować zapisu; strażnik to test, nie runtime.
- Nie osłabiać progu, żeby przeszło; pierwszy pełny wynik może być negatywny i zostaje zapisany.
- Nie klasyfikować testów i fixtures (R42).

## Co zapisać
Rekord decyzji; liczby trafień per repo i rola; lista `unclassified` z lokatorami.
