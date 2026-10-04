# Wątek 11 — domknięcie profilu syntezy archiwum

Siedem pozycji DIC-0149/0153/0154/0155/0157/0165/0167 przeniesiono do danych. Domyślna synteza zachowuje wszystkie bajty siedmiu artefaktów, pełny gap JSON i odkryte terminy: **142 841 bajtów przed i po**, SHA256 `134d5f3d6ebc3514b268e08254bdc26120385986608a359f358e6c63b7bf3c40`. Nowe testy wykonały **6/6 przypadków, 219/219 asercji**, bez pominiętych przypadków.

## Co zmieniono

| Inwentarz | Dane w `archive.pack#/defaults` | Konsument |
|---|---|---|
| DIC-0149 | `synthesis_rendering/display`: osobne presety item260, dedup120, superseded90 i pozostałe długości podglądów | `synth.cpp:234`, `268`, `289` |
| DIC-0153 | `identifiers`: wielka pierwsza litera, sufiksy liczby mnogiej akronimów, wyjątek nazwy projektu, minimalna podstawa odmiany i wielkość liter sufiksu; istniejące `synthesis/named_min_bytes` i `inflection_max_bytes` | `synth.cpp:343`, `347`, `370` |
| DIC-0154 | `interfaces`: lista prefiksów, minimalna długość i warunek wielkiej litery; preset `I`/3/true | wspólny matcher `synth.cpp:77`, użycie `402`, `949` |
| DIC-0155 | `component_evidence`: declaration3/use2/comment2 | `synth.cpp:406`, `414`, `418` |
| DIC-0157 | `feature_terms`: uporządkowane separatory i przedział liczby tokenów 1–5; istniejące feature70/section2 | `synth.cpp:554`, `560`, `566`, `571` |
| DIC-0165 | `display`: manifest240/graph100; `selection/manifest_items`:80 | `synth.cpp:1069`, `1088` |
| DIC-0167 | `files`, `csv`, `strings`: siedem nazw plików, kolejność/projekcja/cytowanie kolumn, precyzja, separator terminów, **105 fragmentów tekstu i szablonów raportów** | `synth.cpp:70`, `989`, `1011`, `1143` |

Pełne ścieżki i klasyfikację zawiera `evidence/archive-synthesis-policy/migration-status-fragment.json`. DIC-0157 opisano wcześniej jako prefiks numeryczny; rzeczywista operacja liczy tokeny nazwy przed separatorem opisu.

Nowa grupa `synthesis_rendering` ma 10 list i 141 skalarów/wpisów, licząc listę jako jeden liść. Fragment defaultów i schematu przekazano właścicielowi profilu do pojedynczego scalenia, bez równoległego zapisu `archive.pack`. Końcowy SHA256 fragmentu: `91e7d39f8cd3b1da645158b864b46f7c9b7f676c922bd29847e65aa565b04e59`. Poprawiono także 167 wskazówek `x-setting`, usuwając błędne podwójne separatory; dane i reguły walidacji pozostały bez zmian.

Limity prezentacji można zwiększać lub ustawić na zero w granicach reprezentacji natywnej. Pusty filename wyłącza konkretną projekcję. Powtarzająca się niepusta nazwa artefaktu, wybór brakującego pola CSV albo brak zmiennej szablonu powodują jawny błąd. Silnik zachowuje faktyczny status komponentu przy zmianie liczby widocznych dowodów. Template wstawia wartość raz: tekst zawierający `{{...}}` nie jest ponownie interpretowany. Po zmianie nazwy pliku odnośniki w raportach korzystają z aktywnego `files`.

## Dowód i odtwarzanie

`test_archive_synthesis_policy.cpp` zawiera sześć przypadków oraz wspólny deterministyczny probe, włączany przez `ARCHIVE_SYNTHESIS_POLICY_PROBE=1`. Fixture ma pięć dokumentów, cztery pliki kodu, 87 itemów, rozwidlenie, relację supersedes, TODO, elementy nazwane i sekcję cech. Testuje zwiększenie dawnych capów, niezależną deduplikację, niestandardowy prefiks, akronimy, zero dowodów komentarza, pełny tekst cechy, projekcję CSV, wyłączenie artefaktu oraz błędy kolizji i szablonu.

Porównanie obejmuje zamrożony `synth.cpp` bez tego domknięcia i aktualny `synth.cpp`, z tymi samymi bieżącymi helperami i domyślnymi danymi. Nie tworzy Runtime ani transportu. Wszystkie sześć jednostek izolowanego buildu skompilowano z flagami projektu, w tym `-Werror`; trzy linki i trzy wykonania zakończyły się kodem0. Artefakty samego eksportu mają łącznie **130 009 bajtów**; wrapper wyniku wraz z gap JSON i discovered_terms ma 142 841 bajtów. Pełne wyjścia przed/po, źródło zamrożonej syntezy, logi i hashe są w `evidence/archive-synthesis-policy/`.

Replay wykorzystuje gotową bieżącą bibliotekę oraz jej `compile_commands.json`:

```sh
python docs/reports/data-in-code/evidence/archive-synthesis-policy/replay.py --build /path/current-build --output /tmp/archive-synthesis-replay
```

Na końcowym runnerze cały zestaw uruchamia się przez `--test-suite=archive.synthesis_policy`. Ten izolowany dowód dotyczy domknięcia syntezy; nie zastępuje pełnego porównania main→gałąź, testów prawdziwego Runtime ani pełnego `ctest`. Te sprawdzenia prowadzi właściciel gałęzi wątku11. Nie wykonywano wywołań sieciowych ani płatnych.

## Czego nie zmieniono i dlaczego

DIC-0158 pozostaje częściowe: lista rozszerzeń jest już w `synthesis/source_extensions`, lecz scanner lokalnego odnośnika oraz odrzucenie `//`, `..`, ścieżek absolutnych i prefiksu http pozostają istniejącym kontraktem lokalizatora (`synth.cpp:634`, `647`). Raport nie przedstawia tych warunków jako konfigurowalnych. Parser Markdown, CSV, stabilne nazwy pól JSON i typy relacji grafu pozostają uniwersalnymi operacjami lub kontraktem formatu. Nie zmieniono prawdziwego pochodzenia/statusu danych.

## Do wątku N

- **Do wątku 1:** template renderer jest inertny; można współdzielić mechanizm podglądu z promptami, ale ten profil nie zmienia analizy ani modelu.
- **Do wątku 2:** rozmiary projekcji są presetami, bez dodatkowych progów produktu. Szacunek kosztu i strażnik×10 wymagają podłączenia przy wykonywaniu operacji.
- **Do wątku 9:** włączyć końcowy zestaw6/219 do pełnego ctest, uruchomić pełną parity pipeline z raportu archiwum; CAPI/server mają korzystać z aktywnego profilu, zanim zastosują własne nazwy lub presety. DIC-0158 ma jawnie zachowany kontrakt lokalizatora.
- **Do wątku 10:** `ArchiveIntelligence::profile()` udostępnia schemat/values; edycja `profiles/archive.pack`, obiekty merge, listy replace, RFC6902 do usuwania wpisów. Grupa `synthesis_rendering` opisuje wszystkie powyższe ustawienia; zmiana pliku wymaga ponownego otwarcia Runtime. Szablony mają zmienne `project`, `pipeline_version`, `round` i `files`; source-reference używa osobno `title`, `location`, `date`.
