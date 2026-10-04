# Wątek 11 — tekst, wpisy i słownictwo archiwum: domknięcie polityki

Domknięto **13 pozycji inwentarza** w `archive/text.cpp`, `items.cpp` i `vocab.cpp`. Dwie dodatkowe grupy w kanonicznym `loom/data/runtime/archive.pack` zawierają **30 pól `text_item_closure` oraz 5 pól `vocab_closure`**. Domyślne wyniki czystego API są identyczne bajtowo ze świeżo zbudowaną bazą `161cc22`: **49 458 B przed i po**, SHA-256 `b255c284ded3bd942d68a5b80b8a0f91950ccb0aecd0406df585cfeb0b47ad2f`. Testy nowych wariantów: **9/9 przypadków, 94/94 asercje, 0 pominiętych**. Pełny zintegrowany `ctest` pozostaje osobną bramką właściciela gałęzi; poniższy dowód zakresu go nie zastępuje.

## Co przeniesiono

| ID | Dane / mechanizm | Preset zgodny z bazą |
| --- | --- | --- |
| DIC-0067 | Uporządkowane długości fraz, dozwolone separatory wejścia, separator wyjścia i odrzucanie powtórzonych sąsiadów | `[2,1]`, spacja/myślnik, spacja, powtórzenia odrzucane |
| DIC-0068 | Szerokość liczbowego znacznika listy | 4 cyfry |
| DIC-0071 | Zbiory końcowej/zamykającej/otwierającej interpunkcji i klasy Unicode otwierające zdanie | Dotychczasowe zbiory; uppercase/decimal |
| DIC-0073 | Nullable dolna i górna granica roku | 1990 / 2100 |
| DIC-0074 | Przyjmowanie epoki Unix i wcześniejszych znaczników czasu | `allow_nonpositive_epoch=false` |
| DIC-0077 | Pełny predykat identyfikatora CamelCase | Długość 4, uppercase 2, lowercase 1, wewnętrzna wielka litera |
| DIC-0084 | Kwantyzacja końcowego wyniku i precyzja uzasadnienia | Mnożnik 10000; precyzja 3 / 1 |
| DIC-0094 | Docelowe typy korekty wyniku requirement/question i sufiksy zamykające pytanie | requirement / open_question; dotychczasowe sufiksy |
| DIC-0097 | Końcowy encoder confidence po istniejącej kalibracji | Mnożnik 100 |
| DIC-0100 | Rodzaj źródła oraz pary type/cue dla markerów TODO/FIXME | code; requirement/todo albo bug/fixme |
| DIC-0101 | Rodzaj źródła oraz pary type/cue dla commitów | commit; implementation/commit albo bug/commit |
| DIC-0102 | Sufiksy wprowadzenia listy, obok istniejących ustawień bajtów/tokenów | `[":"]` |
| DIC-0104 | Role question/answer/superseding, nazwa/status relacji odpowiedzi i szablon uzasadnienia | Dotychczasowe role, resolves/resolved i identyczny tekst |

Grupy uzupełniają wcześniejszą migrację `items` i `vocabulary`; nie tworzą osobnych, konkurencyjnych słowników. Punkt DIC-0099 zachowuje poprzedni mieszany status: wyświetlany tekst ma już ustawienie `items.text_max_codepoints`, natomiast `i_`, separatory `#` i 10 znaków hasha należą do zachowanego kontraktu identyfikatora. Test porównuje ten sam ID po zmianie przypisanego typu/cue i aliasu źródła.

## API i nadpisywanie

Istniejące czyste funkcje przyjmują jawny `const ArchiveProfile*`; do `first_date`, `normalize_date`, `iso_from_epoch` i `TermRecord::to_json` dodano przeciążenia z profilem, zachowując dotychczasowe symbole bez argumentu profilu. `ArchiveProfile` jest niezmienny, a domyślny profil ma niezmienny cache lokalny do funkcji. Cache nie zamienia aktywnego profilu użytkownika w globalne ustawienie. Wywołujący może przekazać inną instancję w pojedynczym wywołaniu.

Daty i końcowa serializacja `TermRecord` są przekazywane z aktywnym profilem przez pipeline/import; numeryczne daty ChatGPT korzystają z nowego przeciążenia `walk_chatgpt`. Okablowanie tych wywołań wykonała część wątku 11 odpowiedzialna za import/pipeline. Domyślne symbole funkcji dat i serializacji pozostają dostępne.

Nakładka: `<data_dir>/profiles/archive.pack`, koperta `loom.runtime_profile_overlay/1`, `domain: archive`. `overrides` scala obiekty rekurencyjnie i zastępuje tablice; `patch` RFC 6902 pozwala usuwać elementy list lub wpisy map zgodnie ze schematem. Przykład przyjmowania historycznych dat:

```json
{
  "schema": "loom.runtime_profile_overlay/1",
  "domain": "archive",
  "overrides": {
    "text_item_closure": {
      "year_min": null,
      "year_max": null,
      "allow_nonpositive_epoch": true
    }
  }
}
```

`phrase_orders: []` wyłącza kandydatów fraz. Puste `list_intro_suffixes` wyłącza odrzucanie wprowadzenia listy. Mnożnik confidence/score równy 0 zachowuje wynik bez zaokrąglania. Role i type/cue są dowolnymi nazwami w danych, a nazwy klas Unicode są rejestrem dostępnych uniwersalnych predykatów: uppercase/lowercase/decimal/alpha/alnum/space/any. Szablon resolution ma zmienne `answer_type`, `question_type`, `answer_date`, `question_date`; brakująca zmienna jest jawnym błędem. Błędne nakładki pozostają błędami ładowania profilu.

Schemat opisuje typy, wymagane pola, dopuszczalne klasy i natywną reprezentację liczb. Nie dodano maksymalnej długości frazy ani zakresu lat jako produktu. ISO zachowuje dotychczasowy czterocyfrowy zapis roku oraz składnię miesiąca/dnia. `iso_from_epoch` odrzuca niefinitywne i niereprezentowalne wartości przed rzutowaniem; granica całkowitego `time_t` jest sprawdzana jako wyłączne `2^digits`, aby zaokrąglony floating-point nie przepuszczał przepełnienia. Niepowodzenie `gmtime_r`/`strftime` jest jawnym pustym wynikiem zgodnym z funkcją konwersji.

## Dowód przed / po i testy

Sonda [profile_archive_text_parity.cc](../../../loom/tests/compat/profile_archive_text_parity.cc) używa identycznych zastanych wywołań API po obu stronach. Nie konstruuje Runtime ani SemanticAnalyzer i nie wywołuje dostawcy. Wariant przed linkuje świeże archiwum z zamrożonego `161cc22`; wariant po linkuje aktualne obiekty text/items/vocab/profile/runtime_profile przed tym samym archiwum bazowym. To dowód czystych funkcji i ustawień, bez mieszania zewnętrznie konstruowanych klas o zmienionym układzie.

Zachowane pełne wyniki: [before-161.json](evidence/archive-text-policy/before-161.json), [after-161.json](evidence/archive-text-policy/after-161.json), [receipt.json](evidence/archive-text-policy/receipt.json). Oba pliki mają 49 458 B i ten sam powyższy hash. Receipt zawiera hash biblioteki bazowej, źródeł, profilu, wygenerowanych danych i obiektów linkowanych po zmianie. Końcowe porównanie wykonano również z ostatnim niezmiennym cache profilu.

Wejścia/wyjścia sondy: 77 tekstów z granicami fraz, Unicode, interpunkcją, listami i cue; 13 dat; 7 wartości epoki; 18 wydobytych wpisów; 12 konfiguracji relacji; 25 kodowanych wyników; 6 list salient terms i 6 wyników rozszerzenia słownika. Kształt, kolejność, confidence, ID, reason i status są porównywane bajt po bajcie.

[focused-tests.log](evidence/archive-text-policy/focused-tests.log) potwierdza 9/9 przypadków i 94/94 asercje: trigramy z innym separatorem i powtórzeniami, pusta lista długości, pięciocyfrowe listy, alternatywna granica zdania, nullable lata i pełne historyczne daty, epoka0/ujemna, NaN/inf/przepełnienie time_t, nowy typ pytania, encoder0/10, aliasy źródeł i type/cue, zachowany ID, wyłączenie colon-intro, nowe role relacji spoza domyślnych relation_types, alternatywny predykat CamelCase i precyzja uzasadnienia przy niezmienionym rankingu/wynikach/evidence.

Wszystkie pięć produkcyjnych obiektów użytych w sondzie oraz nowy test przeszły kompilację z `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`. Wcześniejsze pozytywne receipts (7/38 i 8/83) oraz pierwsze porównanie 49 193 B są zachowane; ich liczb nie dodajemy do ostatniego zestawu. Nie było negatywnego testu tego przebiegu, nie luzowano progów i nie usuwano zastanych testów. Płatne wywołania: 0.

Odtworzenie przy istniejącym świeżym buildzie bazy:

```sh
python3 docs/reports/data-in-code/evidence/archive-text-policy/reproduce.py \
  --baseline-source /workspace/scratch/72fc60ad1cc5/chatadhd-baseline \
  --baseline-build /workspace/scratch/72fc60ad1cc5/baseline-build \
  --output /tmp/archive-text-policy-proof
```

Harness buduje wyłącznie potrzebne obiekty, sondę i test zakresu; zapisuje pełne JSON/logi i receipt, także gdy porównanie wykazuje różnicę. Rewizję i zgodność bazowego builda należy zapewnić jak dla pozostałych sond wątku 11. Zbiorcza delta statusu jest w [migration-status-fragment.json](evidence/archive-text-policy/migration-status-fragment.json); fragment danych/schematu zachowano w [profile-fragment.json](evidence/archive-text-policy/profile-fragment.json).

## Pozostałe kontrakty i praca niewykonana

Pozostają uniwersalne operacje Unicode, tokenizacji, matematyki TF-IDF/cooccurrence, chronologicznego overlapu, składni Markdown/ISO i stabilnego ID. Kanoniczne klucze JSON i ustalone pozostałe rodzaje relacji należą do obecnego kontraktu zapisu. Rozszerzanie metadanych samego formatu wymaga wersjonowanego schematu, a nie bieżącego presetu reinterpretującego archiwalne dane.

Generator fraz respektuje niestandardowy separator wyjścia. Zastany vocabulary ranking nadal rozpoznaje frazę przez obecność spacji i zawiera operacje deduplikacji par słów; niniejsza migracja DIC-0067 dotyczy budowania kandydatów, nie zmienia tego algorytmu rankingu. Preset spacji zachowuje dotychczasowy rezultat. Ewentualny wspólny typ strukturalny frazy jest odrębną zmianą API, którą trzeba porównać z istniejącymi rankingami.

W tej części nie wdrożono UI zapisu profili, C API ani strażnika zużycia. Pełne snapshoty/provenance/caching pipeline opisuje `implementation-archive.md`. Końcowa integracja i pełne testy są prowadzone przez właściciela gałęzi.

## Do wątku N

- **Do wątku 2:** przewidywać zużycie przed większymi długościami/liczbą fraz, większą precyzją i renderowaniem dużych wyników; profilu nie zastępować stałym limitem. Guard ×10 pozostaje wspólnym okablowaniem.
- **Do wątku 10:** podgląd `ArchiveIntelligence::profile()` udostępnia `values` i `value_schema` obu grup; role/type/cue to dane, a nie enum dziedzin w UI. Obsłużyć nullable lata, encoder0, puste listy i inertne zmienne reason. Zapis aktywnego profilu/nakładki i ponowne ładowanie pozostają pracą interfejsu.
- **Do wątku 9:** zebrać tę deltę 13 ID i zachować mieszany status DIC-0099 ze stable-ID wire. Pełny świeży build/ctest oraz porównanie całych artefaktów archiwum mają objąć również finalne przekazanie profilu do dat numerycznych i serialization. Wyniku zakresu 9/94 nie przedstawiać jako pełnego ctest.
