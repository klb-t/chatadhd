# Status integracji wszystkich aktualnych refs — 2026-10-09

Baza: `main@9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`. Źródło inwentarza: `branches.json`,134 wpisy (w tym symboliczny alias `origin`→HEAD/main). Uzupełnienie nie zmienia `branches.json` ani produktu.

**26/26** dozwolonych niearchiwalnych refs z commitami poza ancestry main sprawdzono `git cherry` na konkretnych SHA. **5** jest całkowicie równoważnych patchowo. **21** ma commity bez równoważnego pojedynczego patcha; to nie jest liczba nieprzyjętych wdrożeń. **6** pozostałych dozwolonych wpisów jest już zawartych w ancestry main. **101** archiwalnych refs zachowano jako wyłącznie inwentarz ancestry. **1** chronioną gałąź wykluczono przed dodatkowymi operacjami.

Nie odczytywano request bodies, korpusów, ZIP ani chronionej gałęzi. Dodatkowy pass korzystał z hashy commitów, subjectów, list ścieżek i blob OID. Git oblicza patch-equivalence wewnętrznie; żadne diffy ani treści blobów nie zostały wyświetlone/eksportowane. Nie uruchamiano testów, modeli ani CI.

## Macierz dozwolonych niearchiwalnych refs

`−` = patch-equivalent z main; `+` = brak pojedynczego odpowiednika patchowego. Liczby pomijają merge commity, które są odrębnie policzone w JSON. Status przyjęcia z INDEX jest oddzielony od niezależnego potwierdzenia metadanymi.

| Ref (bez origin/) | SHA | Poza ancestry | − | + | Status |
|---|---|---:|---:|---:|---|
| `origin` | `9e20f99ab27e` | 0 | 0 | 0 | zawarte w ancestry main |
| `claude/chataddhd-cpp-loom-core-IRGRN` | `9e20f99ab27e` | 0 | 0 | 0 | zawarte w ancestry main |
| `gpt/application-profiles-2026-10-04` | `161cc22dfb84` | 0 | 0 | 0 | zawarte w ancestry main |
| `gpt/archive-import-2-2026-10-05` | `f494931dbf24` | 10 | 9 | 1 | przyjęte według INDEX; pozostałość test dokument |
| `gpt/archive-import-2026-10-04` | `5f0abd20dd83` | 46 | 0 | 46 | niepewne pozostałości pierwszego przyrostu |
| `gpt/catalog-selection-2-2026-10-05` | `dab76dfc5c2b` | 3 | 3 | 0 | całość patch equivalent z main |
| `gpt/catalog-selection-2026-10-04` | `d9b29c502baf` | 10 | 10 | 0 | całość patch equivalent z main |
| `gpt/chat-selector-2-2026-10-05` | `2312d2ccc817` | 7 | 7 | 0 | całość patch equivalent z main |
| `gpt/chat-selector-2026-10-04` | `03c670caa6ee` | 20 | 0 | 20 | niepewne pozostałości pierwszego przyrostu |
| `gpt/data-profiles-2026-10-04` | `22873e047abe` | 25 | 0 | 25 | przyjęte według INDEX; różnice po integracji |
| `gpt/ecosystem-agent-research-2026-10-01` | `157762ef60be` | 342 | 0 | 308 | niepewna stara linia rozwoju |
| `gpt/graph-replies-2026-10-02` | `d49e05fd484e` | 341 | 0 | 307 | niepewna stara linia rozwoju |
| `gpt/integrator-2026-10-04` | `125a043cbe03` | 10 | 2 | 8 | dawny checkpoint integratora; pozostałość dowody |
| `gpt/integrator-queue-2026-10-05` | `215563c8fddc` | 0 | 0 | 0 | zawarte w ancestry main |
| `gpt/integrator-state-2026-10-04` | `b81f2732a8e9` | 2 | 0 | 2 | historyczna próba integracji; nie aktywna kolejka |
| `gpt/interface-2-2026-10-04` | `2fcb8228c966` | 38 | 9 | 29 | przyjęte według INDEX; odziedziczone powtórki |
| `gpt/knowledge-precision-2-2026-10-05` | `e4109df7e4af` | 0 | 0 | 0 | zawarte w ancestry main |
| `gpt/knowledge-precision-2026-10-04` | `50e6bb9f80b0` | 8 | 0 | 8 | przyjęte według INDEX; jawna poprawka integracyjna |
| `gpt/model-research-2026-10-04` | `ffe6443e82d7` | 51 | 47 | 4 | nieprzyjęte przygotowanie badawcze po deduplikacji |
| `gpt/native-graph-packet-2-2026-10-05` | `097859af292b` | 7 | 7 | 0 | całość patch equivalent z main |
| `gpt/native-graph-packet-2026-10-04` | `b302df25e1a6` | 14 | 0 | 14 | niepewne pozostałości pierwszego przyrostu |
| `gpt/night-development-2026-10-01` | `33fb30a5e8c2` | 338 | 0 | 304 | niepewna stara linia rozwoju |
| `gpt/onboarding-2-2026-10-05` | `374bab745ce3` | 19 | 10 | 9 | przyjęte INDEX i identyczne bloby runtime |
| `gpt/onboarding-2026-10-04` | `3c0bc36552ef` | 7 | 0 | 7 | pierwszy przyrost przyjęty według INDEX |
| `gpt/real-prompt-preparation-2026-10-05` | `ffe6443e82d7` | 51 | 47 | 4 | nieprzyjęte przygotowanie badawcze po deduplikacji |
| `gpt/real-source-intake-2026-10-05` | `62d1b0546574` | 48 | 47 | 1 | nieprzyjęte przygotowanie badawcze po deduplikacji |
| `gpt/repo-hygiene-2026-10-04` | `ced8f5a34bc1` | 25 | 25 | 0 | całość patch equivalent z main |
| `gpt/research-jev-validation-2026-10-05` | `0f52ac5f262b` | 55 | 47 | 8 | nieprzyjęte przygotowanie badawcze po deduplikacji |
| `gpt/research-prompt-preferences-2026-10-05` | `8e9d31ad00d2` | 50 | 46 | 4 | nieprzyjęte przygotowanie badawcze po deduplikacji |
| `gpt/usage-policy-2026-10-04` | `cc6a7587c454` | 11 | 6 | 5 | runtime patch equivalent; pozostałość testy dowody |
| `main` | `9e20f99ab27e` | 0 | 0 | 0 | zawarte w ancestry main |
| `wip/worktree-agent-a342fccb481c4116c` | `5d85034e2353` | 186 | 0 | 170 | niepewna stara linia rozwoju |

## Wnioski do rzeczywistej kolejki

- W6 obydwa przyrosty, W3 drugi, W4 drugi i higiena W8: pełna patch-equivalence oraz przyjęcie zapisane w INDEX. Nie przyjmować drugi raz.
- W5 drugi: jedyny nierównoważny commit dotyka testu i dokumentu; INDEX opisuje konflikt dwóch importów. Nie ma pozostałej zmiany runtime w tym przyroście.
- W12 drugi: mimo 9 `+`, 19/19 kandydatów kod/dane ma identyczne bloby w main. INDEX potwierdza, że replay pierwszego przyrostu pominięto jako już przyjęty.
- W11 dane/profile, W1 caller i W10 interface: INDEX wymienia dokładne źródłowe tipy i przyjęcie; W1 dostał jawną poprawkę ponawiania. Nieekwiwalentne commity odziedziczonych przyrostów nie tworzą nowej kolejki. Konkretne różnice blobów są w JSON.
- W2 usage-policy: 6 równoważnych commitów; 5 nierównoważnych zmienia tylko testy/evidence/docs. Dawny integrator ma analogiczną pozostałość dowodową, nie nowy runtime.
- Pięć refs badań real-source/prompts/Jev nadal ma niewłączone narzędzia/przygotowanie offline, bez nowych zmian produktu. `model-research` i `real-prompt-preparation` mają ten sam tip `ffe6443`; intake `62d1b05` jest jego przodkiem. Nie liczyć tego jako trzech niezależnych pakietów. Szczegóły w `branch-review.md`.
- Pierwsze przyrosty archive-import/chat-selector/native-graph-packet i stare night/eco/graph/WIP pozostają częściowo **niepewne** na poziomie semantycznego przeniesienia historycznych zmian. Wykaz nie jest potwierdzeniem nowego backlogu implementacyjnego.

## Konkretne różnice do ewentualnego wznowienia historii

### gpt/archive-import-2026-10-04

INDEX jawnie przyjmuje drugi przyrost W5, ale ten metadata-pass nie dowodzi przyjęcia każdego z 46 nieekwiwalentnych commitów starej gałęzi.15/22 kodowych blobów identycznych; konkretnie 7 różnych wskazano w JSON. To kandydaci do porównania, nie 46 nowych zadań.

Różne/nieobecne bloby: `loom/cli/main.cpp`, `loom/include/loom/importer.h`, `loom/src/import/export_internal.h`, `loom/src/import/import_audit.cpp`, `loom/src/import/import_audit.h`, `loom/src/import/import_usage.cpp`, `loom/src/import/importer_core.cpp`.

### gpt/chat-selector-2026-10-04

Drugi przyrost W3 jest patch-equivalent i przyjęty w INDEX. Starszy tip ma 28/30 kodowych blobów identycznych; chat_engine.cpp/context_engine.cpp są inne. Brak metadanych wystarczających do rozstrzygnięcia wszystkich starych 20 commitów.

Różne/nieobecne bloby: `loom/src/chat/chat_engine.cpp`, `loom/src/context/context_engine.cpp`.

### gpt/native-graph-packet-2026-10-04

Drugi przyrost W4 jest patch-equivalent i przyjęty. Pierwszy ma 6/8 kodowych blobów identycznych; różnią się server/src/app.cpp i src/kb/pack.cpp. Nieekwiwalentność 14 commitów nie dowodzi 14 pominiętych zmian.

Różne/nieobecne bloby: `loom/server/src/app.cpp`, `loom/src/kb/pack.cpp`.

### gpt/data-profiles-2026-10-04

INDEX jawnie mapuje 22873e0 na przyjęty rebase 215563c. 73 z 78 kandydatów kod/dane ma ten sam blob na main; 4 różne, 1 nie ma już na tipie. To nie 25 nowych zadań; same patch IDs nie rozpoznają całej historii tego intake.

Różne/nieobecne bloby: `loom/cli/main.cpp`, `loom/data/runtime/cli.pack`, `loom/data/validation/product_literals.pack`, `loom/src/model/runtime_profiles_embedded.inc`.

### gpt/knowledge-precision-2026-10-04

INDEX wymienia dokładny 50e6bb9 i naprawę nowego operation_id przy ponowieniu.17/19 blobów kod/dane identycznych; różnice semantic.cpp i pack_embedded.inc wymagają zachowania nowszego main, nie automatycznego cherry-pick starego callera.

Różne/nieobecne bloby: `loom/src/extract/semantic.cpp`, `loom/src/kb/pack_embedded.inc`.

### gpt/interface-2-2026-10-04

Własne 9 commitów UI są patch-equivalent;29 + obejmuje odziedziczone W1/W12, które INDEX jawnie pomija jako powtórki. 14 różnych blobów to semantic/pack i onboarding rozwijany później; nie przywracać całej gałęzi.

Różne/nieobecne bloby: `loom/data/profiles/user.pack`, `loom/include/loom/onboarding_layers.h`, `loom/src/extract/semantic.cpp`, `loom/src/kb/pack_embedded.inc`, `loom/src/onboarding/builtin.inc`, `loom/src/onboarding/layers.cpp`, `loom/src/onboarding/store.cpp`, `loom/web/src/onboarding/OnboardingPanel.tsx`, `loom/web/src/onboarding/WhatAppKnows.tsx`, `loom/web/src/onboarding/controller.ts`, `loom/web/src/onboarding/index.ts`, `loom/web/src/onboarding/native-snapshot.ts`, `loom/web/src/onboarding/onboarding.css`, `loom/web/src/onboarding/types.ts`.

## Archiwa i granice

Wszystkie 101 `origin/archive/**` są wymienione indywidualnie z SHA i ancestry w JSON, statusem `historia_tylko_inwentarz_ancestry`. Nie wykonano dla nich cherry ani audytu historycznych commitów. Dla chronionej gałęzi zachowano wyłącznie wcześniej przekazany wpis inwentarza; wykonano zero dodatkowych operacji.

Równość blobów dowodzi identyczności pliku przy dwóch tipach, nie jakości funkcji; różność nie dowodzi regresji lub pominięcia, bo main rozwija się dalej. Przyjęcie przyrostu nie zamyka całego wymagania. Na tym etapie zakończono pełną macierz metadanych aktualnych refs; nie deklaruje się pełności semantycznego audytu całej historii. Najbardziej konkretne dalsze zadanie historyczne to porównanie 7/2/2 różniących się plików pierwszych przyrostów W5/W3/W4, następnie wspólnej starej linii night/eco/graph/WIP — bez zaglądania do chronionych danych. B/C 2026-10-09 sprawdza osobno sesja główna.
