# Wątek 11 — analyzer, graph, materialize: profilowanie i dowód zgodności

Baza żądana: `161cc22dfb84fe863389d6b90323bd44516a68dc`. Inwentarz zakończono przed zmianami kodu (bramka rodzica `bab7be3`): 44 pliki w przydzielonym audycie, 64 grupy danych do migracji (59 wątku 11, 5 wątku 1), 14 osobnych grup niezmienników. Ten raport obejmuje wykonany podzbiór analyzer/graph/materialize, nie deklaruje migracji całego repo.

| Obszar | Przed | Po |
|---|---|---|
| Analyzer | Osobny generowany blob: 11 regexów encji, 7 słowników tematów, 2 regexy relacji; progi/confidence w C++ | Jeden descriptor `semantic_analyzer.pack`, te same uporządkowane wartości domyślne; zastępowanie tablic, parametry i per-pattern confidence; stary blob usunięty |
| Graph ingest | Progi 10/2/0.3, limit 9999, nazwy rodzajów/relacji i wagi w C++ | `graph_ingest.pack`, wiązania pól JSON i ustawienia; checked API przed pierwszym zapisem; 0 reindex-limit oznacza brak limitu |
| Graph context | Tekst nagłówka/wiersza, 30/500/40, 500, 4000, 1.0/0.8/10 i szacunek 4 w C++ | `graph_memory.pack`, szablony, kolejność/kanały/budżety/wagi; pominięte pola żądania JSON używają efektywnego profilu; obecne pola wygrywają |
| Materialize | 14 rozszerzeń→6 języków, angielski tekst4 dokumentów i diagnostyki, limity detektorów | `materialize.pack`: te same mapy/szablony/ustawienia, konfigurowalne nazwy i MIME; hash zmienionego profilu w pochodzeniu i input_hash |

Descriptory używają `loom.runtime_profile/1`; użytkownik zapisuje `profiles/<domain>.pack` pod katalogiem danych (`loom.runtime_profile_overlay/1`, `domain`, `overrides`). RuntimeProfile rodzica scala obiekty, zastępuje tablice/skalary i waliduje schema. W swoich konsumentach pełne wartości wstrzykniętego profilu są ponownie sprawdzane dokładnie przeciw builtin descriptorowi; luźniejszy schema o tej samej domenie nie omija walidacji. Błędy mają `Result/Error`; checked API nie wraca do presetu po uszkodzeniu nakładki. Stare graph API zachowują kształt odpowiedzi i jawnie logują błąd, więc nowi konsumenci powinni wybrać checked warianty.

Szablony korzystają wyłącznie z inertnego `{{name}}` / `{{/pointer}}`: tekst wartości nie jest ponownie interpretowany; brak parametru jest błędem. Nie wykonują kodu. Domyślne JSON i teksty pozostają zgodne; metadane hash dodawane są tylko dla profilu różnego od builtin, aby nie zmienić istniejących wyników domyślnych.

## Pomiary i weryfikacja

Probe `loom/tests/compat/profile_semantics_graph_materialize_parity.cc` używa tylko starych publicznych API oraz danych syntetycznych. Obejmuje13 tekstów, 5 jawnych progów tematów na tekst, pełny JSON reguł,5 kontrolowanych etykiet grafu, relacje/wagi, kontekst z długim tytułem i520 znakami oraz estymację tokenów. W pełnym wariancie sprawdza również 4 produkty materializacji; normalizuje losowy run id i pomija zależny od niego product input_hash.

Wariant CORE_ONLY: **9 990 bajtów przed i po, zgodność bajt w bajt**; SHA256 obu wyników `cf3c0ad78fe3660e49e6e5b7fa41c7465cf6ac16b6ec6ac5bd44a56ddd9688e3`. Wariant z czterema produktami został uruchomiony po stronie starej: 11 766 bajtów, SHA256 `0db70356e9c30dfb937353908f2f6c79f464215f386d1a4fad5a5f86768b787d`. **Aktualny pełny wariant i porównanie z dokładną bazą161cc22 czekają na wspólny pełny build rodzica**; nie są tu oznaczone jako zaliczone.

Natychmiastowy test użył wcześniej zbudowanego archiwum9d15d2d. `git diff 9d15d2d 161cc22 --` dla wszystkich przydzielonych źródeł i publicznych nagłówków jest pusty. To pomocniczy dowód; nie zastępuje pełnego ctest/referencji161cc22. Aktualne obiekty analyzer/graph_engine/graph_memory/runtime_profile i memory-profile połączono z bibliotekami statycznymi starego buildu, bez starego Runtime i bez produktów materialize, aby uniknąć mieszania układów obiektów. Flagi: `c++ -std=c++20 -O0 -g0 -DLOOM_PARITY_CORE_ONLY`; biblioteki `libloom_core.a`, `libloom_sqlite3_amalgamation.a`, `libloom_miniz.a`, `-lssl -lcrypto -lpthread -ldl`. Probe i testy jawnie używają starego konstruktora `TempDir("loom_")`.

Focused suite `runtime_recipe_consumers`: **6/6 przypadków,36/36 asercji**,0 pominiętych. Sprawdza zastępowanie reguł i confidence, nowe etykiety/relacje i wagi, błąd przed mutacją, błędny regex, konfigurację szablonów i brak rekursji, zachowanie pominiętych pól kontekstu oraz obronę przed luźnym wstrzykniętym schema. Siódmy przypadek materialize wymaga świeżego Runtime i jest w tym pomocniczym przebiegu wyłączony makrem; pozostaje w normalnym pełnym teście. Syntax-only przydzielonych modułów oraz `git diff --check` przeszły. Nie uruchomiono drugiego pełnego buildu ani wywołań płatnych.

Dowody: `evidence/semantics-scoped-receipt.json` zawiera flagi, hashe źródeł i zakres; obok znajdują się oba core JSON, pełny stary JSON i wynik focused suite. Zachowano negatywne wyniki: początkowa nowa asercja błędnie oczekiwała jednej encji dla adresu e-mail (builtin zwraca również domenę), oraz pomocniczy link po zmianie nagłówka TempDir przed wyborem starego konstruktora. Pierwszy wynik odtwarza się przywracając `CHECK(extract_entities("admin@example.com").size() == 1)`; poprawiony test sprawdza obecność e-maila, a zgodność domyślna jest sprawdzana niezależnym probe.

## Pozostałe literały semantic/re

Dokładny przegląd w `semantic-regex-remaining.json` (13 grup). Mapowania flag i regexowe syntax/opcodes są niezmiennikami gramatyki/VM. Pola JSON są istniejącym kontraktem wire. `source=regex` jest prawdziwym pochodzeniem wykonawcy. Stare stałe nazw encji/relacji pozostają aliasami zgodności, a reguły używają dowolnych stringów. Legacy DTO i decoder zachowują1.0/0.5; wykonanie profilu zapisuje wcześniej rozstrzygnięte wartości. Wyciąganie encji z grupy1/całego dopasowania i relacji z grup1/2 jest istniejącą operacją DSL; alternatywne capture mapping wymaga additive capability schema.

Rodzic przeniósł VM step_limit do `re.pack` (domyślnie10 000 000, walidowane compile/with_profile, inspection). Pozostaje integracja katalogu danych konsumenta i preflight×10. Akumulacja dziesiętnego quantifier do `int` w regex.cpp:264–267 nie sprawdza overflow: to otwarta poprawność parsera, poza tą migracją danych.

`Config.graph_memory_max_nodes/depth` nadal wygrywa z profile fallback default_max_nodes/default_depth, zgodnie z dotychczasowym API. Config wypełnia20/2 również na świeżej instalacji i nie udostępnia rozróżnienia persisted/preset. Te limity są ustawieniami Config, lecz sam overlay fallback ich nie przestawi. Dowolne wartości GraphSelectOptions mają pierwszeństwo zgodnie z zero_as_default. Nie zmieniano obcego core/config.

W materiale zachowano4 zaufane operacje rendererów i3 uniwersalne detektory. Profile zmieniają ich dane, języki, teksty i limity; nie dodają nowego wykonywalnego renderera. Nie powstał tutaj rejestr metod selektora ani kompilator graph-reply, bo należą do innych wątków.

## Do wątku 2

Profile zawierają wszystkie wyżej wymienione presety, z opisami0/unlimited tam, gdzie obsługiwane. Zużycie i VM budgets wymagają podpięcia UsagePolicy przed kosztowną operacją; implementacja policy/x10 leży w wątku 2. Potrzebne API Config rozróżniające jawny wybór użytkownika od presetu, jeśli nakładki default_max_nodes/default_depth mają zmieniać efektywne domyślne limity przy zachowaniu precedence.

## Do wątku 1

`SemanticAnalyzer::create_from_data_dir`, `create_with_profile`, `to_unified_profile` i `profile_hash` są dostępne. GraphEngine/GraphMemory używają ich w ścieżce regex. SemanticLLM nadal ma builtin analyzer z Runtime dla merge/fallback; dlatego nie przypisano mu fałszywego custom hash. Podłącz efektywny analyzer w całej ścieżce SemanticLLM, wraz z własnymi prompt/request hash. Branding headers w semantic_llm.cpp:130–132 pozostają w handoff do właściciela pełnego request composer; nie edytowano promptów, ich parametrów ani semantic_llm.

## Do wątku 3

Używaj `select_context_checked`, `extract_seed_labels_checked` i checked GraphEngine ingest; ContextSelector propaguje `get_active_context_checked` z MemoryEngine. JSON pominięcia kanałów i max_tokens respektują profile; `from_json_with_profile` daje jawnie rozstrzygnięty request. Bezpośredni C++ ContextRequest jest traktowany jako jawny wybór swoich pól. Konfiguracja kanałów/kolejności/budżetów tutaj jest gotowa do użycia przez twój rejestr metod, a fallback Config precedence wymaga uzgodnienia z wątkiem2.

## Do wątku 8

Usunięty `semantic/builtin_rules.json.inc` był osobnym wygenerowanym kopią reguł. Generator `gen_semantic_rules.py` i stare ślady referencji należy wycofać w porządkach; runtime defaults są osadzane przez jeden generator runtime profiles rodzica.

## Do wątku 9

Przed integracją uruchom pełne ctest, wszystkie7 nowych przypadków consumer oraz oba pełne probe z identycznymi bibliotekami baseline 161cc22/current; odśwież receipt z wynikiem4 produktów. Rodzic dodał materialize profile hash do fingerprint przed lookup w knowledge/engine.cpp; wymaga to wspólnej końcowej weryfikacji. Agent nie commitował/pushował i nie edytował STATE/README/main.

## Do wątku 10

Schema w każdym descriptorze opisuje nadpisywalne pola, jednostki i konsumentów; RuntimeProfile inspection udostępnia descriptor, wartości i hash. Edycje zapisuj do `profiles/<domain>.pack`, sprawdzaj przez load/checked API przed użyciem. Graph i materialize czytają profil na każde wywołanie; analyzer jest niezmienny i trzeba stworzyć nową instancję po zmianie. Dostępne ustawienia obejmują słowniki/regexy/confidence, bramki/etykiety/bindings grafu, kontekst/kanały/szablony, mapy języka i treść raportów. Nie przedstawiaj legacy scalar DTO jako nowego globalnego ustawienia. Błędny template/regex/schema ma pozostać widocznym błędem.
