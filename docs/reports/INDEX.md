# Indeks wątków — 2026-10-04

Właściciel indeksu: wątek 9, `gpt/integrator-2026-10-04`.
Baza tej kontroli: `main` `161cc22`; zachowane wszystkie zmiany INTERFEJS/PR9.
Status dotyczy wskazanego commitu, nie przyszłych zmian na gałęzi.
Kolejność odbioru: **2 → 3/4/5 → pozostałe**. Nieukończona gałąź nie jest
przyjmowana jako gotowy wątek. Każdy przyjęty przyrost wymaga świeżego fetch,
rebase, pełnego CTest z kontrolą wykonanych przypadków, build web i fast-forward.

| Wątek | Gałąź / sprawdzony commit | Raport lub dowód | Status i otwarte przekazania |
|---|---|---|---|
| 1 | `gpt/knowledge-precision-2026-10-04` / `a042ab7` | [raport W1](https://github.com/klb-t/chatadhd/blob/a042ab7b45d9cc908154cf0252fbae4bca451d3c/docs/reports/knowledge-precision-2026-10-04.md) | Pierwszy przyrost precyzji gotowy do bramek; w kolejce po 3/4/5. Ponowne zadanie: prompty jako dane i węzły metod, nakładka, podgląd/zmiana wywołania, tryby walidacji; uzgodnić z11 przeniesienie `semantic_llm` i z7 presety. |
| 2 | `gpt/usage-policy-2026-10-04` / `34cc920` | [raport W2](https://github.com/klb-t/chatadhd/blob/34cc920dd3cdb0c0fca0a514569b19111429583f/docs/reports/usage-policy-2026-10-04.md) | Gotowa infrastruktura, bramki integratora trwają. Do3/4/5: jawnie adoptować lifecycle; do10: konfiguracja i JSON API są opisane, nowy dispatcher tylko static-kernel. Nie daje exactly-once wykonania. |
| 3 | `gpt/chat-selector-2026-10-04` / `36ec2a8` | [raport W3](https://github.com/klb-t/chatadhd/blob/36ec2a8f6340ba3d7873d3c4864e3af23178bcd6/docs/reports/chat-selector-2026-10-04.md) | W toku, adapter embed nie kończy selektora. Do4: wspólny format metod/wersji/przebiegów i krawędzi pochodzenia **przed integracją**. Do10: wystawić capability/ustawienia/rejestr i tryby odpowiedzi grafowej. |
| 4 | `gpt/native-graph-packet-2026-10-04` / `3caa6b4` | [raport W4](https://github.com/klb-t/chatadhd/blob/3caa6b4dcfb6412294bf816c1105bcf99afacdbc/docs/reports/native-graph-packet-2026-10-04.md), [reprodukcja](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-packet-unresolved-replay/docs/reports/integrator-packet-replay-2026-10-04.md) | Wstrzymany: powtórzenie unresolved wykonuje operację drugi raz przy jednym rozliczeniu. Do4: poprawka/regresja; do3: format metody/wersji/przebiegu w GraphPacket. Oryginalny kod i wynik negatywny zachowane w archive. |
| 5 | `gpt/archive-import-2026-10-04` / `c797225` | [dowód audytu](https://github.com/klb-t/chatadhd/blob/c7972252a23cb13c54797f6b57df24de903c5296/docs/reports/archive-import-2026-10-04-evidence/audit/README.md) | W toku; brak końcowego raportu. Sprawdzanie wznowienia po błędzie znanego pliku pomocniczego trwa. Do5: końcowe pomiary/receipt i instrukcja właściciela. |
| 6 | `gpt/catalog-selection-2026-10-04` / `1fb25ae` | [protokół W6](https://github.com/klb-t/chatadhd/blob/1fb25ae7a826fbc9897897cc69ea6ccb94f052a6/docs/reports/catalog-selection-2026-10-04-protocol.md) | W toku: helper semantyczny bez wywołania przez selekcję; brak 14 regresji i wyników. Pierwsze spojrzenie na ślepy korpus należy do6 na samym końcu; integrator nie czyta korpusu ani klucza. |
| 7 | `gpt/model-research-2026-10-04` / `68f7531` | [reprodukcja audytu](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-readiness-orphan-response/docs/reports/integrator-readiness-orphan-2026-10-04.md) | W toku/wstrzymany: odpowiedź bez wpisu w ledgerze pomijana, gdy ledger istnieje; brak raportu końcowego. Frontier offline 17/17; nie jest wynikiem jakości modeli. Do7: naprawić audyt; do1: zwycięskie przepisy z dowodami; format twierdzeń o metodach uzgodnić z3/4. Cała wersja68f7531 zachowana na `archive/2026-10-04/model-research-integration-held`. |
| 8 | `gpt/repo-hygiene-2026-10-04` / `ee97311` | [szkic README](https://github.com/klb-t/chatadhd/blob/ee97311d6545ca19e19266d2f37514f2663f0aa9/docs/reports/repo-hygiene-readme-draft-2026-10-04.md) | W toku: poprawiono historyczne0/0; brak końcowego raportu i świeżego pełnego receipt. Do9: README pozostaje szkicem do odbioru. Używane dane seeding zachować; kontrola 36/36. Strażnik wykonanych przypadków 16/16. |
| 9 | `gpt/integrator-2026-10-04` | [raport integratora](integrator-2026-10-04.md) | Weryfikacja i integracja; nie edytuje implementacji innych wątków. |
| 10 | `gpt/interface-2-2026-10-04` / `ccc8bbf` | [raport W10](https://github.com/klb-t/chatadhd/blob/ccc8bbf9f02941978e4a95952ca96b1dfa72437c/docs/reports/interface-2-2026-10-04.md) | W toku; autor wymaga końcowego receipt przed odbiorem. Do9: nie promować zależności2/3/4/1/11 przez sam UI. TaskEngine submit/checkpoint/result nie ma jeszcze publicznego adaptera. |
| 11 | brak opublikowanej gałęzi/raportów w tym fetch | oczekiwane `data-in-code/thread-N.md` i zbiorczy JSON | Do11: inwentarz całego repo przed edycją, oznaczenie metod i kierunku do grafu. Wątek9 będzie przekazywał raporty właściwym właścicielom, także tym po zakończeniu pierwszego przyrostu. |

## Przekazywanie raportów wątku 11

Nie odebrano jeszcze opublikowanego inwentarza. Nie zastępujemy go własnym
wyborem danych do wyniesienia. Po odebraniu każde `thread-N.md` otrzyma tutaj
link do przypiętej wersji, adresata, stan odbioru i wynik ponownego przydziału.
Wątki 1 i 2 mają już wstępnie zakończone pierwsze przyrosty; ich dodatkowe zadania
pozostają otwarte. W11 nie edytuje ich zakresów. Dotyczy to także współdzielonej
granicy promptów pomiędzy `extract/semantic.cpp` (1) i `semantic/semantic_llm.cpp` (11).

## Wspólny format 3/4 — bramka odbioru

Przed połączeniem obu gałęzi wymagamy ich wspólnego, wersjonowanego kontraktu
i regresji obejmującej: metodę jako byt w grafie; wersję/hash promptu i przepisu;
parametry/preset/kombinację użytkownika; przebieg i krawędź wyniku do konkretnej
wersji metody. Oceny modeli/metod są datowanymi twierdzeniami z dowodami.
Sam JSON śladu poza grafem nie spełnia nowego polecenia właściciela.
Integrator sprawdza uzgodniony kontrakt; jego autorami pozostają 3 i 4.

## Do wątku N

Wszystkie otwarte przekazania są w tabeli. Raport końcowy każdego wątku ma
zakończyć się własną sekcją „Do wątku N”, z adresatem i konkretnym zadaniem.
Najnowsze polecenie właściciela: płatne wykonania wyłącznie w 7, w oddzielnym
budżecie 5€ i na kluczu z limitem 5€. Integrator wykonuje wyłącznie testy offline;
historyczna luka `$1.098135722` nie oznacza dostępnych środków.
