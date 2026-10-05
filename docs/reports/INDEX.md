# Indeks wątków — 2026-10-05

Właściciel: wątek9. Baza sesji `30ad7d3`; cały INTERFEJS/PR9 i wcześniejsze R39–R41/usage zachowane.
Status dotyczy przypiętego przyrostu, nie ukończenia całego R39–R41. Kolejność właściciela: **3+4 →5 →1 →12 →10 →11/8 →6(mechanizm)**.
Każdy odbiór: świeży fetch, rzeczywisty rebase, pełny CTest + niezmieniony guard wykonanych przypadków, build web i fast-forward bez merge/force.

| Wątek | Gałąź / źródło autora | Raport | Status / przekazanie |
|---|---|---|---|
|1| `gpt/knowledge-precision-2026-10-04` / `50e6bb9` | [raport1](https://github.com/klb-t/chatadhd/blob/50e6bb9f80b0cd855e4dd1efedaf3399cac5e4e6/docs/reports/knowledge-precision-2026-10-04.md) | Gotowy pierwszy przyrost; następny po5. Dalsze prompty jako wersje metod/legacy callers pozostają osobnym zadaniem. |
|2| `gpt/usage-policy-2026-10-04` / `cc6a758` | [raport2](https://github.com/klb-t/chatadhd/blob/cc6a7587c4548cc455f69a6b8bfa3490cde94bb7/docs/reports/usage-policy-2026-10-04.md) | PRZYJĘTY pierwszy przyrost6930fd2:6defaults→0ręcznych,5defaultpaths; startup/config dalszy przyrost osobno. Nie czeka na3/4/11. |
|3| `gpt/chat-selector-2026-10-04` / `03c670c` | [raport3](https://github.com/klb-t/chatadhd/blob/03c670caa6ee3d8ac2c478186548114f8e83927f/docs/reports/chat-selector-2026-10-04.md) | PRZYJĘTY przyrost; źródło9 `a1be689`. CTest110/110, native663/24489, Python1303,0skip; webPASS. Wspólny kontrakt METHOD_GRAPH i kanoniczny golden uzgodnione. Integrator: registry/wykonanie/ślad121/1604, canonical+freshnativeconsumerPASS, alias/nested/nested+alias3/3PASS, KBwindowPASS. 3 czystyrebase na30ad7d3; bez merytorycznych zmian. Historyczne negatywy4 pozostają na archive; domyślneprofile/legacywiring to dalszyprzyrost. |
|4| `gpt/native-graph-packet-2026-10-04` / `b302df2` | [raport4](https://github.com/klb-t/chatadhd/blob/b302df25e1a65f20c395eadc5ad5ef065d26e33d/docs/reports/native-graph-packet-2026-10-04.md) | PRZYJĘTY przyrost; źródło9 `a1be689`. CTest110/110, native663/24489, Python1303,0skip; webPASS. Wspólny kontrakt METHOD_GRAPH i kanoniczny golden uzgodnione. Integrator: registry/wykonanie/ślad121/1604, canonical+freshnativeconsumerPASS, alias/nested/nested+alias3/3PASS, KBwindowPASS. 3 czystyrebase na30ad7d3; bez merytorycznych zmian. Historyczne negatywy4 pozostają na archive; domyślneprofile/legacywiring to dalszyprzyrost. |
|5| `gpt/archive-import-2026-10-04` / `5f0abd2` | [raport5](https://github.com/klb-t/chatadhd/blob/5f0abd20dd83e5c0e7f43e84c333d5e23a5e681f/docs/reports/archive-import-2026-10-04.md) | Po rebase na e4109df; źródło9 66da570. Niezależny OCR3/3 +22/22 i checkpoint2/2 +31/31 PASS, [dowód](integrator-intake-2026-10-05/5-replay-checkpoint.md). Pełny mixed build/CTest/guard/web jeszcze w toku; main nie przesunięty dla5. |
|6| `gpt/catalog-selection-2026-10-04` / `d9b29c5` | [raport6](https://github.com/klb-t/chatadhd/blob/d9b29c502bafd3b4cb17f187e3e74f32cfff72ee/docs/reports/catalog-selection-2026-10-04.md) | Odbieramy wyłącznie mechanizm. Domyślna jakość31/45,0FP: NEUTRALNA. Opcjonalny DEV33/45 nie promowany; blind nie czytany ani używany ponownie. |
|7| `gpt/model-research-2026-10-04` / `5753d2c` | [raport7](https://github.com/klb-t/chatadhd/blob/5753d2cf93dcba4edd5c35534b667f23b915c9b7/docs/reports/model-research-2026-10-04.md) | Przygotowania offline; właściciel przekaże osobny klucz5€. Integrator0paidcalls. |
|8| `gpt/repo-hygiene-2026-10-04` / `758aba6` | [raport8](https://github.com/klb-t/chatadhd/blob/758aba648f0b2e848828c603b54cc3c2b8abfbcf/docs/reports/repo-hygiene-2026-10-04.md) | Po rebase i poprawceClanga10: potrzebna aktualna pełna macierz Clang/vendored iASan. Seeding/results nadal używane; zachować. |
|9| `gpt/integrator-state-2026-10-04` | [raport9](integrator-intake-2026-10-05/README.md) | Odbiera całą kolejkę; każdy przyrost ma własne bramki i wpis indeksu. |
|10| `gpt/interface-2-2026-10-04` / `fa7538d` | [raport10](https://github.com/klb-t/chatadhd/blob/fa7538d/docs/reports/interface-2-2026-10-04.md) | fa7538d: autor zakończył pełny CTest108/108, web, fixture84/84, nativeUI20/20, E2E16/16. Gotowy do osobnego rebase i mixed gates9 po12. Clang captures naprawione. |
|11| `gpt/data-profiles-2026-10-04` / `d3488a6` | [raport11](https://github.com/klb-t/chatadhd/blob/d3488a6bee016d1ea5e6e286dbc7d3a0249761fc/docs/reports/data-in-code/README.md) | WSTRZYMANY: data/runtime/usage_policy.pack:6–12 drugi literalny preset; generator/model loader niezależne od kanonicznego2. Wymagana poprawka autora i rebase. Reszta kolejki nie czeka. |
|12| `gpt/onboarding-2026-10-04` / `3c0bc36` | [raport12](https://github.com/klb-t/chatadhd/blob/3c0bc36552ef9851f1174946cfb108549aae3228/docs/reports/onboarding-2026-10-04.md) | Gotowy przyrost onboarding/profile/layers/store + komponentyUI. Most HTTP/nawigacja/retencja to dalsze zadania10/3/11; nie deklarujemy pełnej aplikacji. |

## Wspólny format3/4

Jeden kontrakt: [METHOD_GRAPH.md](../../loom/src/packet/METHOD_GRAPH.md), `loom.method_graph/1` / `loom.method_run_trace/1`.
Kanoniczny golden: [input.json](chat-selector-2026-10-04-evidence/golden-consumer/input.json), SHA256 `8db8175c3b70c3947711ddf5daee5073b99bc5a5114ce0075e06e51932cec54e`.
ACK4 jest w b302df2. Integrator wykonał actual registry/prepare/HTTPmock/resultbinding3 i natywny zapis/restart4; oba canonical/freshconsumerPASS.
121/1604 i3/3alias/nested/nested+alias: PASS. Jedno kontrolowane fakeHTTP,0zewnętrznych/paidcalls. Domknięcie tej bramki nie kończy dalszych migracji domyślnych packów/profili3.

## Aktywność2/3/4

Fetch05.10 potwierdził2=cc6a758 i3=03c670c, identyczne refs jak podczas fetch04.10 18:24UTC: ponad2h bez nowego tipa między obserwacjami.
Gitfetch nie dostarcza dokładnego czasu push; nie przedstawiamy committertime jako push.4 ma nowy b302df2 (committer04.10 19:42:58UTC).
Nie ma cyklu oczekiwania:2pierwszyprzyrost jużmain;3rebase wykonał9;4ACK zamknięty. Dalsze2/config nie blokuje3/4.

## Raporty11 i ponowne przydziały

Inwentarz c21e664:695 grup,559plików,39496wierszy mechanicznych; nie oznacza wdrożenia695migracji.
[Oryginalny podział](https://github.com/klb-t/chatadhd/tree/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code) pozostaje punktem wyjścia, lokalizacje wymagają sprawdzenia na aktualnym źródle.

| Do wątku | Grupy | Otwarte zadanie po przyroście |
|---|---|---|
|1|127|Wersje metod/promptów w grafie, dalsze recipes/analysis callers;7 przekazuje najlepsze presety.|
|2|5|Ponowny przydział: startup/config/path/model/log/worker; identyczne defaults, overlay i ownership publicAPI.|
|3|51|Pack/profile defaults rejestru, legacy/resume/publicwiring; selector respektuje warstwy/wykluczenia12.|
|4|27|Dalsze KB pack/store profile i grafowy profil; wspólny kontrakt3/4 jużzamknięty.|
|5|52| Po rebase na e4109df; źródło9 66da570. Niezależny OCR3/3 +22/22 i checkpoint2/2 +31/31 PASS, [dowód](integrator-intake-2026-10-05/5-replay-checkpoint.md). Pełny mixed build/CTest/guard/web jeszcze w toku; main nie przesunięty dla5. |
|6|31|Nowe strojenie tylkoDEV; archiwum/alias/sketch/score jako dane; jakośćdomyślna neutralna.|
|7|9|Offline plan etapów/kosztów; datowane twierdzenia o metodach. Klucz5€ dostarczywłaściciel.|
|8|1|Seeding dimensions/projection, bridge metod; pełna aktualna macierzCI, bez luzowania guard.|
|9|98|Koordynacja luk11native/core/CAPI/crypto,25Android,62retainedPython/tooling; nieprzejmować implementacji.|
|10|81|MostUI/HTTP/nawigacja12, what-app-knows, retencja; ustawienia2 i metody3/4 jako węzły.|
|11|213|Foundation/profile subsystemy; jedno źródłousage2, nie drugi preset; koordynacja1/3/12.|
|12|nowy|Onboarding foundationodebrać; dalszeconsumers/retencję/nawigację uzgodnić3/10/11.|

## Do wątku11 — konkretny bloker

`loom/data/runtime/usage_policy.pack:6–12` kopiuje sześć wartości ręcznie. `loom/src/model/gen_runtime_profiles.py:16–34` czyta tylko runtime/*.pack;
`runtime_profile.cpp:249–265` ładuje tę niezależną tabelę; `test_runtime_bootstrap_profiles.cpp:60–68` porównuje kolejne literały.
Wyprowadzić wrapper11 z kanonicznego `loom/data/policy/usage_policy.pack`2, zachować walidację/nakładkę oraz shallowConfigoverride.
Potem rebase, regeneracja i dowód zmiany canonicalsource plus pełne bramki. Integrator nie edytuje implementacji11.

## Archiwum i ograniczenia dowodów

Przed selekcją pełne źródła zachowujemy na archive/2026-10-05/*-before-intake; oryginalne gałęzie i wszystkie historyczne negatywy pozostają dostępne.
Na main przenosimy wyłącznie wybrane implementacje/testy i pozytywne dowody. Lokalne pierwsze nieudane próby9 nie zostały wypchnięte przed automatycznym czyszczeniem; surowe logi utracono. Kompletny pozytywny ZIP90plików jest zachowany i niezależnie sprawdzony.
Nie zmieniono progów ani usunięto testów. Opt-in catalogscale jawnie0/0, pozostałe wymagane zestawy rzeczywiście wykonane.
Nie czytano ślepego korpusu ani eval/real-holdout-key;0nowych paidcalls.

## Do wątku9

Kontynuować całą kolejkę, aktualizować ten indeks po każdym fast-forward. Wstrzymany11 nie blokuje8 ani6.
Jeśli pełna bramka konkretnego źródła nie przejdzie, podać dokładny log/miejsce i zwrócić autorowi, zachować pełny negatyw.

## Do wątku N

Otwarte przekazania są przypisane w tabelach; status przyrostu nie jest zamknięciem wszystkich wymagań właściciela.
