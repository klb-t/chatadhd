# Wątek 11 — domknięcie profilu syntezy archiwum

Osiem pozycji DIC-0149/0153/0154/0155/0157/0158/0165/0167 przeniesiono do danych. Domyślna synteza zachowuje wszystkie bajty siedmiu artefaktów, pełny gap JSON i odkryte terminy: **142 841 bajtów przed i po**, SHA256 `134d5f3d6ebc3514b268e08254bdc26120385986608a359f358e6c63b7bf3c40`. Końcowe testy wykonały **8/8 przypadków, 235/235 asercji**, bez błędów i pominiętych przypadków.

## Co zmieniono

| Inwentarz | Dane w `archive.pack#/defaults` | Konsument |
|---|---|---|
| DIC-0149 | `synthesis_rendering/display`: osobne presety item260, dedup120, superseded90 i pozostałe długości podglądów | `synth.cpp:234`, `268`, `289` |
| DIC-0153 | `identifiers`: wielka pierwsza litera, sufiksy liczby mnogiej akronimów, wyjątek nazwy projektu, minimalna podstawa odmiany i wielkość liter sufiksu; istniejące `synthesis/named_min_bytes` i `inflection_max_bytes` | `synth.cpp:343`, `347`, `370` |
| DIC-0154 | `interfaces`: lista prefiksów, minimalna długość i warunek wielkiej litery; preset `I`/3/true | wspólny matcher `synth.cpp:77`, użycie `402`, `949` |
| DIC-0155 | `component_evidence`: declaration3/use2/comment2 | `synth.cpp:406`, `414`, `418` |
| DIC-0157 | `feature_terms`: uporządkowane separatory i przedział liczby tokenów 1–5; istniejące feature70/section2 | `synth.cpp:554`, `560`, `566`, `571` |
| DIC-0158 | `source_references`: dziewięć ustawień leksera, przycinania, markerów, wykluczeń i dopasowania znanych URI po sufiksie; istniejące `synthesis/source_extensions` | `synth.cpp:629`, `635`, `656`, `662` |
| DIC-0165 | `display`: manifest240/graph100; `selection/manifest_items`:80 | `synth.cpp:1084`, `1103` |
| DIC-0167 | `files`, `csv`, `strings`: siedem nazw plików, kolejność/projekcja/cytowanie kolumn, precyzja, separator terminów, **105 fragmentów tekstu i szablonów raportów** | `synth.cpp:70`, `1004`, `1027`, `1159` |

Pełne ścieżki i klasyfikację zawiera `evidence/archive-synthesis-policy/migration-status-fragment.json`. DIC-0157 opisano wcześniej jako prefiks numeryczny; rzeczywista operacja liczy tokeny nazwy przed separatorem opisu.

Końcowa grupa `synthesis_rendering` ma 14 list i 146 skalarów/wpisów, licząc listę jako jeden liść. Pierwszy fragment defaultów i schematu przekazano właścicielowi profilu do pojedynczego scalenia, bez równoległego zapisu `archive.pack`. Ostatnie dziewięć ustawień DIC-0158 zapisano po jawnym przekazaniu wyłączności tego pliku; nie było równoległych zmian. SHA256 końcowego fragmentu: `c358671950b9eb738c5d8a75a4f64033c0a8a406c1cae0bc3b6f3c3cc9313c44`. Poprawiono wcześniej także 167 wskazówek `x-setting`, usuwając błędne podwójne separatory; dane i reguły walidacji pozostały bez zmian.

Limity prezentacji można zwiększać lub ustawić na zero w granicach reprezentacji natywnej. Pusty filename wyłącza konkretną projekcję. Powtarzająca się niepusta nazwa artefaktu, wybór brakującego pola CSV albo brak zmiennej szablonu powodują jawny błąd. Silnik zachowuje faktyczny status komponentu przy zmianie liczby widocznych dowodów. Template wstawia wartość raz: tekst zawierający `{{...}}` nie jest ponownie interpretowany. Po zmianie nazwy pliku odnośniki w raportach korzystają z aktywnego `files`.

## Dowód i odtwarzanie

`test_archive_synthesis_policy.cpp` zawiera osiem przypadków oraz wspólny deterministyczny probe, włączany przez `ARCHIVE_SYNTHESIS_POLICY_PROBE=1`. Fixture ma pięć dokumentów, cztery pliki kodu, 87 itemów, rozwidlenie, relację supersedes, TODO, elementy nazwane i sekcję cech. Testuje zwiększenie dawnych capów, niezależną deduplikację, niestandardowy prefiks, akronimy, zero dowodów komentarza, pełny tekst cechy, projekcję CSV, wyłączenie artefaktu oraz błędy kolizji i szablonu. Dwa końcowe przypadki sprawdzają dopuszczenie ścieżek absolutnych, `http` i `..`, pełnego literalnego URL, innych znaków leksera/przycinania/markerów oraz zmianę lub wyłączenie dopasowania znanego URI po sufiksie.

Porównanie obejmuje zamrożony `synth.cpp` bez tego domknięcia i aktualny `synth.cpp`, z tymi samymi bieżącymi helperami i domyślnymi danymi. Nie tworzy Runtime ani transportu. Wszystkie jednostki izolowanego buildu skompilowano z flagami projektu, w tym `-Werror`; trzy linki i trzy wykonania zakończyły się kodem0. Artefakty samego eksportu mają łącznie **130 009 bajtów**; wrapper wyniku wraz z gap JSON i discovered_terms ma 142 841 bajtów. Pierwsze dowody6/219 zachowano w `evidence/archive-synthesis-policy/`; końcowe pełne wyjścia przed/po, logi, polecenia i receipt8/235 są w `evidence/archive-source-references-policy/`. Źródło zamrożonej syntezy pozostaje w pierwszym folderze.

Replay wykorzystuje gotową bieżącą bibliotekę oraz jej `compile_commands.json`:

```sh
python docs/reports/data-in-code/evidence/archive-synthesis-policy/replay.py --build /path/current-build --output /tmp/archive-synthesis-replay
```

Na końcowym runnerze cały zestaw uruchamia się przez `--test-suite=archive.synthesis_policy`. Ten izolowany dowód dotyczy domknięcia syntezy; nie zastępuje pełnego porównania main→gałąź, testów prawdziwego Runtime ani pełnego `ctest`. Te sprawdzenia prowadzi właściciel gałęzi wątku11. Nie wykonywano wywołań sieciowych ani płatnych.

Powyższy `replay.py` faktycznie wykonano ponownie po regeneracji wszystkich 19 profili i korekcie metadata hints: powtórnie identyczne142841 bajtów oraz zielony zestaw6/219. Po końcowym domknięciu DIC-0158 wykonano go jeszcze raz ze świeżym embeddingiem: nadal identyczne142841 bajtów oraz **8/235**. Starsze wyniki są zachowane; końcowe dowody znajdują się w folderze source-references-policy.

## Czego nie zmieniono i dlaczego

DIC-0158 domknięto po rozpoznaniu, że dawne odrzucenia są heurystyką raportu, a nie wire ani kontrolą dostępu do plików. Zmienny profil steruje teraz wszystkimi wskazanymi warunkami. Nadal zachowano uniwersalne rozpoznanie alfanumerycznych bajtów i gramatykę sufiksu rozszerzenia. Wynik opisuje literalny odnośnik i rzeczywisty klucz dokumentu źródłowego; nie rozwiązuje ścieżki, nie otwiera pliku ani nie pobiera URL. Parser Markdown, CSV, stabilne nazwy pól JSON i typy relacji grafu pozostają uniwersalnymi operacjami lub kontraktem formatu. Nie zmieniono prawdziwego pochodzenia/statusu danych.

## Do wątku N

- **Do wątku 1:** template renderer jest inertny; można współdzielić mechanizm podglądu z promptami, ale ten profil nie zmienia analizy ani modelu.
- **Do wątku 2:** rozmiary projekcji są presetami, bez dodatkowych progów produktu. Szacunek kosztu i strażnik×10 wymagają podłączenia przy wykonywaniu operacji.
- **Do wątku 9:** włączyć końcowy zestaw8/235 do pełnego ctest, uruchomić pełną parity pipeline z raportu archiwum; CAPI/server mają korzystać z aktywnego profilu, zanim zastosują własne nazwy lub presety. DIC-0158 jest wdrożone jako heurystyka raportu, bez I/O.
- **Do wątku 10:** `ArchiveIntelligence::profile()` udostępnia schemat/values; edycja `profiles/archive.pack`, obiekty merge, listy replace, RFC6902 do usuwania wpisów. Grupa `synthesis_rendering` opisuje wszystkie powyższe ustawienia; zmiana pliku wymaga ponownego otwarcia Runtime. Szablony mają zmienne `project`, `pipeline_version`, `round` i `files`; source-reference używa osobno `title`, `location`, `date`.
