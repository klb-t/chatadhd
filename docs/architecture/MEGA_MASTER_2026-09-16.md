# ChatADHD / Loom — MEGA MASTER
**Stan roboczy: 2026-09-16**

> To jest scalony dokument architektoniczny, nie zamknięta specyfikacja. Jego zadaniem jest zachować opcjonalność, scalić historycznie rozdzielone pomysły i wyznaczyć wspólny model pojęciowy. Każda arbitralna decyzja pozostaje otwarta, dopóki nie wymusza jej kontrakt, test, koszt lub ograniczenie platformy.

## 0. Teza

Nie budujemy pięciu projektów: „ChatADHD”, „Loom”, „autonomiczny agent”, „analizator archiwów”, „multi-LLM knowledge graph chatbot”.

Budujemy **jeden system** z dwoma poziomami:

- **Loom** — reusable kernel / SDK / runtime: dane, graf, semantyka, provenance, pamięć/kontekst, modele/providerzy, task/action engine, storage/sync, policy, execution, UI-IR.
- **ChatADHD** — pierwszy pełny **workbench/reference application** Looma: chat + graf + archiwa + projekt + wykonanie + artefakty + panele.

Dawne „osobne projekty” stają się **capability packs, workflows, adapterami albo widokami**. Nie nadajemy teraz nowej nazwy całemu ekosystemowi — to decyzja kosmetyczna i nie jest potrzebna do implementacji.

---

## 1. Co faktycznie scala się bez konfliktu

### 1.1 ChatADHD
Daje gotowe wymagania dla:
- rozmów i branching history,
- pamięci,
- grafu wiedzy,
- analizy semantycznej,
- multi-model/provider routing,
- uniwersalnego importu,
- wyszukiwania,
- załączników,
- głosu,
- paneli,
- synchronizacji,
- szyfrowania,
- pracy wieloplatformowej.

### 1.2 Loom
Daje docelowy rdzeń:
- C++20,
- stabilną granicę C ABI,
- graph store,
- typed relations i topology,
- semantic analyzer,
- pattern mining / template induction,
- provider abstraction,
- storage abstraction,
- pipeline/orchestrator,
- rozszerzalność bez zależności od konkretnego GUI.

### 1.3 Autonomiczny Agent (2024→)
Nie powinien być osobną aplikacją. To **execution/orchestration layer** Looma:
- task → plan/action graph,
- browser / terminal / GUI / filesystem / API tools,
- checkpoint / retry / rollback / resume,
- local / VM / GCP / przyszłe compute,
- role/preset jako dane,
- wiele modeli w rolach,
- logi jako materiał do uczenia się z doświadczenia.

Wczesne, kilkugodzinne eksperymenty z prostą automatyzacją GUI nie są źródłem wymagań architektonicznych dla obecnego systemu. Zachowujemy je wyłącznie jako część surowej historii, bez wyciągania z nich komponentów, presetów ani invariantów.

### 1.4 Analizator archiwów / „the projects project”
To **Project Compiler / Archive Intelligence workflow**:
- ingest surowych eksportów,
- zachowanie oryginału,
- normalizacja,
- retrieval I tury,
- automatyczne rozszerzenie słownika i retrieval II tury,
- graf tematów/idei/decyzji/forków,
- chronologia przejścia po grafie,
- wykrywanie braków i sprzeczności,
- scalenie rozproszonych idei,
- materializacja specyfikacji / kodu / testów / backlogu.

To jest idealny pierwszy „self-hosting” use case: nowy system powinien umieć z własnych archiwów odtworzyć ten dokument.

### 1.5 Multi-LLM / KG chatbot
To nie osobny chatbot. To złożenie:
- model registry,
- provider registry,
- capability registry,
- policy/routing engine,
- evaluator/cross-check,
- graph/context engine,
- ChatADHD UI.

### 1.6 Multimedia / video / artifact workbench
To kolejny zestaw artifact types + pipelines:
- video,
- storyboard,
- timeline,
- images,
- audio/music,
- structured intermediate representation,
- renderer/compositor/generator adapters.
Powinien korzystać z tego samego Task/Artifact/Provenance/Provider/UI frameworku.

---

## 2. Konstytucja architektury

### A. Nie podejmujemy decyzji, jeśli nie musimy
Każda decyzja ma jedną z trzech klas:
1. **invariant** — musi być w kodzie/kontrakcie,
2. **policy/default** — dane, które można wymienić,
3. **accident/platform detail** — adapter.

Nie awansujemy 2 ani 3 do 1 bez dowodu.

### B. „Kod ≠ dane” — ale nie „wszystko YAML”
Dane nie mogą zastępować semantyki programu.

**W kodzie zostają:**
- kontrakty typów,
- invarianty,
- walidacja,
- algorytmy,
- mechanizmy bezpieczeństwa,
- execution semantics,
- transakcje,
- deterministyczne granice.

**Danymi są:**
- providerzy,
- modele,
- capabilities,
- metryki,
- presety,
- policy/routing,
- role agentów,
- relacje,
- reguły kompozycji,
- UI trees,
- layouty,
- workflow definitions,
- resource profiles,
- target/platform manifests,
- templates,
- źródła dokumentacji,
- feature flags.

### C. Serialization is not architecture
Nie wybieramy „YAML” jako fundamentu.

Definiujemy **typed declarative IR / schema registry**, a nad nim kodeki:
- YAML — wygodny human authoring,
- JSON/JSON5 — interop/debug,
- SQLite — queryable persistent registry,
- protobuf/CBOR/MessagePack — gdy pomiar pokaże sens binarnego transportu.

Zmiana serializacji nie może zmieniać semantyki.

### D. Capability-based, nie enum-based
Kod pyta:
`can(resource, capability, constraints)`  
a nie:
`if provider == X`.

Nowy provider/model/platforma ma wejść przez manifest + adapter. Nowa implementacja znanego kontraktu wymaga pluginu, ale nie przebudowy rdzenia.

### E. Unknown future ≠ „przewidzimy wszystko”
Nie da się uczciwie obiecać, że każda przyszła funkcja będzie wyłącznie edycją danych.

Rozróżnienie:
- nowa wartość w istniejącej semantyce → **data-only**,
- nowy backend znanego kontraktu → **plugin/adapter code**,
- nowa semantyka, której kontrakt dziś nie opisuje → **schema/core evolution**.

Celem jest zminimalizowanie trzeciej kategorii, nie udawanie że zniknie.

### F. Raw source is immutable
Każdy import zachowuje:
- source bytes/hash,
- source ID,
- timestamp,
- parser/version,
- transformation chain,
- confidence,
- derived objects.

Derived state można przebudować; źródła nie.

### G. Wszystko istotne jest resumable i auditable
Task, ingest, analysis, sync, model call, agent action:
- mają ID,
- status,
- input hash,
- output hash,
- provenance,
- checkpoint,
- retries,
- log/event stream.

### H. System uczy się, ale nie „magicznie sam się przepisuje”
Uczenie z doświadczenia ma tor:
`events/logs → outcomes → mined pattern → candidate rule/template/policy/test → evaluation → promotion`

Zmiana executable core nadal przechodzi normalny, wersjonowany workflow kodu i testów.

---

## 3. Uniwersalny model obiektów

Minimalny wspólny vocabularz:

- **Artifact** — dowolny byt: message, file, code, image, video, dataset, report, UI tree, model response.
- **Node / Edge** — semantyczne byty i relacje.
- **SourceRef / Provenance** — skąd coś pochodzi i przez jakie transformacje przeszło.
- **Event** — append-only fakt, że coś zaszło.
- **Task** — intencja/rezultat do osiągnięcia.
- **Action** — pojedynczy wykonawczy krok.
- **Plan** — graf zadań/akcji, nie koniecznie sekwencja.
- **Checkpoint** — stan pozwalający wznowić/odgałęzić wykonanie.
- **Capability** — operacja, którą może zaoferować resource/provider/model/tool.
- **Resource** — model, API, komputer, GPU, VM, browser, datastore, renderer, tool.
- **Provider** — sposób uzyskania resource/capability.
- **Metric** — wielowymiarowy pomiar jakości/kosztu/szybkości/ryzyka.
- **Policy** — reguła wyboru/routingu/zgody.
- **Template** — kandydat struktury wielokrotnego użycia.
- **View** — deklaratywny opis UI.
- **ContextSet** — dobrany zestaw źródeł dla konkretnego tasku/model call.
- **Project** — nie katalog, tylko subgraf + polityki + aktywne views/workflows.

Typy rozszerzalne przez registry/schema; hot paths mogą mieć typed tables/structs.

---

## 4. Warstwy mega-systemu

### 4.1 Ingest + Normalization
Adaptery:
- ChatGPT exports,
- Claude exports + projects + memories,
- GitHub,
- filesystem,
- SQLite,
- JSON/JSONL,
- HTML/MHT,
- Markdown/text,
- screenshot/image,
- audio/video,
- przyszłe konektory.

Pipeline:
`Source → parser → RawArtifact → NormalizedArtifact → semantic jobs`

### 4.2 Provenance / Event / Blob layer
- content-addressed blobs,
- append-only event stream,
- hashes i chain-of-custody,
- transformation records,
- dedup,
- source snapshots.

### 4.3 Graph + Semantic layer
- entity/topic/concept/code/file/message/project nodes,
- typed relation registry jako dane,
- entity resolve/merge,
- topology rules,
- traversal,
- embeddings/index,
- pattern mining,
- template induction,
- contradiction/supersession/alternative relations.

### 4.4 Context / Memory engine
Nie „wstrzykujemy pamięci do promptu”, tylko budujemy `ContextSet` pod konkretne zadanie.

Selekcja może uwzględniać:
- relevance,
- graph distance,
- chronology,
- authority/provenance,
- confidence,
- freshness,
- project membership,
- token/latency/cost budget,
- completeness dial,
- diversity / redundancy,
- explicit pins/blacklists.

„Kompletność” jest parametrem ciągłym/policy, a nie 4 kategoriami mocy.

### 4.5 Provider / Model / Compute fabric
Ten sam mechanizm registry dla:
- LLM API,
- embeddings,
- ASR,
- OCR,
- image/video/audio generation,
- local models,
- on-device models,
- Ollama/LM Studio,
- własnej chmury,
- VM/GCP,
- przyszłych providerów.

Manifest zawiera capabilities i źródła danych o:
- cenie,
- latency/throughput,
- context limits,
- task-specific quality metrics,
- hardware requirements,
- privacy/residency,
- tool support,
- freshness timestamp,
- provenance metryk.

### 4.6 Policy / Routing engine
Input:
`task requirements + user constraints + resource manifests + live metrics`

Output:
`candidate plan / selected resource(s)`

Możliwy explicit override usera zawsze wygrywa, o ile nie łamie twardego safety/invariant contract.

### 4.7 Agent runtime / Execution engine
Historyczny Agent staje się tu:
- planner optional,
- action decomposer,
- tool adapters,
- terminal/browser/GUI/filesystem/API,
- execution sandbox,
- checkpoint/rollback/retry,
- state verification po akcji,
- human approval gates jako policy,
- multi-agent orchestration tylko wtedy, gdy daje zysk.

„junior_dev/senior_dev/researcher” = role/policy bundles, nie klasy zaszyte w kodzie.

### 4.8 Environment / Deployment fabric
Jedna abstrakcja `IExecutionEnvironment`:
- current process,
- local sandbox,
- container,
- VM,
- remote host,
- GCP/inna chmura.

„Samoreplikacja” historycznego agenta formalizuje się jako:
`EnvironmentManifest + bootstrap + state transfer + verification`
— czyli reprodukowalny deployment, nie specjalny mechanizm.

### 4.9 Project Compiler / Archive Intelligence
Kluczowy workflow:

1. ingest wszystkiego bez ufania tytułom,
2. lekki lexical pass,
3. wyciągnięcie terminów/encji z trafień,
4. drugi pass rozszerzonym słownikiem,
5. semantic/embedding retrieval,
6. graph clustering,
7. timeline + forks,
8. extraction:
   - idea,
   - decision,
   - rejected option,
   - unresolved question,
   - implementation,
   - bug,
   - requirement,
   - invariant,
   - rationale,
9. contradiction/supersession resolution bez kasowania historii,
10. synthesis z linkami do źródeł,
11. materialization:
   - master spec,
   - implementation plan,
   - manifests,
   - tests,
   - code skeleton,
12. kolejny przebieg po nowych terminach wykrytych podczas syntezy.

### 4.10 Learning / Experience distillation
Z 2024 agenta zostaje bardzo dobry pomysł:
pełne logi z debugowania są materiałem badawczym.

Engine powinien tworzyć:
- `Experience` records,
- problem signature,
- environment,
- attempted actions,
- result,
- root cause candidate,
- successful remedy,
- edge cases,
- confidence.

Powtarzalne wzorce → candidate Template/Policy/Test.

### 4.11 UI / Panel engine
UI ma własny IR:
`ViewNode { type, props, bindings, children, actions, constraints }`

Panele:
- chat,
- conversation list,
- knowledge graph,
- active context,
- project structure,
- terminal/execution,
- logs,
- artifacts,
- video/timeline/storyboard,
- model/provider inspector,
- memory/context inspector.

Renderer jest adapterem. Compose Multiplatform może być pierwszym, nie ontologią systemu.

### 4.12 Storage / Sync / Identity
Kontrakty:
- `IDataStore`
- `IBlobStore`
- `IEventStore`
- `ISecretStore`
- `IKeyProvider`
- `ISyncTransport`
- `IIdentityProvider`

Backends mogą obejmować:
- local SQLite/files,
- encrypted local,
- cloud,
- zero-knowledge server,
- Google Drive,
- hybrid.

### 4.13 Artifact engine
Jedna rama dla:
- tekstu,
- kodu,
- raportu,
- wykresu,
- audio,
- obrazu,
- wideo,
- prezentacji,
- danych.

Artifact może mieć:
- źródła,
- wersje/branch,
- structured IR,
- renderer/exporter,
- generation pipeline,
- validation rules.

---

## 5. Jak dawne projekty wpadają do wspólnej struktury

| Dawna nazwa | W mega-systemie |
|---|---|
| ChatADHD | reference workbench / UX |
| Loom SDK | kernel/runtime |
| autonomous Agent | Task/Action/Execution + policy + tools |
| archive analyzer | Archive Intelligence workflow |
| the projects project | Project Compiler + provenance/fork timeline |
| MultiLLM/KG chatbot | Provider/Model + Graph/Context + UI composition |
| GCP VM manager | ExecutionEnvironment / ComputeProvider adapters |
| video combine / scene tools | Artifact + media pipeline pack |
| WatchDog | domain application/plugin/preset on common substrate |
| pitching agent / avatar | presentation/agent UI pack, later |
| LEM | może dostarczyć reprezentację/metadata do context/semantic layer, ale nie jest zależnością rdzenia |

---

## 6. YAML? Odpowiedź architektoniczna

**Tak jako jeden z formatów authoringu; nie jako „prawda systemu”.**

Przykładowy przepływ:

`YAML/JSON/TOML/DB/API → parser → typed ConfigIR → validation → resolved runtime objects`

To pozwala później:
- zmienić format bez migracji logiki,
- generować config z UI,
- przechowywać registry w SQLite,
- przesyłać to samo przez sieć w protobuf/CBOR,
- wersjonować schema osobno od serializacji.

---

## 7. Najważniejszy test architektury

Nie „czy umie czatować”.

**Self-hosting test:**
wrzucamy eksport ChatGPT + Claude + projects + GitHub, a system ma sam:
1. odnaleźć ChatADHD/Loom/Agent/Archive Analyzer mimo mylących tytułów,
2. przeprowadzić multi-pass retrieval,
3. zbudować graf,
4. wykryć historyczne forki i superseded decisions,
5. stworzyć source map,
6. wygenerować ten Mega Master,
7. wskazać różnice między specyfikacją a kodem GitHub,
8. zaproponować patch plan,
9. po zatwierdzeniu uruchomić coding agent,
10. sprawdzić wynik i dopisać nowe doświadczenie do wiedzy.

To jednocześnie testuje większość wspólnych struktur bez budowania sztucznego demo.

---

## 8. Implementacja równoległa bez „big-bang”

Można uwzględnić wszystkie pomysły jednocześnie **w modelu i kontraktach**, ale nie warto pisać wszystkich konkretnych backendów równocześnie.

Najpierw pionowy szkielet:
1. Canonical IDs + Artifact/Source/Provenance/Event.
2. Ingest Claude + ChatGPT + plain files.
3. GraphStore + relation registry.
4. Job/Task engine + resumability.
5. Provider/Model registry + jeden realny provider.
6. Context engine.
7. Archive Intelligence pass 1→2.
8. CLI/C ABI.
9. minimalny ChatADHD workbench.
10. dopiero potem kolejne adaptery — niezależnie i równolegle.

W ten sposób „wszystkie opcje” istnieją jako extension points, ale nie produkujemy setek martwych abstrakcji bez testu użycia.

---

## 9. Zasady ewolucji

Każda nowa funkcja odpowiada najpierw na pytania:
1. Czy to nowa wartość danych?
2. Czy to nowa implementacja istniejącego kontraktu?
3. Czy naprawdę wymaga nowej semantyki?

Jeśli odpowiedź 3 pada zbyt często, architektura jest zbyt sztywna.

Każdy refactor powinien zmniejszać:
- wiedzę zaszytą w conditionalach,
- coupling do providerów/platform,
- duplicate schemas,
- implicit state,
- nieaudytowalne transformacje.

A zwiększać:
- provenance,
- replayability,
- composability,
- introspection,
- testability,
- runtime substitutability.

---

## 10. Otwarte decyzje — celowo NIE zamknięte

- JSON vs YAML vs protobuf jako external representation.
- SQLite vs inny backend dla niektórych storage paths.
- Compose MP vs alternatywne rendery poza pierwszym klientem.
- monorepo vs kilka repozytoriów.
- event sourcing pełny vs hybrydowy.
- plugin ABI poza C ABI.
- dokładna ontologia nodes/edges.
- scheduler/orchestrator implementation.
- local vs remote server topology.
- platform build order.
- jaki provider/model jest defaultem.
- czy i kiedy Web jest klientem first-class.

Zamykamy je dopiero przez constraint albo benchmark.

---

## 11. Rzeczy, których nie wolno zgubić z historycznego Agenta

- centralna pamięć/system knowledge, nie pamięć jednej instancji,
- wiedza destylowana z pełnych logów debugowania,
- checkpointy z możliwością ponownego złożenia ścieżki,
- GUI action → screenshot/state → success evaluation,
- role modeli dobrane do zadania,
- local ↔ remote/GCP portability,
- preset = informacja, nie kod,
- pełne logowanie z różnymi widokami poziomu szczegółowości,
- retry/rollback/failure artifacts,
- auto-detection środowiska,
- natural-language high-level tasks nad deterministycznym action engine.

---

## 12. Rzeczy, których nie wolno zgubić z ChatADHD

- edit → branch, stara wersja nie ginie,
- message/version provenance,
- graph budowany na bieżąco,
- pamięć/kontekst jako osobny byt,
- universal import,
- provider/model registry,
- załączniki + dedup,
- voice/ASR,
- panel system,
- debug/log panel,
- multi-platform,
- zero-knowledge storage/sync option,
- brak telemetrii jako default,
- local-first / graceful degradation.

---

## 13. Rzeczy, których nie wolno zgubić z „projects project”

- tytuły rozmów są słabym sygnałem,
- raw source zostaje,
- każda idea/fork/decyzja ma chronologię,
- porzucone ≠ nieważne,
- system ma wskazywać brakujące artefakty,
- kod też jest źródłem wiedzy i należy z niego wydobywać wartościowe fragmenty,
- graf ma pokazywać nie tylko stan końcowy, ale drogę po stanach,
- kolejne przebiegi wyszukiwania mają wynikać z terminów znalezionych w poprzednich.

---

## 14. Bieżący stan GitHub — krytyczna obserwacja

Widoczny zdalny `chatadhd` jest nadal linią Python/Kivy v0.07.x, z katalogami `core/`, `engine/`, `gui/` i Pythonowym `main.py`.
Repo `loom` istnieje, ale ma rozmiar 0.

Wniosek: opisany w dokumentach bootstrap C++/Loom i ostatnia praca Claude Code nie są obecnie odzwierciedlone w widocznym zdalnym repo. Najbardziej prawdopodobne wyjaśnienie: praca jest lokalna / w paczce / jeszcze niepushnięta. Nie należy traktować GitHuba jako aktualnego single source of truth, dopóki ten stan nie zostanie zsynchronizowany.

---

## 15. Mining wykonany w tej iteracji

Na eksporcie Claude:
- nie używano tytułów jako głównego filtra,
- pass 1: rdzeniowe terminy i synonimy,
- z wyników wyprowadzono pass 2: m.in. checkpoint/rollback, central memory, auto-migration/self-deploy, GUI control, project mining, context retriever, provider discovery, pattern/topology/materialization, panel/artifact/media, compute fabric i learning from logs; krótkie wczesne eksperymenty GUI nie są traktowane jako wymagania,
- wynik został zapisany jako osobna mapa źródeł CSV.

Najważniejsze odnalezione przodki:
- **Agent base** (2024-11) — VM/GCP, computer use, dwa LLM-y, długoterminowa/centralna pamięć, checkpointy, self-deployment.
- **Comprehensive Mind Map of AI Agent System and Chat Setup** — role modeli, central knowledge, full logs → knowledge, multi-agent, full-duplex voice/avatar later.
- **the projects project** — raw archive ingest, graf + chronologia + forki + brakujące pliki.
- **Automating chatbot conversation analysis** — raport tematów/idei, aktualizowany model struktur, merge/split tematów, wspólny semantic analyzer.
- **Fundamentalne zasady kodowania aplikacji** — abstrakcje, presets-as-data, capability docs refresh, task-specific model metrics, Memory Retriever completeness.
- **Migracja ADHD chata na Android Studio C++** — C++ core, semantic analyzer i graph engine jako biblioteki, abstrakcje model/ASR/OCR/storage.
- **Materialization and graph store optimization** — Loom jako podstawa pełnego ChatADHD.
- **Watchdog automated coding agents** — UI/config as data i minimal-human coding workflow.
- **Agent piczujący** — ponowne scalenie universal agent + archive semantic graph + media/presentation modules.

---

## 16. Następny sensowny invariant implementation target

Nie wybieramy jeszcze pełnego roadmapu produktu. Wybieramy test, który wymusza dobre abstrakcje:

**Archive-to-Project self-hosting vertical slice**

Input:
- obecne eksporty ChatGPT/Claude,
- project exports,
- istniejący GitHub.

Output:
- canonical source store,
- graph,
- source map,
- decision/fork timeline,
- Mega Master,
- gap report GitHub ↔ spec,
- generated machine-readable project manifest,
- resumable task log.

Jeśli ten vertical slice da się rozszerzyć potem do czatu, agenta, WatchDoga i media pipeline przez dołożenie adapterów/policy/data, filozofia architektury działa. Jeśli trzeba przepisywać rdzeń — mamy test, gdzie abstrakcja była za wąska.

---

## 17. Jedno ostrzeżenie

„Zaimplementujmy wszystkie znalezione pomysły jednocześnie” jest dobre jako **wymóg kompatybilności architektonicznej**, ale złe jako strategia pisania całego kodu.

Inaczej powstaje *speculative generality*: setki interface'ów bez realnego kontraktu i brak end-to-end systemu.

Lepsze znaczenie „jednocześnie”:
- każdy pomysł od razu trafia do grafu wymagań,
- wszystkie wspólne byty są modelowane od początku,
- kontrakty nie zamykają przyszłych adapterów,
- konkretne implementacje dochodzą przez pionowe testy,
- po każdym pionowym teście abstrakcje są poprawiane na podstawie dowodu.

To jest spójne z zasadą: **decyzji nie podejmujemy, jeśli nie musimy**.
