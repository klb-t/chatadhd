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
| 2 | `gpt/usage-policy-2026-10-04` / `03b0c4e` | [raport W2](https://github.com/klb-t/chatadhd/blob/03b0c4ed954e53cb72ec7947fe6e5d9ad28d495f/docs/reports/usage-policy-2026-10-04.md) | **Wstrzymany:** preset policy nadal w C++; nowe settings/preview uczciwie oznacza brak migracji. Do2: autorytatywne dane/loader/nakładka. Źródło ma teraz23 grupy; integrator wykonał19/19 na poprzednim34cc920, nie na03b0c4e. Build native/web poprzedniej wersji zielony, osobny końcowy pełny CTest108/108+guard zielony (659 native/1276 Python); pierwsze106/108 zachowane. Do10: preview zastępuje całą nakładkę, jest read-only/doradcze, static API bez shared export. |
| 3 | `gpt/chat-selector-2026-10-04` / `36ec2a8` | [raport W3](https://github.com/klb-t/chatadhd/blob/36ec2a8f6340ba3d7873d3c4864e3af23178bcd6/docs/reports/chat-selector-2026-10-04.md) | W toku, adapter embed nie kończy selektora. Do4: wspólny format metod/wersji/przebiegów i krawędzi pochodzenia **przed integracją**. Do10: wystawić capability/ustawienia/rejestr i tryby odpowiedzi grafowej. |
| 4 | `gpt/native-graph-packet-2026-10-04` / `3caa6b4` | [raport W4](https://github.com/klb-t/chatadhd/blob/3caa6b4dcfb6412294bf816c1105bcf99afacdbc/docs/reports/native-graph-packet-2026-10-04.md), [reprodukcja](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-packet-unresolved-replay/docs/reports/integrator-packet-replay-2026-10-04.md) | Wstrzymany: powtórzenie unresolved wykonuje operację drugi raz przy jednym rozliczeniu. Do4: poprawka/regresja; do3: format metody/wersji/przebiegu w GraphPacket. Oryginalny kod i wynik negatywny zachowane w archive. |
| 5 | `gpt/archive-import-2026-10-04` / `cc66059` | [dowód W2](https://github.com/klb-t/chatadhd/blob/cc6605953b9a5ed0a1e84c497fdd1a15537ca0f2/docs/reports/archive-import-2026-10-04-evidence/w2-integration/README.md), [checkpoint replay](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-import-checkpoint-replay/docs/reports/integrator-import-checkpoint-2026-10-04.md) | **Wstrzymany:** checkpoint rozpoznanego membera nadal błędny; kod cc66059 taki sam jak6bfa652. Nowa poprawka bare JSON i terminalnego usage replay nie usuwa utraty projektu/dokumentu/linku. CLI nadal resume=true. Nowe dowody pamięci i3/3 probe W2 nie są pełną bramką. Do5: regresje/naprawa, raport końcowy, instrukcja importu i poprawny link replay. 2GiB tylko wygenerowano, nie zmierzono importu. |
| 6 | `gpt/catalog-selection-2026-10-04` / `e0caa3b` | [raport W6](https://github.com/klb-t/chatadhd/blob/e0caa3b7447a39c3c25c1226f7e3f0ff8ea77115/docs/reports/catalog-selection-2026-10-04.md) | **Wstrzymany przez autora i integratora:** helper już podłączony; DEV nadal31/45,14 pominięć. Sztuczne wektory14/14 sprawdzają mechanizm. Autor CTest104/106; pierwsze spojrzenie wykorzystane raz, bramka jakości nie przeszła (pułapki5/15). Cały checkpoint na `archive/2026-10-04/catalog-selection-integration-held`. Do6: nowy DEV przyrost, zachować negatyw; bez ponownego strojenia na ślepym korpusie. |
| 7 | `gpt/model-research-2026-10-04` / `5753d2c` | [dowód naprawy](integrator-readiness-fix-2026-10-04/README.md), [historyczny błąd](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-readiness-orphan-response/docs/reports/integrator-readiness-orphan-2026-10-04.md), [przygotowanie etapu5](https://github.com/klb-t/chatadhd/tree/5753d2cf93dcba4edd5c35534b667f23b915c9b7/docs/research) | W toku; **orphan fix zweryfikowany pozytywnie** na5753d2c: kontrola z/bez ledgeru blokuje, bound-only nie blokuje. Historia negatywna zachowana, nie przypisywać błędu nowej wersji. Frontier17 przypadków bez luzowania; etap2 zamrożone60 requestów z historycznym kosztem$0.3852768, zero nowych wywołań integratora. Do7: raport końcowy/rozliczenie, przygotowanie porównania graph-reply vs tekst+JSON, rzeczywisty eksport twierdzeń o metodach i przepisy do1. |
| 8 | `gpt/repo-hygiene-2026-10-04` / `2544cf3` | [raport W8](https://github.com/klb-t/chatadhd/blob/2544cf3a75c1477797213328f7279b3e8576579e/docs/reports/repo-hygiene-2026-10-04.md), [szkic README](https://github.com/klb-t/chatadhd/blob/ee97311d6545ca19e19266d2f37514f2663f0aa9/docs/reports/repo-hygiene-readme-draft-2026-10-04.md) | Kompletne receipts autora ponownie odczytano strażnikiem: dev108 outer/107 wykonanych,659 native/24465 assertions,1276 Python; ASan108/105,659/24464,1252 Python+24 jawne FFI skips. **Cała macierz niezielona:** vendored Clang nadal build failure,0CTest, dwa unused[this]. Do10: captures; do8: nowy CI po poprawce. Seeding36/36 używane, zachować; guard16/16. Do9: README po odbiorze. |
| 9 | `gpt/integrator-2026-10-04` | [raport integratora](integrator-2026-10-04.md) | Weryfikacja i integracja; nie edytuje implementacji innych wątków. |
| 10 | `gpt/interface-2-2026-10-04` / `9fb5b54` | [raport W10](https://github.com/klb-t/chatadhd/blob/9fb5b54e674308ea8dc9f9ca80b9ceb093fe2085/docs/reports/interface-2-2026-10-04.md) | W toku; usage preview zależy od API03 z2, packet od4, rejestr od3, prompt preview od1/11. Końcowy pełny receipt autora nadal pending; dwa Clang captures niepoprawione. Do10: naprawa/receipt; do9: nie promować backendów przez samo UI. TaskEngine submit/checkpoint/result bez publicznego adaptera. |
| 11 | `gpt/data-profiles-2026-10-04` / `b88154c` | [inwentarz](https://github.com/klb-t/chatadhd/blob/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code/README.md), [memory/selector](https://github.com/klb-t/chatadhd/blob/b88154c2caaeac676a4fbd19aefcc60eb546ff20/docs/reports/data-in-code/implementation-memory-selector.md), [semantics](https://github.com/klb-t/chatadhd/blob/b88154c2caaeac676a4fbd19aefcc60eb546ff20/docs/reports/data-in-code/implementation-semantics.md) | W toku: profile memory/legacy selector/analyzer/graph ingest/graph-memory/materialize są w kodzie. Inwentarz695/559 przed edycją przekazany poniżej. Pomocnicze stare-archive parity nie zastępuje świeżego baseline/fullCTest. Do11: propagować błędy szablonów stage; uzupełnić dowody/opisy CLI/commitu. Do3: checked APIs, overlay i walidacja embeddingów; do2/10: runtime injection/guard/adaptery. Graf metod nadal pending3/4. |

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
Domyślne metody muszą pochodzić z packa; definicje i kombinacje użytkownika
rejestr3 czyta/zapisuje w grafie. Pochodzenie modeli pozostaje `model`.
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

## Do wątku 11 — jawne błędy i dowody

`materialize::run_stage()` w b88154c ignoruje błędy `dossier()` oraz
`extrapolated_spec()` (if(result) bez gałęzi błędu). Nowa poprawna składniowo
nakładka `templates.dossier_header="{{missing}}"` może więc ukryć błąd renderowania
i zwrócić sukces z niepełnymi produktami. To wniosek ze sprawdzonego kodu,
nie niezależnie wykonany test nowej gałęzi. Do11: propagować błąd lub jawnie
zwrócić niekompletny wynik; dodać regresję stage, nie tylko samego renderera.

Zapisane memory/selector before/after są identyczne (59955B,
SHA256 `caec4bb87a68e1f284978793feceffa8a5b42445db38776c691561e425150360`),
lecz używają starszego archiwum9d15. Raport semantics jawnie pozostawia pełne
porównanie czterech produktów i fullCTest otwarte. CLI profile commands
reklamowane w RUNTIME_PROFILES nie występują w sprawdzonym drzewie;
opis inwentarza wskazuje nieopublikowany bab7be3 zamiast c21e664,
a odsyłacz focused-tests.log nie ma pliku. Do11: poprawić status/linki.

Do3: `SelectorEngine::index()` nie sprawdza liczności zwróconych embeddingów,
choć `set_profile()` już ją sprawdza. Wspólna walidacja liczności, wymiarów
i wartości dla index/query/reconfigure przed rankingiem; nadmiar wektorów
może wyjść poza corpus/ids. Użyć checked APIs i jawnego wstrzyknięcia overlay.

## Do wątku N

Wszystkie otwarte przekazania są w tabeli. Raport końcowy każdego wątku ma
zakończyć się własną sekcją „Do wątku N”, z adresatem i konkretnym zadaniem.
Najnowsze polecenie właściciela: płatne wykonania wyłącznie w 7, w oddzielnym
budżecie 5€ i na kluczu z limitem 5€. Integrator wykonuje wyłącznie testy offline;
historyczna luka `$1.098135722` nie oznacza dostępnych środków.
