# Wymagania właściciela jako dane (`docs/spec`)

Pakiet SPEC1. Stan na 2026-10-23. Opis mówi, co kod robi DZIŚ.

## Po co to jest

Właściciel wielokrotnie prosił o „pełną specyfikację”. Prawie komplet jego słów już jest w trzech plikach
Markdown, ale to proza i nic jej nie czyta: briefy dla agentów pisze się ręcznie, więc każda poprawka
właściciela oznaczała restart. Teraz te trzy pliki są też DANYMI z numerami:

| Plik źródłowy | Co w nim jest | Co z niego powstaje |
|---|---|---|
| `docs/architecture/SLOWA_WLASCICIELA_2026-10-16.md` | 403 pozycje z kodami (PH, CL, GR, CM, GL, MR, AG, UI, DIP, ON, RE, PC, OT), cytaty dosłowne, „Sens”, dodatki bez numeru | pozycje z cytatami, sensem, streszczeniem wymagania |
| `docs/reports/LUKI_2026-10-16.md` | stan kodu wobec każdej pozycji: JEST / CZĘŚCIOWO / BRAK / NIEAKTUALNE / SPRZECZNE, dowód, następny krok | status z datą, dowody, następny krok |
| `docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md` | sekcje R1…R63, D1…D3 z cytatem dosłownym | pozycje z cytatem i opisem |

Pliki w tym katalogu:

- `ids.json` — rejestr numerów (alias → numer). Zmienia się tylko przez dopisanie.
- `understanding.json` — zatwierdzenia i poprawki rozumienia (powstaje przy pierwszym `propose`).
- `spec.json` — WYLICZONE dane (schemat `loom.spec/1`): nie edytuj ręcznie.
- `SPEC.md` — WYLICZONY widok dla człowieka: tabela liczników i lista pozycji po tematach. Nie edytuj ręcznie.
- `config.json` — opcjonalny; nadpisuje presety z `tools/spec.py` (rodzaje pozycji, etykiety statusów, ścieżki plików).

Narzędzie: `python3 tools/spec.py` (Python 3.11, bez sieci, **bez żadnego wywołania modelu**). Testy:
`python3 -m unittest discover -s tools/tests`.

## Numer pozycji

Numer kanoniczny jest **nieprzezroczysty**: `s_` i 12 znaków szesnastkowych (np. `s_f23586b9dffb`).
Nie niesie tematu, rodzaju ani daty — to są atrybuty pozycji. Przydziela się go RAZ i zapisuje w `ids.json`;
poprawienie literówki w cytacie (dyktowanie się myli) numeru nie zmienia. Dawne kody (`UI12`, `GR38`,
`R56`, `MM01–MM10` …) zostają jako **aliasy**, bo odwołują się do nich dokumenty; `show`, `list` i `cite`
rozumieją oba. Pozycje bez dawnego kodu (dodatki z dopiskiem „Uzupełnienie…”, sekcje rejestru bez numeru R)
mają alias z nagłówka, np. `slowa:uzupelnienie-3-2026-10-16-16-30-utc-…` — **zmiana nagłówka takiej sekcji da
nowy alias, a stary numer zostanie w `ids.json` zarezerwowany** (`check` to zgłosi jako ostrzeżenie).

## Jak zapisać poprawkę albo nową wypowiedź właściciela

Poprawka to NOWA albo zmieniona pozycja, nie restart:

```
printf '%s\n' '"dosłowne słowa właściciela"' > /tmp/cytat.txt
python3 tools/spec.py add --id R64 --date 2026-10-24 --quote-file /tmp/cytat.txt \
        --title "krótki tytuł" --sense "jedno zdanie, co to znaczy"
```

`add` dopisuje sekcję `## R64 — …` w `OWNER_REQUIREMENTS_2026-09-26.md` (przed pierwszą sekcją
nienumerowaną), buduje pozycję i odświeża `spec.json`, `ids.json`, `SPEC.md`. Plik z cytatem trafia tam
dokładnie tak, jak jest — narzędzie niczego nie dodaje ani nie poprawia. **Odmawia, gdy numer już istnieje**
(`R58` → „już istnieje”) i nigdy nie zmienia istniejącego cytatu. `--dry-run` pokazuje, co by wstawiło.
Kod `R`/`D` + liczba jest jedyną postacią, jaką przyjmuje `add`.

Ręczna edycja trzech plików `.md` też jest dozwolona; potem `python3 tools/spec.py build`. `check` zgłosi błąd,
dopóki `spec.json` nie nadąża za plikami.

## Rozumienie pozycji (reguła R61)

Kosztowna praca zaczyna się dopiero po zatwierdzeniu przez właściciela parafrazy koordynatora. Parser **nie
tworzy żadnych parafraz** — wszystkie pozycje startują ze stanem `none`.

```
python3 tools/spec.py propose  <numer> --text "jak ja to rozumiem"      # koordynator; stan: proposed
python3 tools/spec.py approve  <numer>                                   # TYLKO na słowo właściciela; stan: approved
python3 tools/spec.py correct  <numer> --text "jak to ma być naprawdę"   # TYLKO na słowo właściciela; stan: corrected
python3 tools/spec.py list --understanding proposed                      # co jeszcze czeka na niego
```

Nic nie zatwierdza się samo poza jednym, jawnie ustawionym wyjątkiem (IW-3, niżej: milczenie po `ask_after`, tylko przy
niskim koszcie odwrotu, z rekordem decyzji i komendą odwrotu); `approve` odmawia, gdy nie ma propozycji ani poprawki. Każda zmiana stanu zostaje
w `history` (kto, kiedy, jaki tekst) w `understanding.json`. Cytat dosłowny się przy tym nie zmienia.

## Jak brief cytuje numery

W briefie zamiast wklejać tekst pisze się `spec:<numer>` (kanoniczny albo dawny kod), np. `spec:s_f23586b9dffb`
albo `spec:UI12`. Agent czyta pozycję poleceniem `python3 tools/spec.py show <numer>`: dostaje cytat dosłowny z
plikiem i linią, sens, status z datą, dowody i następny krok. Koordynator może sprawdzić brief:

```
python3 tools/spec.py cite brief.md           # OK / BRAK dla każdego spec:<numer>; kod wyjścia 1, gdy numeru nie ma
python3 tools/spec.py cite brief.md --full    # wypisuje pozycje z cytatami dosłownymi do wklejenia agentowi
```

## Co oznaczają pola (skrót)

- `quotes[]` — cytaty dosłowne: `text`, `src` (plik:linia), `ref` (źródło, które cytat podaje u siebie), `attr`
  (oznaczenia z pliku, np. `dosłownie`, `przekazane`), `lead` (słowa tuż przed cytatem), `secondary` = cytat stoi
  przy napisie typu „relacja”, „zapis asystenta”, `principles.json` (heurystyka z listy w presetach; to NIE jest
  rozstrzygnięcie, czy to słowa właściciela — o tym mówi plik źródłowy).
- `sense` — wiersz „Sens:” (albo „Sedno:” w temacie kosztów); `summary` — jednozdaniowe wymaganie z sekcji
  „Wymagania wyprowadzone”; `restatement` — opis pod cytatem w rejestrze R/D (zapis asystenta).
- `status` — `{value, checked_on, observation: true, src}` skopiowane z LUKI. **To dana obserwacja z dnia
  `checked_on` (2026-10-16, z tytułu pliku), nie ograniczenie** (R60). Pozycje bez statusu (104) po prostu nie
  występują w LUKI; nie znaczy to, że czegoś brakuje w aplikacji.
- `kind` — wynika z przedrostka kodu i pliku: `R` → requirement, `D` → decision, `PH` → philosophy, pozostałe
  kody ze SLOWA → remark (uwaga właściciela), dodatki bez numeru → statement. Lista jest presetem
  (`kind_by_prefix` w `tools/spec.py` albo `config.json`), nie regułą; „rule” i „question” z briefu nie mają
  jeszcze żadnej pozycji, bo żaden przedrostek ich nie wyznacza i nic nie zostało zgadnięte.
- `theme` — temat ze SLOWA; dla pozycji tylko z LUKI wywnioskowany z przedrostka, jeśli przedrostek ma w SLOWA
  jeden temat (`theme_inferred: true`); inaczej `null` („bez tematu”).
- `refs`, `supersedes`, `superseded_by`, `supersession_notes` — odwołania do innych pozycji znalezione w tekście
  pozycji (kody i zakresy, np. `GL05–GL08`) oraz zapisy typu „zastąpione przez PC02”. Notatka niesie zdanie
  źródłowe, bo zastąpienie bywa częściowe — to czytelnik decyduje, czy całkowite.

## Co `check` sprawdza

Numery unikalne i zgodne z `ids.json`; aliasy nie powtarzają się; **każdy cytat (i sens, streszczenie, dowód,
następny krok) jest dosłownym fragmentem swojego pliku pod podanym `plik:linia`** (zmieniony cytat w `spec.json`
= błąd; przesunięty o linie = ostrzeżenie); `spec.json` jest identyczny z wynikiem `build` (inaczej: „uruchom
build”); każdy kod z LUKI ma pozycję w SLOWA/rejestrze albo jest wypisany jako sierota z linią; liczniki per
status, per temat, per plik; lista kodów tylko w jednym pliku; porównanie z liczbami z nagłówków LUKI (nagłówek
mówi dziś `BRAK 12`, pozycji jest 16 — nagłówek jest nieaktualny). `--strict` robi z sierot błąd, `--gaps`
wypisuje niezaklasyfikowane fragmenty.

## Znane luki (stan faktyczny)

- 117 niepustych wierszy w 43 fragmentach plików źródłowych nie jest pozycją: wstępy do tematów („Zasady
  czytania”, „Zakres”), listy „Pominięte / Odrzucone” (celowo NIE słowa właściciela), „Relacje między
  punktami”, sekcja „Krytyk kompletności” w LUKI (wypowiedzi, których zbiór jeszcze nie ma), nagłówki z
  „źródło: przebieg 2”. Lista: `python3 tools/spec.py check --gaps` oraz `gaps` w `spec.json`. Nic z tego nie
  zostało odrzucone po cichu — to proza, nie wypowiedzi z numerem.
- Cytaty wtórne (relacja agenta, zasada z bazy, zapis asystenta) parser zachowuje i oznacza `secondary`; nie
  odrzuca ich i nie rozstrzyga, czy to słowa właściciela — przenosi oznaczenia z plików.
- Zbiór sięga stanu 2026-10-16 (SLOWA, LUKI) plus dopiski z 2026-10-21 i rejestr R do R63 (2026-10-23). Czego nie
  ma w tych trzech plikach, tego nie ma w danych; nowe słowa właściciela dopisuje `add`. Przeglądu transkryptów
  po 16.10 ten pakiet nie robił.
- Status w `spec.json` nie jest odświeżany: to obserwacja z 2026-10-16. Nowy przegląd luk = nowy plik LUKI i
  zmiana ścieżki w `config.json` (`sources.luki`) albo nowa wersja pliku, potem `build`.


## Skrzynka decyzji na żywej stronie (R88 v1, 2026-10-23)

Strona drzewa (`python3 tools/spec.py html`, artefakt https://claude.ai/artifact/MfbrGaubViGGhsp2Z4ZEm7) ma przy każdej
pozycji przyciski: **Zatwierdzam parafrazę** (gdy parafraza jest w stanie `proposed`/`corrected`), **Popraw swoimi
słowami** (tekst), **Uwaga / dopisek** (tekst). Każde kliknięcie zapisuje wpis w bazie strony (kolekcja `spec_inbox`:
`node`, `alias`, `kind` = approve | correct | remark, `text`, `at`, `state` = new). Zadanie zaplanowane **„Sekretarz
spec”** (świeża sesja na Sonnecie, co godzinę o :23; preset) czyta wpisy `new`, stosuje je przez `spec.py approve /
correct / add` (tekst właściciela dosłownie), odświeża stronę, robi commit i push na gałąź dokumentów; wpis dostaje
`state` = applied (z komendą) albo escalated (z powodem) — nietypowe trafiają wiadomością do koordynatora. Czat zostaje
do dyskusji. Odwrót: wyłączyć zadanie i opublikować stronę bez `capabilities`.

### Kolejka „Czeka na Ciebie” na górze strony (R89 P2, 2026-10-23)

Na górze strony, nad „W edycji i ostatnio zmienione”, jest sekcja **Czeka na Ciebie (N)**: każda pozycja, której parafraza jest
w stanie `proposed`, a po połączeniu z bazą strony także każdy wpis `spec_inbox` w stanie `escalated` (wpis przekazany przez
sekretarza koordynatorowi; pozycja i wpis są w jednym wierszu). Wiersz ma alias i tytuł, parafrazę zwiniętą do dwóch wierszy
(„więcej”), **te same trzy przyciski co blok pozycji** (Zatwierdzam parafrazę / Popraw swoimi słowami / Uwaga; jeden kod
zapisu, jedno kliknięcie to jeden dokument `spec_inbox` o tym samym kształcie) i linię **„Bez Twojej odpowiedzi: …”**.
Tekst tej linii bierze się z pola `understanding.if_silent`, a gdy go nie ma, z presetu w generatorze (`QUEUE_PRESET`: „przyjmuję
wstępnie i jadę dalej; odwrót tani (R75)”; dla kosztu odwrotu `high` bez własnego tekstu: wariant bez słów „odwrót tani”).
Kolejność to preset w danych: najpierw pozycje z `understanding.cost_to_reverse` = `high` (brak pola = zwykły koszt), potem data
parafrazy malejąco, przy tej samej dacie wyższy numer pierwszy; notka o tym stoi w nagłówku sekcji. Pola ustawia
`spec.py propose|correct --if-silent TEKST --cost-to-reverse NAPIS` (wartości otwarte, dowolny napis; brak flagi zostawia
poprzednią wartość, pusty napis usuwa pole), a pierwszeństwo kosztu zmienia `spec.py html --queue-first high ...` (bez
wartości = bez pierwszeństwa). **Zmienione od ostatniej wizyty:** strona zapamiętuje czas wizyty w `localStorage` tej
przeglądarki (wygoda; bez pamięci przeglądarki działa tak samo, tylko nic nie oznacza), pozycje z dowolną datą późniejszą
dostają znacznik „nowe”, a liczba jest w nagłówku (data bez godziny liczy się jako koniec dnia, więc zmiana z dnia wizyty też
jest oznaczona). Wyszukiwarka przeszukuje także kolejkę; sekcja znika, gdy nic nie czeka. Testy: `tools/tests/test_spec_html.py`
(w tym strona narysowana w Chromium na 400 px, pomijana, gdy Playwrighta nie ma). Odwrót: usunąć sekcję z `spec_html.py`
(pola `if_silent` i `cost_to_reverse` są opcjonalne i nic poza stroną ich nie czyta).

**Pytanie w pełnym kształcie i skrzynka z klasą (IW-3, 2026-10-26; wszystkie progi to presety w
`tools/spec.py` → `DEFAULT_CONFIG["question"]`, nadpisywane przez `docs/spec/config.json`).**
`spec.py propose|correct` przyjmuje też `--ask-after OKRES` (od pierwszego pokazania; przykłady: `24h`, `2d`, `90m`, `PT6H`
albo chwila ISO), `--criteria TEKST` (powtarzalne; lista zastępuje poprzednią), `--class KLASA` (otwarta lista, przykłady:
understanding, money, private-data) i `--first-shown ISO` (preset: chwila `propose`; nowy tekst = nowe pokazanie;
`spec.py shown <id> [--at] [--force]` poprawia). Strona pokazuje pod pozycją kryteria, klasę, „pokazane N h temu” i kiedy milczenie
rozstrzyga (albo że po czasie pozycja trafi do koordynatora, gdy odwrót nie jest tani).
`spec.py silent [--now] [--run-id RID]` wykonuje `if_silent` pytań, którym minął `ask_after`, **tylko** gdy `cost_to_reverse`
jest na liście niskich (preset `low_cost_to_reverse`, flaga `--low`), do twardego limitu na bieg (preset `max_silent_per_run`,
`--limit`, `-1` = bez limitu); stan `approved`, `by: silence`, pole `decision` {text, if_silent, reversal, cost_to_reverse, by, at}
i to samo w `decisions[]` zapisu biegu. Reszta to `escalated` z powodem. Bez `ask_after` milczenie niczego nie rozstrzyga
(preset `default_ask_after: null`). Odwrót: `spec.py reopen <id> [--why]` (pozycja wraca do `proposed`, decyzja trafia do
historii jako `reverses`); jego późniejsze `approve` po milczeniu jest dozwolone i nadpisuje `by`.
Skrzynka: wpis strony ma teraz `class` (z `kind` przez preset `class_by_kind`; lista otwarta) i dostaje od sekretarza `verdict`
(+ `why`), widoczny przy pozycji. `spec.py inbox <plik.json>` (eksport wpisów `new`) stosuje bez modelu klasy z
`apply_classes` (approval → approve, correction → correct) oraz każdy wpis, którego tekst jest słowem zatwierdzenia
(`approve_words`, np. „Zatwierdzam”), a resztę eskaluje; wypisuje werdykty do zapisania z powrotem. Testy:
`tools/tests/test_spec_question.py`. Odwrót pakietu: nowe pola są opcjonalne; usunięcie podkomend nie zmienia danych.


**Liczniki W1, „Od Twojego wyjścia” (W9) i mapa miejsc v1 (IW-4, 2026-10-26; progi to presety w `tools/page_counters.py` → `PRESET`).**
Nad kolejką strona pokazuje wiersz liczników: suma `counter_delta` zdarzeń ze wszystkich zapisów biegów (`docs/reports/runs/`,
indeks `runs.jsonl`; liczniki i ich lista z danych, np. w toku / zrobione / nie wyszło / oryginalne), stany biegów, „dziś do Ciebie:
N słów” (paragony walut `words_currencies`; brak paragonów jest napisany wprost) i po połączeniu z bazą stan skrzynki. Pod nim
jeden wiersz „miejsca: N żyją · …” z `docs/ops/places.json` (statyczna tabela miejsc, sesji i usług; każdy stan z datą
obserwacji i źródłem), rozwijany do listy. Sekcja „Od Twojego wyjścia” rysuje się, gdy od ostatniej wizyty minęło więcej niż
`w9_after_hours` (preset 6 h): przyrost liczników od tamtej chwili, zdarzenia (pierwsze `w9_visible`, reszta „N zwiniętych —
pokaż”), nowe w kolejce, werdykty skrzynki. Ostatnia wizyta: dokument `spec_visits/owner` w bazie strony (zapisuje go tylko
właściciel — preset `visit_marker_who`: owner / anyone / none), a bez bazy pamięć przeglądarki jak dotąd. Żadnej liczby nie
liczy model: `python3 tools/page_counters.py` wypisuje ten sam JSON, który dostaje strona; `spec.py html` dołącza go sam
(`spec_html.py --no-counters` wyłącza). Testy: `tools/tests/test_page_counters.py` (zgodność z rekordami, W9 po symulowanej
przerwie w Chromium na 400 px). Odwrót: `--no-counters` albo usunięcie sekcji; `places.json` nic poza stroną nie czyta.

**Słowa sterujące i gesty (IW-5, 2026-10-26).** Jedna tabela `tools/routines/words.json` (rekord gestu `{gesture, patterns,
effect, cost, reversal, since, render}`; ziarno: 8 słów z cytatów — „stop” i „tutaj masz wolną rękę” R89, „rób to co wiesz” i
„mogę iść spać” R75, „podkręć” R62, „nie gadaj tyle” CLAUDE.md, „odpal” R90, „co pamiętasz” R73 — oraz gesty z §0 punkt 3;
lista otwarta, dopisuje się jednym poleceniem `tools/words.py add`). `tools/words.py apply "<tekst>"` rozpoznaje słowa (bez
polskich znaków, interpunkcji, wielkości liter) i stosuje skutek bez modelu, gdy koszt ≤ próg (preset); stan (mandaty, prośby
„stop” z listą biegów w toku i ich kosztem z `receipt[]`, znaczniki, preferencje, obecność) w `docs/spec/gestures_state.json`,
każde użycie w `docs/reports/runs/words_uses.jsonl`; krótkie słowo w długim zdaniu (np. „odruchowo stop nacisnąłem…”) jest
trafieniem słabym: zapisane, niezastosowane. Odwrót: `words.py reverse <use_id>`. Miary: `words.py stats`.

**start_gate (IW-6, 2026-10-26).** `tools/start_gate.py --role <rola>` rozwiązuje rola → model → miejsce z `tools/routines/roles.json`
(presety z `_history`; luz R63 jako parametr), liczy estymatę z `pace_plan.json`, sprawdza cap modelu (R91: Fable 100, reszta 90 —
preset), tempo (reguły `pace.py`), dysk i stan miejsca, ×10; karta JSON z werdyktem `start` / `start_with_advisories` / `wait` /
`owner`. Datowane obserwacje (np. „Fable niedostępny do 27.10 ~10:00 UTC”) dają advisory i zastępstwo z łańcucha roli, po swojej
dacie przestają działać same. `--log-only` nie blokuje; `--run-id` zapisuje `resolved` i estymatę w biegu; `single-package.js`
bierze model z karty (`args.start_gate`), a bez modelu i karty loguje advisory (dziedziczony model sesji).
