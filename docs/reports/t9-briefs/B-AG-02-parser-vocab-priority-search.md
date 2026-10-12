# B-AG-02 — AGEDS: słowniki parsera, profil kolejki, profil wyszukiwania jako dane z `produced_by`

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/AGEDS` @ `9c1d513b`; potwierdzić na bieżącym `main` |
| Fala / priorytet | W2 / P2 |
| Ustalenia | EA-AGEDS-004, EA-AGEDS-005, EA-AGEDS-009 |
| Niezmienniki EITG | G2, G6, G9 |
| Rozmiar / model | M / Sonnet 5.5 high |
| Zależności | B-AG-01 (hash receptury, `effective_recipe()`) |
| Worktree | `t9-ag-02` |

## Zakres
- Słowniki nagłówków, rozpoznawanie numerów (telefon) i nazw plików jako metoda parsera + słownik
  domenowy + profil formatu; każda obserwacja ma `produced_by` z wersją metody; `raw_value`
  niezmienne; hipotezy pozostają `unverified` (EA-AGEDS-004).
- Priorytet kolejki: profil z wagami i tie-breakerem zamiast kolejności wyboru i zaszytych wag;
  już zakolejkowane zadania zachowują porządek, chyba że użytkownik zleci repriorytetyzację
  (EA-AGEDS-005).
- Wyszukiwanie: profil metody (ranking BM25, tokenizer, snippet, sufity) + polityka zasobów
  + konfiguracja/wersja indeksu; zmiana tokenizera = jawny reindex z zachowaniem wersji
  (EA-AGEDS-009).

## Pliki
- `server/app/scanner.py:65–123` (`_kind_for_header`, `_phone`, `filename_observations`, `_Scan.observe`).
- `core/src/commonMain/kotlin/dev/klbt/ageds/core/Models.kt:87–116` (`TranscriptionPriority.score`);
  `androidApp/src/main/java/dev/klbt/ageds/MainActivity.kt` (`EvidenceVm.uploadAndQueue`);
  `server/app/jobs.py` (`claim_job`).
- `server/app/search.py:7–12` (`validate_query`, `search_page`); `server/app/main.py` (`api_search`,
  `home`); `server/app/db.py` (`init_db`).

## Mechanizm docelowy (istniejący)
Obserwacje mają już `scanner_version` + `basis` i status `unverified` (jawność epistemiczna
działa — audyt). Rozszerzenie: dokładna receptura/hash zamiast samej wersji; słownik jako plik
danych serwera; profil kolejki obok receptury ASR; konfiguracja indeksu z wersją w `db`.

## Kroki
**Rozważenie.** Potwierdzić lokatory; rekord decyzji: obecne słowniki, wagi i ranking = presety
`legacy` bajt w bajt; zmiana parsera nigdy nie przepisuje starych wyników (pokazuje różnice).

**Implementacja.** (a) test: dodanie wpisu `telefono` w pliku metody zmienia interpretację bez
zmiany algorytmu; `raw_value` identyczne; nowy wynik przypina wersję metody; (b) profil kolejki:
test: dwa profile odwracają kolejność dwóch syntetycznych zadań bez edycji kodu; UI pokazuje
wybrany profil i efektywny wynik; (c) profil wyszukiwania: test: zmiana dozwolonego profilu
wyniku i rankingu wpływa na syntetyczne zapytanie bez edycji algorytmu; migracja tokenizera
wymaga jawnego reindex z zachowaniem wersji; walidacja zapytań nie dopuszcza SQL injection
(konfiguracja nie może wyłączyć integralności — ryzyko audytu).

**Optymalizacja.** Słownik parsera i słownik wyszukiwania z jednego źródła, jeśli to te same
terminy; koszt reindeksacji ujawniony w UI.

**Iteracja.** Pozostałe moduły parserów (`server/app/importers/*`, `read_pages.py`, `verified_*`)
z punktu wznowienia audytu — te same wzory.

## Kryterium zamknięcia (EA-AGEDS-004/005/009)
- Trzy testy akceptacyjne z audytu PASS; `pytest` zielony; sufity wyszukiwania są ustawieniem
  z obecną wartością, przekroczenie jawnie zgłaszane.

## Czego nie robić
- Nie promować hipotez `unverified` do faktów przez samą zmianę słownika.
- Nie zmieniać kolejności już zakolejkowanych zadań bez jawnego polecenia.

## Co zapisać
Rekord decyzji; hashe presetów; liczba wpisów słowników wyniesionych z kodu.
