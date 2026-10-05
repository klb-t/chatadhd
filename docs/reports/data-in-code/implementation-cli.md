# Wątek 11 — CLI: aktywny profil, podgląd i trwałe nakładki

CLI korzysta z `loom/data/runtime/cli.pack` z nakładką użytkownika i udostępnia **profile list / inspect / validate / save**. Przegląd końcowy obejmuje działający kod i nowe asercje smoke, a nie wynik wykonania przyszłego buildu. Kontrola składni C++ z ostrzeżeniami projektu i `-Werror`, AST Pythona oraz whitespace diff są zielone; **wykonanie końcowego `cli.smoke` i pełnego `ctest` pozostaje do potwierdzenia po pełnym buildzie**. Ta część nie uruchamiała buildu ani płatnych wywołań.

## Włączona polityka i zgodność domyślna

Profil zawiera pomoc, 17 nazw flag bez wartości, 24 nazwy/aliasy poleceń, 13 grup z 51 domyślnymi nazwami podpoleceń, 14 presetów liczbowych, ustawienia prezentacji, tytuł nowej rozmowy/typ pamięci/format eksportu, klucze kohort oceny, wybór metryk grafu, aliasy i fallback poziomów logowania oraz interwał odpytywania anulowania watch.

| Obszar | Zastane wartości przeniesione do danych |
| --- | --- |
| Listy / wyszukiwanie | conversations50, graph nodes200, search20, tasks50, resume scan1000, sources100, artifacts100, catalog list100, eval scan1000000 |
| Kontekst | graph expand depth1; context depth0 i 4000 tokenów; chat depth2 jako fallback jawnie podanego parametru |
| Tworzenie | `New Chat`, typ pamięci `text`, eksport `markdown` |
| Prezentacja | szerokości100/70/80/60/160, prefiksy dat16/19/19/19/10, hash12, `…`, skala wyniku100 |
| Watch / logi | okres300s, odcinek snu1000ms, domyślnie warning, alias warn, nieznana nazwa → warning |

Jawne `--limit`, `--depth`, `--max-tokens`, `--type`, `--format` i inne obsługiwane argumenty wygrywają z presetem danej operacji. CLI ma własne jawne presety transportu; np. typ pamięci z `cli.creation.memory_node_type` nie jest automatycznie dziedziczony z `memory.pack`. Przy pominiętym `chat --depth` pole ChatOptions pozostaje pominięte i decyzję zachowuje wspólne API. Nie przedstawiamy tej migracji jako zakończonego scalenia selektorów wątku3.

Domyślna pomoc jest identyczna z dawnym `kUsage`: **4770 B przed i po**, SHA-256 `1efe7af10542ef871d475fae9b0e648372fe97dbc864f84e4ca07ac127ce8fbe`. Zachowane pliki [help-before.txt](evidence/cli/help-before.txt), [help-preset.txt](evidence/cli/help-preset.txt) i [static-review-receipt.json](evidence/cli/static-review-receipt.json) opisują porównanie do HEAD. Zbiór 17 flag również jest identyczny. To porównanie danych i kodu; nie jest całościową sondą bajtową wszystkich wyjść CLI.

Metryki `graph_stats` wybiera lista w danych, a wykonuje rejestr zaufanych zapytań SQL. Nie jest interpolowane SQL dostarczone przez użytkownika. Nieobsługiwana metryka zwraca jawne Unavailable. Nazwy aliasów poleceń/podpoleceń są danymi; zaufane identyfikatory handlerów pozostają zdolnościami kodu. Profil nie dodaje obsługi nieistniejącego polecenia.

## Polecenia profili i zapis

```sh
loom --data-dir /path/to/data profile list
loom --data-dir /path/to/data profile inspect cli
loom --data-dir /path/to/data profile validate cli --file settings.json
loom --data-dir /path/to/data profile save cli --file settings.json
```

`profile` bez podpolecenia oznacza list. Wyjście tych poleceń jest JSON. `inspect` zwraca efektywne `values`, `value_schema`, rewizję, hash i informację builtin. `validate` i `save` czytają `--file` albo cały stdin. Przyjmują zwykły obiekt nadpisań albo pełną kopertę `loom.runtime_profile_overlay/1` z `domain`, `overrides` i opcjonalnym `patch` RFC6902. Podgląd dotyczy RuntimeProfile; np. dodatkowe źródłowe słowniki KB archiwum są osobnym podglądem `ArchiveIntelligence::profile()`.

Walidacja obejmuje schemat RuntimeProfile; dla domeny CLI również natywny kontrakt rozmiarów i skali prezentacji. Nie jest zatwierdzeniem zużycia ani uniwersalnym wykonaniem wszystkich walidatorów operacji innych domen. Ich dodatkowe zależności i invariants obowiązują u konsumentów.

`save` najpierw ładuje aktualny profil, stosuje zmianę i sprawdza wynik. Następnie zapisuje różnicę **builtin → pełne efektywne wartości** jako kopertę z pustym `overrides` i `patch: Json::diff(...)` do `<data_dir>/profiles/<domain>.pack`. Dzięki temu usunięty klucz mapy pozostaje usunięty po późniejszym częściowym nadpisaniu. Obiekty scala się rekurencyjnie, a tablice zastępuje; usuwanie jest jawną operacją RFC6902 zgodną ze schematem.

Zapis używa istniejącego `fsutil::atomic_write`: plik tymczasowy i rename, z jego zastaną obsługą fsync. Błąd walidacji występuje przed zapisem i nie zastępuje poprzedniego pliku. `validate` nie zapisuje nakładki. Obecnie polecenia profili otwierają Runtime, więc mogą inicjalizować jego katalog/bazę i wykonywać zwykłą procedurę startu; nie należy reklamować ich jako operacji całkowicie pozbawionych zmian w katalogu danych. Wyjątek odczytowy opisany poniżej dotyczy pomocy, wersji i braku polecenia.

## Trzy poprawki z końcowego przeglądu

Wykryto i poprawiono trzy konkretne problemy przed finalnym buildem:

1. **Pomoc/wersja inicjalizowały katalog.** Bootstrap profilu wywoływał `resolve_data_dir`, który tworzy sentinel i katalogi. Teraz lokalny wybór odczytowy zachowuje kolejność: jawna niepusta ścieżka → `CHATADHD_DATA` → pierwszy kandydat z sentinelem → pierwszy kandydat. Jawne ścieżki/środowisko mają to samo rozwinięcie `~` i rozwiązanie ścieżki. Pomoc, wersja i brak polecenia nie wywołują `ensure_data_dir`. Niedirectoryjna składowa oznacza znany brak nakładki; istniejąca uszkodzona nakładka lub błąd odczytu pozostaje jawnym błędem. Zwykłe polecenia nadal otwierają Runtime, który inicjalizuje katalog zgodnie z dotychczasowym API.
2. **Schemat prezentacji miał wymyślone pułapy.** Usunięto limit INT32 dla ustawień obsługiwanych przez `size_t` i minimum `1e-300` dla skali. Rozmiary mają deklarowaną reprezentację uint64 i dodatkowo jawne, nieowijające sprawdzenie natywnego `size_t`. Skala ma schemat minimum0 oraz obowiązkowy kontrakt konsumenta: finite i ściśle dodatnia. `denorm_min` jest poprawne. Ten sam kontrakt działa przy bootstrapie oraz CLI validate/save, więc scale0 nie może zapisać nakładki blokującej następny start. Nie zmieniono domyślnej skali100 ani algorytmu formatowania wyniku.
3. **Alias podpolecenia profile był pomijany.** Rozwiązanie aliasów podpolecenia odbywa się przed specjalnym dispatch `profile`, więc np. `subcommands.profile.peekfixture = inspect` rzeczywiście działa.

Nowe asercje w `test_cli_smoke.py` sprawdzają trzy polecenia informacyjne z nieistniejącym jawnym katalogiem oraz każde z nich przy syntetycznym HOME tylko do odczytu i HOME wskazującym zwykły plik. Sprawdzają brak tworzenia katalogu. Pozostałe nowe sprawdzenia obejmują: efektywną pomoc i flagi, aliasy wersji/conv/profile, nowy tytuł rozmowy, preset limitu i pierwszeństwo `--limit`, validate bez zapisu nakładki, niezmienność pliku przy odrzuconym save/scale0, zapis i ponowny odczyt natywnych maksymalnych rozmiarów i najmniejszej dodatniej skali, usunięcie `.cpp` z mapy materialize przez RFC6902 i zachowanie tego usunięcia przy dalszym nadpisaniu, jawny błąd istniejącej uszkodzonej nakładki także dla wczesnego `--help`.

Te przypadki są gotowe do wykonania przez **rzeczywisty `cli.smoke` po finalnej regeneracji danych i buildzie**. Nie podano liczby „zaliczonych przypadków” na podstawie ich obecności w źródle. Wcześniejsze części smoke pozostały w całości; nie zmieniono progów ani limitu czasu testu.

## Status 42 pozycji i zakres CLI

Właściwy 42-pozycyjny fragment obejmuje worker/media/github/net/**oraz** CLI, a nie 42 pozycje samego CLI. Aktualny snapshot zbiorczego mappingu: **38 migrated, 2 partially-migrated, 2 retained-wire**; zachowany [migration-status-42-snapshot.json](evidence/cli/migration-status-42-snapshot.json).

| Grupa ID | Zakres | Status |
| --- | --- | --- |
| DIC-0173–0178 | worker | 4 migrated; 0176/0177 częściowe: wspólne prompty i recepta cen/zużycia |
| DIC-0179–0189 | media | 11 migrated |
| DIC-0190–0194 | github | 5 migrated |
| DIC-0195–0199 | net | 3 migrated; 0198/0199 zachowane kontrakty HTTP/URI/SSE |
| DIC-0500–0514 | CLI | 15 migrated w opisanym zakresie ustawień |

15 pozycji CLI dotyczą kolejno: pomocy, arności flag, prezentacji, limitów list/wyszukiwania, tytułu, eksportu, presetów kontekstu, typu pamięci, skanu resume, metryk grafu, limitu eval, kluczy kohort, watch, logowania i rejestru nazw poleceń. „Migrated” nie obejmuje wszystkich pozostałych angielskich diagnostyk ani uogólnienia każdego zapisanego formatu. DIC-0515 (import export-mode auto/off/on) jest w szesnastopozycyjnym inwentarzu pliku CLI, lecz należy do wątku5 i nie wchodzi do powyższych 42.

## Pozostałe ograniczenia i praca niewykonana

`tasks resume` nadal wykonuje pojedynczy skan z konfigurowalnym limitem, nie pełną paginację wszystkich tasków. `catalog eval` nadal wykonuje pojedyncze query z konfigurowalnym limitem; brak osobnej metryki pokrycia queried/total katalogu. Dalsze zmiany tych zachowań nie zostały przedstawione jako ukończone dzięki samej zamianie liczby na preset.

Pozostałe klucze i etykiety zapisu, statusy historyczne, protokoły sekretów, enumy importera oraz ustawienia Catalog należą do ich obecnych API/właścicieli. Rozwinięcie podglądu i scalenie ContextEngine/pamięci/selekcji wymagają współpracy z wątkiem3. CLI nie rozstrzyga automatycznie semantyki pominięcia względem jawnego zera we wszystkich request DTO; zachowany domyślny efekt nie jest pełnym rozwiązaniem ich wspólnego kontraktu.

## Do wątku N

- **Do wątku 2:** wystawić wspólny publiczny odczytowy wybór katalogu, aby usunąć lokalne powtórzenie historycznej kolejności kandydatów w CLI. Guard×10 musi korzystać z przewidywania operacji, bez przywracania stałych limitów CLI. Native pozytywna skala i size_t to reprezentacja/konsument, nie próg zużycia.
- **Do wątku 3:** nadal otwarte scalenie starego selektora/pamięci z ContextEngine, jednoznaczna semantyka pominiętego pola/jawnego zera oraz wspólny kontrakt requestów. Nie reinterpretować zapisanych historycznych wire/defaults przez bieżący profil; nie twierdzić, że CLI15 migrated zamyka tę integrację.
- **Do wątku 5:** DIC-0515, schema/capabilities ImportOptions dla nazw export-mode i ich wspólnej walidacji CLI.
- **Do wątku 10:** profile list/inspect/validate/save mogą stanowić bazę trybu eksperckiego; edytor ma respektować schema, RFC6902 removals, native kontrakty konsumentów i rozdział od zatwierdzania zużycia. Po zapisaniu aktywny profil ładuje następne wywołanie CLI.
- **Do wątku 9:** zregenerować embedding `cli.pack`, wykonać finalny build i rzeczywisty cli.smoke/full ctest, zachować logs/receipts także przy wyniku negatywnym. Ten raport potwierdza tylko przegląd, porównanie pomocy/flag i składnię końcowych źródeł. Zmiany trzech plików CLI są gotowe; pełna bramka pozostaje integratorowi.
