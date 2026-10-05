# Wątek 11: pamięć i selektor — migracja danych

Data: 2026-10-04. Baza źródeł: `161cc22`. Inwentarz zapisano przed edycją kodu w `bab7be3`. Ten raport dotyczy wyłącznie `loom/src/memory` i `loom/src/search` oraz ich nagłówków, danych i testów. Nie stanowi dowodu przejścia zintegrowanego `ctest`.

## Co przeniesiono

`loom/data/runtime/selector.pack` zawiera 14 parametrów w czterech grupach. Domyślne wartości odtwarzają wcześniejsze zachowanie:

| Grupa | Wartości domyślne |
|---|---|
| `tfidf` | `min_token_length=2`, `lowercase=true`, `max_features=5000`, `idf_smoothing=1`, `idf_offset=1`, `normalize=true`, `sublinear_tf=false` |
| `keyword` | `min_token_length=1`, `lowercase=true` |
| `ranking` | `minimum_score=0`, `top_k=5` |
| `tiers` | `with_embedding=1`, `without_embedding=2`, `embedding_failure=2` |

`max_features=0` oznacza brak limitu liczby cech. `5000` jest presetem, a nie granicą polityki. Identyfikatory 1/2/3 oznaczają implementowane możliwości embedding/TF-IDF/keyword. Wybór embeddingu bez dostawcy zwraca jawne `Unavailable`; nie wykonuje innej metody pod etykietą embeddingu. Domyślny fallback do TF-IDF pozostaje taki sam.

`loom/data/runtime/memory.pack` zawiera 21 pól najwyższego poziomu:

| Przeznaczenie | Wartości domyślne |
|---|---|
| Tworzenie | typ `text`, waga `1`, identyfikator 12 znaków, auto-tagi dla `["text"]`, próg tagowania `1` |
| Wielkość wyników | kontekst 16000 znaków, wyszukiwanie 10 wyników, etykieta grafu 25 znaków |
| Graf | typ `memory`, relacja `child`, waga relacji `1` |
| Format | wcięcie dwa znaki spacji, separator `\n`, prefiks i separator tagów ` #`, precyzja wagi `1`, szablon ` [w:{{weight}}]` |
| Dyspozycja po danych | mapa szablonów `folder/file/dir`, szablon domyślny, mapa priorytetów `folder:0`, priorytet domyślny `1` |

Renderowanie nie zawiera już warunków po nazwach `folder/file/dir`. Nowy typ węzła otrzymuje szablon i kolejność przez mapy danych. Zmienne szablonu obejmują pola zapisanego węzła oraz `indent`, sformatowane `weight`, `tags` i `path`; dostęp do metadata działa przez `{{/metadata/topic}}`. Renderer jest inertny: nie wykonuje treści węzła ani ponownego rozwijania podstawionego tekstu.

Wartości liczbowe ograniczają jedynie wymagania operacji i reprezentacji: długość tokenu/identyfikatora co najmniej 1, pozostałe liczności nieujemne, pola używane jako `int` do `INT_MAX`, pola `size_t` do zakresu tej reprezentacji. Wagi i progi wyniku nie dostały sztucznego zakresu 0–1. Słownictwo renderowanych typów jest otwarte.

## API i nakładki

`SelectorEngine(tier, embedder, optional_profile)` dodaje trzeci, opcjonalny argument. Nieprzekazanie profilu zachowuje wcześniejszy preset. `RuntimeProfile::load("selector", data_root)` plus wstrzyknięcie profilu to jawny punkt podłączenia nakładki. `set_profile(profile)` sprawdza wartości według deskryptora obsługiwanego przez konsumenta, przygotowuje kopię indeksu i podmienia stan dopiero po udanym przebudowaniu. Odrzucony profil, błąd embeddingu albo zła liczba wektorów nie niszczą działającego indeksu.

`search_checked(query, optional_top_k)` zwraca `Result<vector<SelectorHit>>`. `profile_inspection()` zwraca pełne wartości, schemat i hash profilu. Stare `search()` nadal zwraca pusty wynik na błąd zapytania u dostawcy embeddingu, zgodnie z dotychczasową semantyką; wariant checked udostępnia oryginalny błąd. Nowy błąd ustawień/schematu w starym wrapperze jest wyjątkiem z oryginalnym opisem, zamiast pozorowanego pustego wyniku.

`MemoryEngine(path, analyzer)` ładuje preset, a potem nakładkę `<path.parent_path()>/profiles/memory.pack`. Nieprawidłowa istniejąca nakładka nie jest zastępowana presetem. `profile_status()` i `profile_inspection()` udostępniają problem. `reload()` ponownie sprawdza profil, więc poprawienie pliku naprawia stan. Surowe `get_all()` i `get_node()` nadal udostępniają zapisane źródła także przy błędzie renderowania.

Dodano `get_children_checked`, `get_active_context_checked`, `search_checked`, `get_graph_data_checked`. Stare wrappery przy błędzie profilu/szablonu rzucają wyjątek z oryginalnym komunikatem; poprawny preset nigdy nie powoduje tej nowej ścieżki. `add_node()` przy pominięciu typu używa typu z profilu. Opcjonalne argumenty `max_chars`, `limit`, `top_k` pozwalają odróżnić preset od wartości wyraźnie przekazanej przez klienta. Istniejące zwykłe wywołania C++ zachowują zgodność źródłową; konsumentów C++ należy przebudować razem ze zmienionymi nagłówkami.

Przykładowa nakładka użytkownika:

```json
{
  "schema": "loom.runtime_profile_overlay/1",
  "domain": "memory",
  "overrides": {
    "renderers": {"note": "{{indent}}NOTE {{content}} [{{/metadata/topic}}]"},
    "sort_priorities": {"note": -1},
    "context_max_chars": 24000
  }
}
```

Zasady scalenia, walidacja i hash są wspólnym mechanizmem `RuntimeProfile`, bez oddzielnych globalnych ustawień domeny w modułach pamięci/selekcji.

## Wyniki przed i po

Zapisano pełne syntetyczne wyjścia: [before.json](evidence/memory-selector/before.json), [after.json](evidence/memory-selector/after.json). Źródło zewnętrznej sondy: `loom/tests/compat/profile_memory_selector_parity.cc`; używa wyłącznie API istniejącego przed migracją i atrapy offline.

| Pomiar | Przed | Po |
|---|---:|---:|
| Wiersze selektora: trzy metody, osiem zapytań, cztery liczności i błędy dostawcy | 99 | 99 |
| Dodatkowa sonda presetu ograniczenia słownika >5000 cech | 1 | 1 |
| Jawne warianty długości kontekstu pamięci | 4 | 4 |
| Wiersze wyszukiwania pamięci | 12 | 12 |
| Rozmiar pełnego wyjścia | 59955 B | 59955 B |
| SHA-256 | `caec4bb87a68e1f284978793feceffa8a5b42445db38776c691561e425150360` | ten sam |

Porównanie jest bajtowo identyczne, obejmuje też domyślny kontekst, korzenie i graf pamięci. Fixture zawiera osiem węzłów, różne typy, Unicode, nieaktywny podgraf, osierocony węzeł, tagi, ujemną wagę i tekst przypominający szablon.

To **wtórny dowód zgodności zakresu**, nie wynik zintegrowanego buildu `161cc22`. Przed migracją wykorzystano istniejące archiwum rdzenia `9d15d2d`, nagłówki zamrożonego `161cc22` i tę samą sondę. `git diff 9d15d2d 161cc22 -- loom/src/memory loom/src/search loom/src/semantic loom/src/util loom/src/core/config.cpp loom/src/db loom/include/loom/memory_engine.h loom/include/loom/selector.h` był pusty. Po migracji przed tym archiwum podlinkowano nowe obiekty pamięci, selektora i profilu. Sonda nie konstruuje zmienionych, starych obiektów Runtime ani innych konsumentów o niezgodnym układzie ABI. Świeży pełny baseline `161cc22` i pełny build po migracji pozostają zadaniem bramki integracyjnej.

## Testy zmienionych ustawień

[focused-tests.log](evidence/memory-selector/focused-tests.log): **22/22 przypadki, 139/139 asercji, 0 pominiętych**. W tym 14 dotychczasowych przypadków i osiem nowych:

- przebudowanie słownika przy `max_features=1`, zmiana progu wyniku;
- liczba cech ponad dawny preset 5000, z `max_features=0`;
- odrzucenie obcej domeny bez utraty indeksu;
- błąd embeddingu podczas zmiany profilu bez utraty indeksu;
- brak dostawcy dla ustawionego tier 1 oraz zachowanie indeksu przy błędzie dostawcy i fallbacku 1;
- nowy typ pamięci `note` z rendererem, sortowaniem, metadata, limitem wyszukiwania i etykietą grafu;
- jawne odrzucenie błędnej nakładki i skuteczne `reload()` po naprawie;
- błąd brakującej zmiennej szablonu z zachowaniem źródła.

Zmodyfikowane moduły i testy przeszły kontrolę składni z zestawem ostrzeżeń projektu oraz `-Werror`. Nie uruchomiono kolejnego pełnego buildu podczas równoległej kompilacji rdzenia.

## Odtworzenie wtórnej sondy

Poniższe ścieżki odnoszą się do środowiska, w którym wykonano dowód. `OLD_BUILD` musi wskazywać archiwum `9d15d2d`, a `BASELINE` zamrożone źródła/nagłówki `161cc22`. Dla finalnej bramki należy zamiast starego archiwum użyć świeżo zbudowanego `161cc22`.

```sh
cd /workspace/scratch/72fc60ad1cc5/chatadhd
TASK_TMP=/workspace/scratch/72fc60ad1cc5
OLD_BUILD=/workspace/scratch/98e6ad903811/baseline/loom/build/dev
BASELINE=/workspace/scratch/72fc60ad1cc5/chatadhd-baseline
git diff --exit-code 9d15d2d 161cc22 -- loom/src/memory loom/src/search loom/src/semantic loom/src/util loom/src/core/config.cpp loom/src/db loom/include/loom/memory_engine.h loom/include/loom/selector.h
c++ -std=c++20 -I"$BASELINE/loom/include" -Iloom/third_party/nlohmann \
  loom/tests/compat/profile_memory_selector_parity.cc \
  "$OLD_BUILD/libloom_core.a" "$OLD_BUILD/libloom_sqlite3_amalgamation.a" "$OLD_BUILD/libloom_miniz.a" \
  -pthread -ldl -lm -lssl -lcrypto -o "$TASK_TMP/memory-selector-parity-before"
c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann -Iloom/src \
  -c loom/src/memory/memory_engine.cpp -o "$TASK_TMP/memory-profile.o"
c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann -Iloom/src \
  -c loom/src/search/selector.cpp -o "$TASK_TMP/selector-profile.o"
c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann -Iloom/src \
  -c loom/src/model/runtime_profile.cpp -o "$TASK_TMP/runtime-profile-memory-selector.o"
c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann \
  loom/tests/compat/profile_memory_selector_parity.cc \
  "$TASK_TMP/memory-profile.o" "$TASK_TMP/selector-profile.o" "$TASK_TMP/runtime-profile-memory-selector.o" \
  "$OLD_BUILD/libloom_core.a" "$OLD_BUILD/libloom_sqlite3_amalgamation.a" "$OLD_BUILD/libloom_miniz.a" \
  -pthread -ldl -lm -lssl -lcrypto -o "$TASK_TMP/memory-selector-parity-after"
"$TASK_TMP/memory-selector-parity-before" > "$TASK_TMP/memory-selector-before.json"
"$TASK_TMP/memory-selector-parity-after" > "$TASK_TMP/memory-selector-after.json"
cmp "$TASK_TMP/memory-selector-before.json" "$TASK_TMP/memory-selector-after.json"
sha256sum "$TASK_TMP/memory-selector-before.json" "$TASK_TMP/memory-selector-after.json"
c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann -Iloom/third_party/doctest -Iloom/tests \
  loom/tests/main.cpp loom/tests/test_selector.cpp loom/tests/test_memory.cpp \
  "$TASK_TMP/memory-profile.o" "$TASK_TMP/selector-profile.o" "$TASK_TMP/runtime-profile-memory-selector.o" \
  "$OLD_BUILD/libloom_core.a" "$OLD_BUILD/libloom_sqlite3_amalgamation.a" "$OLD_BUILD/libloom_miniz.a" \
  -pthread -ldl -lm -lssl -lcrypto -o "$TASK_TMP/memory-selector-focused-tests"
"$TASK_TMP/memory-selector-focused-tests" > "$TASK_TMP/memory-selector-focused-tests.log" 2>&1
```

Po zmianach w innych domenach, zwłaszcza układzie `SemanticAnalyzer`, testy konstruujące te klasy wymagają świeżych zgodnych obiektów albo pełnego buildu. Zachowany log dotyczy stanu nagłówków/obiektów podczas wykonania opisanej sondy.

## Pozostałe literały i praca niewykonana

- `MemoryNode::from_json` oraz inicjalizatory struktury nadal definiują domyślny odczyt starszego formatu (`text`, aktywny, głębokość 0, waga 1); zmiana ich przez bieżący preset reinterpretowałaby zapisane stare dane. Oddzielny schemat migracji zapisu jest właściwym miejscem dalszego uogólnienia.
- Kanoniczne nazwy pól JSON, logów i błędów pozostały częścią kontraktu. `json::DumpOptions.indent=2` zachowuje dotychczasowy format pliku; nie jest parametrem wyników selekcji.
- Pamięć nadal wyszukuje przez case-insensitive substring i zachowuje kolejność wstawiania; selektor używa uniwersalnych operacji Unicode, TF-IDF i cosinusa, stabilnego rankingu oraz remisu w kolejności korpusu. Rozszerzanie rejestru metod należy do wątku 3, bez tworzenia konkurencyjnego selektora.
- Nie zmieniono pełnego okablowania Runtime, publicznego C API, ContextEngine ani guardów zużycia. Nakładkę selektora można dziś jawnie załadować i wstrzyknąć; runtime nie został automatycznie zmieniony w tym zakresie.
- Nie deklarujemy zielonego pełnego `ctest` ani buildu web na podstawie tych testów zakresu.

## Do wątku 2

Podłączyć oszacowanie przed przebudową indeksu, embeddingiem i renderowaniem dużego kontekstu do polityki zużycia ×10. Liczba cech, wyników, tokenów/znaków i precyzja formatu są danymi/presetami; do potwierdzenia wzrostu używać rzeczywistego szacunku, a nie wprowadzać stałych limitów w tych modułach.

## Do wątku 3

Przy składaniu jednego selektora korzystać z `SelectorEngine::search_checked`, `set_profile` i `profile_inspection`. `max_features=0` jest nieograniczony; przebudowanie jest transakcyjne. Wariant checked ujawnia błąd dostawcy, legacy wrapper zachowuje historyczny pusty wynik. Nakładkę `selector` ładować przez `RuntimeProfile::load` i wstrzykiwać; kontekst pamięci pobierać przez `get_active_context_checked`, aby nie ukrywać błędów profilu. Nie duplikować rendererów po nazwach typów.

## Do wątku 10

Do trybu eksperckiego używać `profile_inspection()` i `value_schema` jako formularza. Edytować mapy `renderers`/`sort_priorities` i pozostałe wartości przez nakładkę domeny. Pokazywać hash, wartości efektywne i jawny błąd walidacji/renderowania; surowe źródło zachować dostępne. Po zapisie pamięć wymaga `reload()`, a selektor `set_profile()` i sprawdzenia jego wyniku. Nie przedstawiać suwaka 5000 cech jako granicy silnika.

## Do wątku 9

Przebudować wszystkich konsumentów zmienionych nagłówków, podłączyć profile do kompozycji Runtime/C API i wykonać pełny `ctest` oraz build web. Zastąpić wtórny dowód świeżym porównaniem całego `161cc22`, zachowując obecne pełne wyjścia i sondę. Nie mylić 22 testów zakresu z bramką całego repo. Uwagę z audytu CLI o ignorowanych nakładkach `help`/`flags` i aliasach `version/help` przekazano integratorowi; integrator wdrożył bootstrap profilu przed parserem i przed obsługą tych poleceń.
