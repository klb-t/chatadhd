# Indeks wątków — 2026-10-04

Właściciel indeksu: wątek 9, `gpt/integrator-2026-10-04`.
Baza tej kontroli: `main` `161cc22`; zachowane wszystkie zmiany INTERFEJS/PR9.
Status dotyczy wskazanego commitu, nie przyszłych zmian na gałęzi.
Kolejność odbioru: **2 → 3/4/5 → pozostałe**. Nieukończona gałąź nie jest
przyjmowana jako gotowy wątek. Każdy przyjęty przyrost wymaga świeżego fetch,
rebase, pełnego CTest z kontrolą wykonanych przypadków, build web i fast-forward.

| Wątek | Gałąź / sprawdzony commit | Raport lub dowód | Status i otwarte przekazania |
|---|---|---|---|
| 1 | `gpt/knowledge-precision-2026-10-04` / `a042ab7` | [raport W1](https://github.com/klb-t/chatadhd/blob/a042ab7b45d9cc908154cf0252fbae4bca451d3c/docs/reports/knowledge-precision-2026-10-04.md) | Pierwszy przyrost precyzji gotowy do bramek; w kolejce po 3/4/5. Ponowne zadanie: prompty jako dane i węzły metod, nakładka, podgląd/zmiana wywołania, tryby walidacji; uzgodnić z 11 przeniesienie `semantic_llm` i z7 presety. |
| 2 | `gpt/usage-policy-2026-10-04` / `34cc920` | [raport W2](https://github.com/klb-t/chatadhd/blob/34cc920dd3cdb0c0fca0a514569b19111429583f/docs/reports/usage-policy-2026-10-04.md) | **Wstrzymany po nowym poleceniu właściciela:** preset policy nadal literalnie w C++. Native/web build i19/19 świeżych kontraktów przechodzą; pełny CTest trwa. Do2: autorytatywny preset do danych/profilu z rzeczywistym loaderem i nakładką. Do3/4/5: jawnie adoptować lifecycle; do10: JSON API tylko static-kernel. Nie daje exactly-once wykonania. |
| 3 | `gpt/chat-selector-2026-10-04` / `36ec2a8` | [raport W3](https://github.com/klb-t/chatadhd/blob/36ec2a8f6340ba3d7873d3c4864e3af23178bcd6/docs/reports/chat-selector-2026-10-04.md) | W toku, adapter embed nie kończy selektora. Do4: wspólny format metod/wersji/przebiegów i krawędzi pochodzenia **przed integracją**. Do10: wystawić capability/ustawienia/rejestr i tryby odpowiedzi grafowej. |
| 4 | `gpt/native-graph-packet-2026-10-04` / `3caa6b4` | [raport W4](https://github.com/klb-t/chatadhd/blob/3caa6b4dcfb6412294bf816c1105bcf99afacdbc/docs/reports/native-graph-packet-2026-10-04.md), [reprodukcja](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-packet-unresolved-replay/docs/reports/integrator-packet-replay-2026-10-04.md) | Wstrzymany: powtórzenie unresolved wykonuje operację drugi raz przy jednym rozliczeniu. Do4: poprawka/regresja; do3: format metody/wersji/przebiegu w GraphPacket. Oryginalny kod i wynik negatywny zachowane w archive. |
| 5 | `gpt/archive-import-2026-10-04` / `f2af0a8` | [dowód audytu](https://github.com/klb-t/chatadhd/blob/c7972252a23cb13c54797f6b57df24de903c5296/docs/reports/archive-import-2026-10-04-evidence/audit/README.md), [reprodukcja checkpointu](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-import-checkpoint-replay/docs/reports/integrator-import-checkpoint-2026-10-04.md) | Wstrzymany: c797225 po błędzie znanego pliku pomocniczego pomija interpretację przy wznowieniu i oznacza źródło complete. Poprawny projekt z dokumentem kończy z 0/0 węzłów po usunięciu chwilowego błędu DB. f2af0a8 poprawia replay terminalnego usage receipt, bez zmiany wadliwego checkpointu interpretacji. Do5: poprawka/regresje, końcowy raport, receipt i instrukcja właściciela. |
| 6 | `gpt/catalog-selection-2026-10-04` / `1fb25ae` | [protokół W6](https://github.com/klb-t/chatadhd/blob/1fb25ae7a826fbc9897897cc69ea6ccb94f052a6/docs/reports/catalog-selection-2026-10-04-protocol.md) | W toku: helper semantyczny bez wywołania przez selekcję; brak 14 regresji i wyników. Pierwsze spojrzenie na ślepy korpus należy do6 na samym końcu; integrator nie czyta korpusu ani klucza. |
| 7 | `gpt/model-research-2026-10-04` / `3855178` | [reprodukcja audytu](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-readiness-orphan-response/docs/reports/integrator-readiness-orphan-2026-10-04.md) | W toku/wstrzymany: odpowiedź bez wpisu w ledgerze pomijana, gdy ledger istnieje; brak raportu końcowego. Frontier offline 17/17; nie jest wynikiem jakości modeli. Nowy EUR5 gate to przygotowanie offline, nie wykonanie badań. Do7: naprawić audyt; do1: zwycięskie przepisy z dowodami; format twierdzeń o metodach uzgodnić z 3/4. Wersja3729514 zachowana na `archive/2026-10-04/model-research-integration-held`. |
| 8 | `gpt/repo-hygiene-2026-10-04` / `51caa0c` | [raport W8](https://github.com/klb-t/chatadhd/blob/51caa0ca58b1704a28dae23202b3c8868fa488b3/docs/reports/repo-hygiene-2026-10-04.md), [szkic README](https://github.com/klb-t/chatadhd/blob/ee97311d6545ca19e19266d2f37514f2663f0aa9/docs/reports/repo-hygiene-readme-draft-2026-10-04.md) | W toku: świeży pierwszy CI dev wykonał 18/18 kontekst i 18/18 wiedzę, ale guard odrzucił brak JUnit; Clang zatrzymały dwa unused[this] w app.cpp. Do10: poprawić captures; do8: finalny CI/RESULTS; do9: rebase/bramki i README po odbiorze. Seeding 36/36 używane; zachować. Strażnik 16/16. |
| 9 | `gpt/integrator-2026-10-04` | [raport integratora](integrator-2026-10-04.md) | Weryfikacja i integracja; nie edytuje implementacji innych wątków. |
| 10 | `gpt/interface-2-2026-10-04` / `ccc8bbf` | [raport W10](https://github.com/klb-t/chatadhd/blob/ccc8bbf9f02941978e4a95952ca96b1dfa72437c/docs/reports/interface-2-2026-10-04.md) | W toku; autor wymaga końcowego receipt przed odbiorem. Do9: nie promować zależności2/3/4/1/11 przez sam UI. TaskEngine submit/checkpoint/result nie ma jeszcze publicznego adaptera. |
| 11 | `gpt/data-profiles-2026-10-04` / `c21e664` | [inwentarz W11](https://github.com/klb-t/chatadhd/blob/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code/README.md) | Opublikowano inwentarz przed edycją: 695 grup, 559 plików, 39496 kandydatów mechanicznych. Przyrost wyłącznie dokumentacyjny; migracje i graf metod pozostają otwarte. Raporty przekazane poniżej; ponownie przydzielono zadania 1/2. |

## Przekazywanie raportów wątku 11

Odebrano checkpoint c21e664 przypięty do 161cc22, z 13 raportami i bez zmiany
wykonywalnego kodu. Liczby sprawdzono: 695 unikalnych grup; 559 plików
(450 production/instrument, 107 fixture, 2 generated); 39496 wierszy mechanicznych.
To inwentarz, nie 695 wdrożonych migracji ani dowód kompletnej semantyki.
Właściciele muszą sprawdzić lokalizacje na własnym aktualnym kodzie.

| Adresat | Grupy / przypięty raport | Przekazane zadanie / stan |
|---|---|---|
| 1 | 127 / [thread-1](https://github.com/klb-t/chatadhd/blob/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code/thread-1.md) | **Ponowny przydział** po pierwszym przyroście: prompty/receptury jako wersje metod w grafie, reguły i parametry jako dane; uzgodnić granicę semantic_llm z 11. Odbiór nowego zadania niepotwierdzony. |
| 2 | 5 / [thread-2](https://github.com/klb-t/chatadhd/blob/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code/thread-2.md) | **Ponowny przydział** po pierwszym przyroście: DIC0325–0329, startup/config/path/model/log/worker presety z nakładką, identyczne defaults i porównanie przed/po. Przed odbiorem backendu dodatkowo wynieść nowy usage_policy_defaults do autorytatywnych danych. Odbiór niepotwierdzony. |
| 3 | 51 / [thread-3](https://github.com/klb-t/chatadhd/blob/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code/thread-3.md) | Presety selektora/typowania, kombinacje i fallback; rejestr i ślad w grafie, z 4. Ogólne providery poza embed nie rozszerzają automatycznie zakresu 3. |
| 4 | 27 / [thread-4](https://github.com/klb-t/chatadhd/blob/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code/thread-4.md) | KB/pack/store w aktualnym rozszerzonym zakresie 4; format method/version/run GraphPacket z 3. Capi_knowledge poza literalnym capi_packet pozostaje granicą do przydziału. |
| 5 | 52 / [thread-5](https://github.com/klb-t/chatadhd/blob/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code/thread-5.md) | Profile formatów i ról, DB/FTS/audit defaults; najpierw naprawa checkpointu interpretacji. Import/audit w cli/main.cpp należy do 5. |
| 6 | 31 / [thread-6](https://github.com/klb-t/chatadhd/blob/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code/thread-6.md) | Archiwum/aliasy/sketch/scoring/linking jako presety; ślad efektywnych wag/progów metod. |
| 7 | 9 / [thread-7](https://github.com/klb-t/chatadhd/blob/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code/thread-7.md) | Wersjonowane receptury badań i datowane twierdzenia o metodach w formacie profili modeli; bez przepisywania historycznych wyników. |
| 8 | 1 / [thread-8](https://github.com/klb-t/chatadhd/blob/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code/thread-8.md) | DIC0692 seeding dimensions/projection; zachować zamrożone wyniki. Dodać do inwentarza jawne wyjątki ASan/opt-in ze strażnika dowodów. |
| 9 | 98 / [thread-9](https://github.com/klb-t/chatadhd/blob/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code/thread-9.md) | Luki zakresów: 11 native/core/CAPI/crypto, 25 Android, 62 retained Python/tooling. To zadania koordynacji, nie prawo do edycji tych implementacji przez 9. |
| 10 | 81 / [thread-10](https://github.com/klb-t/chatadhd/blob/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code/thread-10.md) | UI/server profiles i overlay; metody edytować jak węzły grafu. Packet routes app.cpp należą do 4; pozostałe server/UI do 10. Naprawić Clang captures z raportu 8. |
| 11 | 213 / [thread-11](https://github.com/klb-t/chatadhd/blob/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code/thread-11.md) | Profile własnych subsystemów i ogólnego CLI; semantic_llm prompt/receptura koordynowana z 1, graph-memory selector z 3. Oznaczyć każdą metodę i kierunek jej reprezentacji w grafie. |

Przekazanie odbywa się przez ten opublikowany indeks i przypięte raporty;
nie oznacza potwierdzonego odbioru lub ukończenia przez autora.
Numer w raporcie nie rozszerza automatycznie podanych przez właściciela zakresów.
Android, retained Python, crypto, pozostałe core/CAPI, centralny eksport ABI
i rejestracja CMake pozostają jawnymi lukami własności. Nie przydzielamy ich
do implementacji integratorowi 9 ani nie edytujemy pod pozorem scalania.

## Wspólny format 3/4 — bramka odbioru

Przed połączeniem obu gałęzi wymagamy ich wspólnego, wersjonowanego kontraktu
i regresji obejmującej: metodę jako byt w grafie; wersję/hash promptu i przepisu;
parametry/preset/kombinację użytkownika; przebieg i krawędź wyniku do konkretnej
wersji metody. Oceny modeli/metod są datowanymi twierdzeniami z dowodami.
Sam JSON śladu poza grafem nie spełnia nowego polecenia właściciela.
Integrator sprawdza uzgodniony kontrakt; jego autorami pozostają 3 i 4.

## Do wątku 2 — warunek odbioru po nowym poleceniu

`loom/src/core/config_usage_policy.cpp::usage_policy_defaults()` tworzy preset
×10/window32/timeout30000/include_reservations=true w wykonywalnym źródle.
Możliwość nadpisania nie spełnia zasady, że polityka pochodzi z danych.
Przenieść autorytatywny dokument do `loom/data/policy/usage_policy.json` albo
uzgodnionego profilu; rzeczywiście go odczytywać we wszystkich default paths
(`Config::get`, `UsagePolicy::open`, `settings`, effective options), zachowując
nakładkę `loom_usage_policy` i walidację. Sam plik JSON obok ręcznej kopii C++
nie wystarczy. Generowane osadzenie danych jest zgodne z poleceniem.

Wymagany dowód: zmiana danych presetu oraz nakładki zmienia effective options,
bez zmiany domyślnej zgodności ani usuwania19 kontraktów. Jeśli generator/manifest
packa wymaga zmiany poza zakresem2, przekazać konkretną zależność.
Identyfikatory protokołu, stany lifecycle, integralność pokwitowań i sprawdzanie
reprezentowalności pozostają uniwersalnymi operacjami w kodzie.

## Do wątku N

Wszystkie otwarte przekazania są w tabeli. Raport końcowy każdego wątku ma
zakończyć się własną sekcją „Do wątku N”, z adresatem i konkretnym zadaniem.
Najnowsze polecenie właściciela: płatne wykonania wyłącznie w 7, w oddzielnym
budżecie 5€ i na kluczu z limitem 5€. Integrator wykonuje wyłącznie testy offline;
historyczna luka `$1.098135722` nie oznacza dostępnych środków.
