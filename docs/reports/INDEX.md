# Indeks wątków — 2026-10-05

Właściciel: wątek9. Baza sesji `30ad7d3`; cały INTERFEJS/PR9 i wcześniejsze R39–R41/usage zachowane.
Status dotyczy przypiętego przyrostu, nie ukończenia całego R39–R41. Pierwszy3+4 zakończony. Aktualna kolejność właściciela: **5 →1 →12 →10 →Claude(docs) →11 →8 →6(mechanizm) →ready drugie przyrosty**.
Każdy odbiór: świeży fetch, rzeczywisty rebase, pełny CTest + niezmieniony guard wykonanych przypadków, build web i fast-forward bez merge/force.

| Wątek | Gałąź / źródło autora | Raport | Status / przekazanie |
|---|---|---|---|
|1| `gpt/knowledge-precision-2026-10-04` / `50e6bb9` | [raport1](https://github.com/klb-t/chatadhd/blob/50e6bb9f80b0cd855e4dd1efedaf3399cac5e4e6/docs/reports/knowledge-precision-2026-10-04.md) | PRZYJĘTY przyrost; źródło9 `a95a3e1`. CTest 117/117, native 710/26399, Python 1322/0skip; web PASS. CZĘŚCIOWO:20źródeł identycznych z50e6bb9:precyzja leksykalna i privatePromptRegistry.43/43registry;11sekcji metryk synthetic równe i24/24integrity. Zmienione inputhash/runID wynikają dokładnie z wersji packa; nie deklarujemy całej karty byte-parity. semantic.cpp/semantic_usage.h wykluczone na3FAIL/8asercjach retry; pełny negatyw archive. Backend wiring/graph-method projection pozostają otwarte. Selfhost autora:18005→12863claims i37249→37249observations; brak nowego selfhost9. |
|2| `gpt/usage-policy-2026-10-04` / `cc6a758` | [raport2](https://github.com/klb-t/chatadhd/blob/cc6a7587c4548cc455f69a6b8bfa3490cde94bb7/docs/reports/usage-policy-2026-10-04.md) | PRZYJĘTY pierwszy przyrost6930fd2:6defaults→0ręcznych,5defaultpaths; startup/config dalszy przyrost osobno. Nie czeka na3/4/11. |
|3| `gpt/chat-selector-2026-10-04` / `03c670c` | [raport3](https://github.com/klb-t/chatadhd/blob/03c670caa6ee3d8ac2c478186548114f8e83927f/docs/reports/chat-selector-2026-10-04.md) | PRZYJĘTY przyrost; źródło9 `a1be689`. CTest110/110, native663/24489, Python1303,0skip; webPASS. Wspólny kontrakt METHOD_GRAPH i kanoniczny golden uzgodnione. Integrator: registry/wykonanie/ślad121/1604, canonical+freshnativeconsumerPASS, alias/nested/nested+alias3/3PASS, KBwindowPASS. 3 czystyrebase na30ad7d3; bez merytorycznych zmian. Historyczne negatywy4 pozostają na archive; domyślneprofile/legacywiring to dalszyprzyrost. |
|4| `gpt/native-graph-packet-2026-10-04` / `b302df2` | [raport4](https://github.com/klb-t/chatadhd/blob/b302df25e1a65f20c395eadc5ad5ef065d26e33d/docs/reports/native-graph-packet-2026-10-04.md) | PRZYJĘTY przyrost; źródło9 `a1be689`. CTest110/110, native663/24489, Python1303,0skip; webPASS. Wspólny kontrakt METHOD_GRAPH i kanoniczny golden uzgodnione. Integrator: registry/wykonanie/ślad121/1604, canonical+freshnativeconsumerPASS, alias/nested/nested+alias3/3PASS, KBwindowPASS. 3 czystyrebase na30ad7d3; bez merytorycznych zmian. Historyczne negatywy4 pozostają na archive; domyślneprofile/legacywiring to dalszyprzyrost. |
|5| `gpt/archive-import-2026-10-04` / `5f0abd2` | [raport5](https://github.com/klb-t/chatadhd/blob/5f0abd20dd83e5c0e7f43e84c333d5e23a5e681f/docs/reports/archive-import-2026-10-04.md) | PRZYJĘTY przyrost; źródło9 `0a81480`. CTest 117/117, native 710/26399, Python 1322/0skip; web PASS. 31wybranych źródeł bez zmian merytorycznych po rebase; OCR3/3 MIME i22/22kontrole, checkpoint2/2scenariusze i31/31kontrole na rzeczywistym kernelu. Forward-only adnotacje i audit przyjęte; runbook autora przypięty w INDEX. Pierwsze timeouty zachowane w archive, progi bez zmian. |
|6| `gpt/catalog-selection-2026-10-04` / `d9b29c5` | [raport6](https://github.com/klb-t/chatadhd/blob/d9b29c502bafd3b4cb17f187e3e74f32cfff72ee/docs/reports/catalog-selection-2026-10-04.md) | Odbieramy wyłącznie mechanizm. Domyślna jakość31/45,0FP: NEUTRALNA. Opcjonalny DEV33/45 nie promowany; blind nie czytany ani używany ponownie. |
|7| `gpt/model-research-2026-10-04` / `5753d2c` | [raport7](https://github.com/klb-t/chatadhd/blob/5753d2cf93dcba4edd5c35534b667f23b915c9b7/docs/reports/model-research-2026-10-04.md) | Przygotowania offline; właściciel przekaże osobny klucz5€. Integrator0paidcalls. |
|8| `gpt/repo-hygiene-2026-10-04` / `fedaf6b` | [raport8](https://github.com/klb-t/chatadhd/blob/fedaf6b/docs/reports/repo-hygiene-2026-10-05.md) | NIEZGŁOSZONY gotowy: autorfedaf6b nadal pełna macierz w toku.16live źródeł identyczne9c47124, nowy sourcebound runner. Własny normalgate i Clang/vendored potrzebne; seeding/results nadal używane. |
|9| `gpt/integrator-queue-2026-10-05` | [raport9](integrator-intake-2026-10-05/README.md) | Odbiera całą kolejkę; każdy przyrost ma własne bramki i wpis indeksu. |
|10| `gpt/interface-2-2026-10-04` / `fa7538d` | [raport10](https://github.com/klb-t/chatadhd/blob/fa7538d/docs/reports/interface-2-2026-10-04.md) | fa7538d: autor zakończył pełny CTest108/108, web, fixture84/84, nativeUI20/20, E2E16/16. Gotowy do osobnego rebase i mixed gates9 po12. Clang captures naprawione. |
|11| `gpt/data-profiles-2026-10-04` / `22873e0` | [raport11](https://github.com/klb-t/chatadhd/blob/22873e047abe6b56808dd2fbc4fe41188ee0e7a8/docs/reports/data-profiles-2026-10-05.md) | GOTOWY pierwszy przyrost22873e0: poprzedni bloker naprawiony; wrapper wynika z canonical2 przez source recipe. Autor127/127,783native/26985assertions,1371Python0skip; własny rebase i mixed gates9 po dokumentachClaude. |
|12| `gpt/onboarding-2026-10-04` / `3c0bc36` | [raport12](https://github.com/klb-t/chatadhd/blob/3c0bc36552ef9851f1174946cfb108549aae3228/docs/reports/onboarding-2026-10-04.md) | PRZYJĘTY przyrost; źródło9 `1e63bcc`. CTest 121/121, native 761/29103, Python 1322/0skip; web PASS. 28źródeł autora3c0bc36 byte-identical po realnym rebase. Native onboarding4suites=51/2704 z pełnego JUnit; controller17/17, generatorcheckPASS. ActualW3registry bridge load/resolve/tamper/multiversionPASS;0providers/0method-execution, bez deklaracji podpiętego czatu. Profile/scenario/layers/store+UIcomponents przyjęte; HTTP/nawigacja/retencja dalsze10/3/11. Helperreporter Node22 jawnieTAP;pierwszy parsernegatyw w archive. |

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
|5|52|Pierwszy import/audit/checkpoint przyjęty; drugi preset/layers156a820 gotowy do własnego odbioru; dalsze registry/layers uzgodnić11/12.|
|6|31|Nowe strojenie tylkoDEV; archiwum/alias/sketch/score jako dane; jakośćdomyślna neutralna.|
|7|9|Offline plan etapów/kosztów; datowane twierdzenia o metodach. Klucz5€ dostarczywłaściciel.|
|8|1|Seeding dimensions/projection, bridge metod; pełna aktualna macierzCI, bez luzowania guard.|
|9|98|Koordynacja luk11native/core/CAPI/crypto,25Android,62retainedPython/tooling; nieprzejmować implementacji.|
|10|81|MostUI/HTTP/nawigacja12, what-app-knows, retencja; ustawienia2 i metody3/4 jako węzły.|
|11|213|Foundation/profile subsystemy; jedno źródłousage2, nie drugi preset; koordynacja1/3/12.|
|12|nowy|Onboarding foundationodebrać; dalszeconsumers/retencję/nawigację uzgodnić3/10/11.|

## Do wątku11 — dawny bloker zamknięty w22873e0

`loom/data/runtime/usage_policy.pack:6–12` kopiuje sześć wartości ręcznie. `loom/src/model/gen_runtime_profiles.py:16–34` czyta tylko runtime/*.pack;
`runtime_profile.cpp:249–265` ładuje tę niezależną tabelę; `test_runtime_bootstrap_profiles.cpp:60–68` porównuje kolejne literały.
Wyprowadzić wrapper11 z kanonicznego `loom/data/policy/usage_policy.pack`2, zachować walidację/nakładkę oraz shallowConfigoverride.
Dawny bloker powyżej naprawiony w22873e0: projection recipe korzysta z canonical2; generator/native provenance i source-edit tests obecne. Pełne autor127/127. Pozostaje własny rebase/mixedgate9. Integrator nie edytował implementacji11.

## Archiwum i ograniczenia dowodów

Przed selekcją pełne źródła zachowujemy na archive/2026-10-05/*-before-intake; oryginalne gałęzie i wszystkie historyczne negatywy pozostają dostępne.
Na main przenosimy wyłącznie wybrane implementacje/testy i pozytywne dowody. Lokalne pierwsze nieudane próby9 nie zostały wypchnięte przed automatycznym czyszczeniem; surowe logi utracono. Kompletny pozytywny ZIP90plików jest zachowany i niezależnie sprawdzony.
Nie zmieniono progów ani usunięto testów. Opt-in catalogscale jawnie0/0, pozostałe wymagane zestawy rzeczywiście wykonane.
Nie czytano ślepego korpusu ani eval/real-holdout-key;0nowych paidcalls.

## Do wątku9

Kontynuować całą kolejkę, aktualizować ten indeks po każdym fast-forward. Naprawiony11 ma własny odbiór po dokumentachClaude; niezgłoszony8 nie blokuje6.
Jeśli pełna bramka konkretnego źródła nie przejdzie, podać dokładny log/miejsce i zwrócić autorowi, zachować pełny negatyw.

## Do wątku N

Otwarte przekazania są przypisane w tabelach; status przyrostu nie jest zamknięciem wszystkich wymagań właściciela.

## Priorytet właściciela — 2026-10-05 16:50 Europe/Amsterdam

Kolejność:5→1→12→pierwszy10→ClaudeR42/budget(tylkodokumenty, rebase bez build)→11→8→6mechanizm→gotowe drugieprzyrosty. Pierwszy10fa7538d/nowy równoważny prefixda50562: gotowy; cała760ddc3 zawiera drugiprzyrost wstrzymany120/121, nie zastępuje pierwszego. Claude3cd5848e7484d50d00f19f0855bc2cd228ad7f3b: tylkoAGENTS+OWNER_REQUIREMENTS, gotowy clean stage.

Pełne obecne negatywy9 zachowane na archive/2026-10-05/integrator-runtime-negatives (`156a4bb`): buildartefacts, kompletny failed117gate i85plików rawsynthetic DB/WAL. SHA256syntheticZIP52a0f3c14856c7d4609c1b7629926fc3a7e1dee88abdb0ae7f3bde0b41e732a2. Workspace-corruption reproduces; identyczny/tmpcontrol24/24integrity PASS. Dokładny mechanizmnieustalony. Serialny retry pozostał niekompletny; nie sumowano go jako PASS. Nowy kompletny117/117 jest osobnym przebiegiem.

## Stan kolejki integratora — bieżący checkpoint

**Przyjęte:** 2:first, 3+4:first, 5:first, 1:partial, 12:first. Każdy kodowy przyrost ma własny pełny CTest, guard rzeczywiście wykonanych przypadków i build web; dokumenty Claude są wyjątkiem zleconym przez właściciela.

**Czeka w kolejce, z commitem gotowym do odbioru** (ready autora nie zastępuje bramek9):

| Przyrost | Przypięty commit | Pozostały odbiór |
|---|---|---|
|10:first|`fa7538d650da2f4ad37f5ff9254b60a6ee72938f`|równoważny prefix da50562; zachować packet routes|
|Claude:docs|`3cd5848e7484d50d00f19f0855bc2cd228ad7f3b`|R42 i AGENTS budget; tylko rebase|
|11:first|`22873e047abe6b56808dd2fbc4fe41188ee0e7a8`|canonical usage projection; rebase/full clean build/gates9|
|6:first|`d9b29c502bafd3b4cb17f187e3e74f32cfff72ee`|wyłącznie mechanizm; neutralne31/45,0FP; DEV, bez blind|
|5:second|`f494931dbf24c7debac588361d1d0098275bd47a`|source156a820;24paths; własne mixed gates9|
|4:second|`097859af292bf2b28915e4684f9c085efabd66e2`|autor117/117/parity1536; własny rebase/gates9|
|6:second|`dab76dfc5c2b9ac0cb71c434146fd71bd33bab65`|dane alias/index; własny rebase/gates9|

**Wstrzymane / niegotowe:** pełny1 (3 przypadki/8 asercji retry/cache), drugi10 (120/121, ten sam błąd);8 `57ad9bb20b5e21b4445d03abb7bcd2e616f63f2d` (pełna macierz nieukończona);3second `6b54926950752c6890eb69ec85bf1c4643ec346a` i12second `374bab7` (WIP, bez gotowego własnego fullgate).1second nie zawiera nowego gotowego przyrostu. Nie cofamy przyjętego INTERFEJS.0 płatnych wywołań9.

## Do wątku1 — regresja pełnego caller

Pełny50e6bb9 odrzucony: `unit.test_knowledge_semantic`13cases/125assertions:10PASS+3FAIL,8failedassertions; retry zwraca blocked zamiast failed i nie odtwarza cache/kandydatów. Naprawić tożsamość/lifecycle próby z W2 bez luzowania ledger/testów. [Pełny negatyw](https://github.com/klb-t/chatadhd/tree/archive/2026-10-05/precision-intake-negative/docs/reports/integrator-intake-2026-10-05). Wybrany20pathpartial nie zawiera semantic.cpp/semantic_usage.h. Rejestr43 nie jest dowodem podłączenia produkcyjnego caller.

## Do wątku N

9 kontynuuje pozostałe ready w podanej kolejności;1 naprawia retry,8 kończy macierz,3/12 kończą drugie przyrosty. Otwarte zadania11 pozostają przypisane w tabeli inwentarza, przyjęcie foundation ich nie zamyka.
