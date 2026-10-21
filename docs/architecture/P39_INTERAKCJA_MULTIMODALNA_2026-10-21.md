# P39 — Interakcja multimodalna: trajektoria rozmowy, pełny dupleks, głos i twarz (2026-10-21)

Specyfikacja końcowa po trzech projektach (A: trajektoria tekstowa, B: kontroler dupleksu + symulator, C: ekspresja/głos/awatar) i po panelu sędziów (33 problemy). Stan odniesienia: worktree `claude/p39-design` na `9de65cba` (main `0556d726`, 2026-10-21 00:18). Podrzędne wobec `HANDOFF_MULTIMODAL_DUPLEX_JEV_2026-10-21.md`, `LOOM_CONCEPTUAL_MODEL.md` (I1–I9), `GRAF_HUB_KOD_SILNIK_2026-10-13.md`, `M1_USTAWIENIA_JAKO_GRAF_2026-10-16.md` (§2.6 jedna powierzchnia API, §3.2 nawigator). Luki: `docs/reports/LUKI_2026-10-16.md` MM01–MM10, VO01, AV01. Zasady, które obowiązują w każdym zdaniu niżej: Twoje listy to przykłady do wypełnienia (mówię, co dodałem — §11); wszystko konfigurowalne, presety nie reguły; żaden próg „3 zdarzeń”, żadna zamknięta lista typów; nic nie idzie do modelu bez Twojego włączenia; koszt zawsze pokazany (zero = „0”, nieznany = „nieznany” z powodem); Twoje prawdziwe dane: wolno ich używać lokalnie (D3/DIP26), nigdy w repozytorium, testach i logach, nigdy do nowego dostawcy bez Twojego tak/nie; postęp mierzy się tym, co możesz dotknąć na telefonie; nic nie jest „zrobione” bez testu.

## 0. Twoje słowa (dosłownie, najpierw)

2026-10-21 (`SLOWA_WLASCICIELA_2026-10-16.md`, Uzupełnienie 4):

> GPT nie pracuje nad projektem teraz na ciebie liczę, że to wszystko co zebrałeś przełożysz na kod, bo zainstalowałem to APK No i tyle dobrze że cokolwiek sensownego robi, ale nie te 15%, o których mówiłem, tylko bliżej trzech – czterech. nie bierz bezwzględnych liczb za bardzo serio pokazuje tylko orientacyjne relacje między nimi. lecisz Ultra, wykorzystaj fabla gdzie trzeba bo trochę trudnych rzeczy jest kropka wrzucam ci jeszcze co mi się przypomniało, co któryś z Was agentów ponoć zacytował zbierając materiały, ale olał. zobacz swoją dokumentację, czy masz na przykład wideo rozmowę z avatarem 3D asystenta - tego nie było w materiale od GPT który ci dorzucę. wyciągach na ten temat pamiętam że zwracałeś szczególną uwagę na pomysł ominięcia doliny niesamowitości poprzez złamanie hiperrealizmu artefaktami

2026-10-16 (S1): „Weź mi daj dopiero jak będzie wszystko zrobione, bo co nie sprawdzam to nie ma.” · „to listy które podaję […] to są tylko przykłady na których masz się wzorować”.

O głosie (Gemini, 2026-10-15, `GEMINI_GLOS_WYPOWIEDZI_I_WYMAGANIA_2026-10-15.md`): „darmowe frameworki do analizy mowy, które poza oczywiście tekstem dają powiedzmy z kilkadziesiąt co najmniej parametrów funkcji czasu” · „wprost algorytmiczne, a nie uczenie maszynowe […] cechy, które yyy już człowiek będzie w stanie zinterpretować, na przykład znając kontekst sytuacji” · „najbardziej widoczne by było zdziwienie. Strach to jako rodzaj stresu. Co jeszcze w głosie słychać? Właśnie niepewność.” · „rytmikę zmienną w czasie i właśnie oddech […] dane będące połączeniem tych surowych czasów plus właśnie tego co tekst wymusza” · „ta hiperkontrola uwzględnia również niedoskonałości, które są zaplanowane”. Wypowiedź 11 (badany, któremu mówi się, że kłamstwo zostanie wykryte, ma stres niezwiązany z kłamstwem) to Twój własny argument przeciw „wykrywaczowi kłamstw” — tak go czytam.

Przekazanie GPT (`HANDOFF_…_2026-10-21.md`) to nie Twoje słowa, tylko hipotezy i wymagania z innej rozmowy; jego dziesięć punktów (TL;DR 1–10) i pakiet P0/P1/P2/P3 traktuję jak listę do wypełnienia, nie jak zakres. Kontrola dokumentacji (2026-10-21): w repozytorium i na 191 gałęziach nie ma rozmowy wideo z awatarem 3D ani pomysłu z artefaktami; jest tylko „pitching agent / avatar — later” (`MEGA_MASTER_2026-09-16.md:464`, `loom/data/profiles/self.json:385-389`) i „full-duplex voice/avatar later” (`MEGA_MASTER:639`). Źródło leży w Twoich eksportach — wyciągam je lokalnie narzędziem X-Port (pozycja E-A1), zanim cokolwiek o artefaktach ustalę.

## 1. Co jest dziś (z weryfikacji, plik:linia)

| Punkt przekazania | Stan | Gdzie | Co wziąć, czego brak |
|---|---|---|---|
| 1. Stan wielu uczestników (uwaga, akceptacja kierunku, pilność…) per aktor/cel/perspektywa/czas | częściowo | `loom/include/loom/model.h:372-540` (Entity z otwartym `kind`, Claim z otwartym predykatem, `Qualifiers.valid_from/valid_to/scope/extra`, `Assessment` z derywacją, pewnością, przesłankami, kontrą, konsekwencjami, pytaniami otwartymi, alternatywami); `loom/data/graph/classes.pack:708-750` (party → person/group/organization/collective/ai_agent, otwarte); `graph_views.pack:1489` rodzaj `goal`; `guardian.pack:1210-1225` (węzły hipotez, `supports`/`contradicts`) | Jeden Claim niesie wszystko: podmiot = uczestnik, predykat = `attends_to`, obiekt = cel, `valid_from/to` = okno, `scope` = perspektywa, `derivation` = metoda, `confidence` = niepewność, `support[]` = obserwacje. Brak: rozmowa zna tylko role user/assistant, zero takich twierdzeń, nic na telefonie |
| 2. Kontroler pełnego dupleksu | nie | `chat_engine.h:48-49` (anulowanie zapisuje fragment z `metadata.cancelled`; test `tests/test_chat_engine.cpp:377-389`); `web/src/api/operations.ts:36,59` („Live microphone and realtime voice are unavailable”); `ports.pack:40` rola `speech.synthesize` bez adaptera; `client_devices.h` (klient deklaruje mikrofon/głośnik ze zgodą) | Wziąć: `CancelToken` + strumień jako kanał `generation`; deklaracje urządzeń; role portów. Brak: TTS, odtwarzanie PCM, VAD, pozycja odtwarzania, pętla audio; mikrofon = nagraj-i-wyślij |
| 3. Plan interwencji razem z odpowiedzią | częściowo | `src/packet/packet.h` (`compile_reply`, `reply_fragment`), `chat_engine.h:128-131` `ChatOptions.flow`, `annotation_layer.h:58-69` (twierdzenia per fragment tekstu) | Wziąć: odpowiedź grafowa + adnotacje per fragment. Brak: priorytet segmentu, przewidziane przerwania, zależności, warunki otwarte |
| 4. Tani estymator (JEV) jako kandydat + lokalne DSP/VAP | częściowo | R38 (`OWNER_REQUIREMENTS:453-465`: Jev Lab **nie w repozytorium**); `annotation_layer.h:143` `resolve_model` (polityki modeli jako dane); `src/packet/METHOD_GRAPH.md` (datowane oceny jako osobne twierdzenia) | Wziąć: rola portów + polityka modelu + METHOD_GRAPH. Brak: kod estymatora tury, DSP, porównanie |
| 5. Planowanie warunkowe (ustąp, zachowaj, oceń ponownie, ewentualnie porzuć) | częściowo | `tasks.h:1-18` (pending/running/paused, checkpoint, `recover_interrupted` → **pending**), `action_run.h:205-225`, `model.h:967-981` (Decision active/superseded/reverted) | Wziąć: zadanie `paused` jako trwałe zobowiązanie, Decision + Claim z `valid_to` i `open`. Brak: zobowiązanie z wyzwalaczem i warunkami, ponowna ocena przed wykonaniem, testy |
| 6. Metadiagnostyka od pierwszego zdarzenia | częściowo | `guardian.h`, `src/guardian/*.cpp`, `guardian.pack:94-113` (progi ask/propose/apply = **dane**), `model.h:1034-1050` Prediction | Wziąć: węzły hipotez i krawędzie. Brak: diagnoza decyzji (nie tylko poprawek czatu), księgowanie kontrfaktyczne; progi strażnika **nie mogą** bramkować diagnozy (§3.1.6) |
| 7. Mimika jako projekcja stanu | nie | `view_engine.h` (renderery jako dane: `graph_views.pack:4195` `view_renderers`), `web/src/view-engine/scene3d.ts`, `ui_animation.pack`, `actions.pack` `media.generate_*` | Wziąć: renderer twarzy = kolejny wpis `view_renderers` nad tym samym modelem widoku. Brak: twarz, blendshape, lipsync, polityka ekspresji |
| 8. Graf z potencjałem rekonstrukcji | jest | `LOOM_CONCEPTUAL_MODEL.md:369-399` (I1/I5/I7), `model.h:299-356` (Locator ze źródłem sha256, wskaźnikiem JSON, zakresem bajtów, czasem; Observation), METHOD_GRAPH (hashe definicji, ślad przebiegu) | Używać wprost. Brak: polityka przechowywania surowego audio (dodana jako ustawienie w E-V1) |
| 9. Bez granic ontologicznych (aktor = rola relacyjna) | częściowo | `classes.pack` party (otwarte), R44/R46 (`OWNER_REQUIREMENTS:565-599`), P29 węzły wyłonione, `action_rules.h:83-93` (zgoda/uprawnienia = osobne bramy) | Wziąć: Entity z otwartym kind + twierdzenia; zgoda nigdy z obserwacji. Brak: konwencja `exists_as` (fikcyjny/hipotetyczny/sprzeczny) — tu jako wartość słownika w danych, nie typ |
| 10. Semantyka niezależna od skali | częściowo | `perspectives.h`/`view_engine.h` (budżet w żądaniu, `0|null` = brak), `money_format.h`, int64 w Locatorze | Wziąć. Brak: testy 10^5 zdarzeń, 10^3 aktorów, id > 2^53 (T-T1) |
| Głos jako funkcje czasu (VO01) | nie | `src/media/media_providers.cpp:155-165` (Groq: start/end segmentu), `:222-230` (Google: **gubi czasy słów**, choć ustawienie `word_time_offsets` istnieje `:189,210`) | Brak narzędzia DSP; synchronizacja słów wymaga zmiany w `media_providers.cpp` (E-V2) |
| Awatar 3D, artefakty (AV01) | nie | tylko wzmianki (§0) | Źródło w Twoich eksportach — E-A1 |

## 2. Cel jako jeden model

Jedno zdanie: **każda interakcja — tekstowa dziś, głosowa i wideo jutro, symulowana w laboratorium — jest jednym strumieniem zdarzeń `loom.dialogue_events/1`, z którego powstaje jeden pakiet grafu (obserwacje → twierdzenia o stanie → kandydaci działań → decyzje z alternatywami → zobowiązania warunkowe → diagnoza), a ekran, głos i twarz są tylko rendererami tego samego stanu.**

```
producenci strumienia                jeden pakiet grafu (GraphPacket, CAS, replay)           renderery / wykonawcy
──────────────────────               ───────────────────────────────────────────────          ─────────────────────
rozmowa tekstowa (K1, K3)  ─┐        Observation (zdarzenie, pozycje, czas int64+skala)        nawigator M1: oś czasu, karty
symulator dupleksu (D-K1)  ─┼─▶      Claim: stan uczestnika / cel / zobowiązanie     ─▶        pasek „Odłożone” nad polem pisania
pętla audio (D-W1)         ─┤        Decision + alternatywy z Q per wymiar×horyzont            propozycja w rozmowie („Dokończyć?”)
narzędzie głosu (E-V1)     ─┘        Prediction, hipotezy diagnozy, oceny datowane             twarz/awatar (E-F2), głos (D-W1), scena (E-S1)
                                      METHOD_GRAPH: metoda, wersja, parametry, przebieg
```

### 2.1 Strumień `loom.dialogue_events/1` (jedyny format zdarzeń, dla wszystkich torów)

```
{schema: "loom.dialogue_events/1", stream_id: "ds_<hex12>",
 segment: {index: int64, prev_sha256: string|null, closed: bool},          // łańcuch hashy segmentów (z projektu B), segment zamknięty = niezmienny
 time_base: {unit: "ms"|"us"|"ns"|"samples", rate?: int64, epoch: "unix"|"stream"},
 actors: [{ref: "party:self" | "model:<id>" | "<adres grafu>" | "<nowy byt>", label?, class?}],
 events: [{i: int64, t: int64, t_end?: int64, actor, kind: string, text?, ref?: "msg:<id>",
           positions?: {plan, generated, synthesized, played, heard_estimated},   // każda: {chars|tokens|t|samples: int64} albo {unknown: <powód>}
           channels?: {generation, synthesis, playback, capture: {version: int64, position: int64}},   // z projektu B, tylko gdy kanał istnieje
           attrs: {...}}]}
```

- **Czas = `int64` + zadeklarowana skala**, jeden zapis dla wszystkich torów (sędziowie: trzy kodowania → jedno). Narzędzie głosu używa `samples` + `rate`; symulator `us` + `stream`; rozmowa `us` + `unix`. W pakiecie: `Observation.attrs.time {t, t_end, unit, rate, epoch}`; `Locator.time_start/time_end` (double, sekundy) tylko do wyświetlania — znana strata, zapisana.
- **`kind` jest otwarty** (słownik `trajectory.pack events.kinds` ze znaczeniami pl/en); nieznany rodzaj zostaje z `emergent: true` (R44/P29). Identyfikatory to `kb::stable_id` (`kb.h:317`) od treści.
- **Pozycje** rozróżniają plan ≠ wygenerowane ≠ zsyntezowane ≠ odtworzone ≠ usłyszane (hipoteza). W trybie tekstowym: `generated` = znaki wygenerowane, `played` = znaki wyświetlone **gdy klient je zgłosił** (`displayed_chars` przy anulowaniu), inaczej `{unknown: no_client_report}`; `heard_estimated` = wniosek operatora tempa czytania (T-K5), do tego czasu `{unknown: no_estimator}`.
- **Tryb na żywo**: tylko **zamknięte** segmenty mają hash i pakiet; ogon na żywo jest segmentem otwartym (bez pakietu), zamykanym po `packet.segment_events` zdarzeniach (preset 2000; 0 = tylko ręcznie) albo akcją. Dzięki temu identyfikatory pakietów nie dryfują (sędziowie, problem 11).

### 2.2 Mapowanie na istniejący model Loom (P0.1; bez nowej zamkniętej kategorii)

| Pojęcie | Nośnik (plik:linia) | Jak dokładnie; poprawki po sędziach |
|---|---|---|
| Actor | `model::Entity` z otwartym `kind` (`model.h:372`); klasy `party → …` (`classes.pack:708-750`); **rola = relacja**: Claim `participates_as` aktor → trajektoria, `qualifiers.scope` = perspektywa | istnienie / tożsamość / zgoda = **osobne twierdzenia** (`exists_as: fictional|hypothetical|contradictory|unknown` to wartość ze słownika danych); uprawnienia tylko z `action_rules.pack` (R43), nigdy z obserwacji |
| Goal | Entity `goal` + Claim `pursues`, `goal_validity` z `valid_from/valid_to`; porzucenie = `ClaimStatus::Superseded` + Decision | nic nie jest kasowane |
| InteractionEvent | **Observation** (`model.h:334`) z `kind = ObservationKind::Utterance` (zbiór zamknięty, `model.h:141` — **nie rozszerzam enumu**); otwarty rodzaj zdarzenia w `attrs.event_kind`, indeks w `attrs.event_i` (int64), czas w `attrs.time`; lokator `sha256:<segment>` + `json_pointer=/events/<n>` | sędziowie (3): `Observation.kind` jest zamknięty — dlatego rodzaj zdarzenia siedzi w `attrs`, co jest danymi, a enum zostaje; sędziowie (5): `Observation.ordinal` zostaje `int` — `n` liczy się w obrębie segmentu (≤ `segment_events`), więc nigdy nie zbliża się do 2^31; globalny indeks `i` jest w `attrs.event_i` |
| Decision / Alternative | `model::Decision` + `DecisionAlternative` (`model.h:954-980`) | sędziowie (4): `ValueImpact` to {zasada, efekt, uzasadnienie} **bez liczb** — wartości Q per (wymiar × horyzont) idą do `DecisionAlternative.value` jako JSON `{q: {<wymiar>: {<horyzont>: {value_bp: int (0–10000), basis: rule|heuristic|estimator, calibrated: false}}}, policy: {id, version, definition_sha256}, mode: live|shadow, latency_us, cost: {money, calls}}`; `effects[]` nadal mapuje zasady (`philosophy/principles.json`). Liczby w **punktach bazowych (int)** — bitowo stabilne między x86 i arm64 (ryzyko H4) |
| Prediction | `model::Prediction` (`model.h:1034`): przewidziane przerwanie = sytuacja (segment), `outcome` po kolejnych zdarzeniach; **kontrfaktyczność**: gdy interwencja zmieniła warunki → `NotApplicable` z powodem, nigdy „pudło” | |
| DeferredIntention | Claim `intends_to_deliver` (podmiot = agent, obiekt = `content_segment`) z `qualifiers.valid_to`, `qualifiers.extra {trigger, conditions[], reassess_before_execute: true, decision_id}`, `assessment.open.questions` = warunki; **nośnik trwały**: zadanie `loom_tasks` rodzaju `trajectory.deferred` w stanie `paused` | sędziowie (6): `recover_interrupted` przenosi `running → pending` (`tasks.h:15-17`), więc handler **na wejściu** sprawdza wyzwalacz i bez niego zwraca `Errc::Paused` (po `pause()`); ponowna ocena tylko gdy wyzwalacz zaszedł |
| StateProjection | Claim z predykatem ze słownika danych (`attends_to`, `accepts_direction`, `urgency_of`, `floor_owner`, …), wartość `{estimate, method, uncertainty}`, `scope` = perspektywa, `derivation` = operator + wersja, klasa dowodu `inferred` z `ExpectedProperty` | nazwy osi to **opcjonalne projekcje**; uwaga ≠ aprobata = dwa predykaty; `trajectory.state {actor?, goal?, window?, modality?}` (P0.4) filtruje po tych kwalifikatorach — bez audio |
| Transform | `loom.method_graph/1` (`src/packet/METHOD_GRAPH.md`): metoda / wersja / parametry / prompt / przebieg z hashami; wynik → `produced_in_run` | datowane oceny = osobne twierdzenia z `measurement_status` |
| Assessment / Evidence | 7 pytań `model::Assessment`; `Support` = obserwacja + lokator + cytat; `Counter` | I2/I3 egzekwowane przez `KnowledgeStore` |
| BudgetPolicy | ustawienia + presety w `trajectory.pack budgets`; wywołania modeli przez `use_model` (×10, limity, `money_format`) | polityki lokalne kosztują **wiadome 0** (`money_format.h:10` tekst zera), nie „nieznane” (sędziowie 23) |

Dwa magazyny, jeden adres: pakiety w `loom_kb_*` (`GraphPacketStore` `accept/read/replay`, `graph_packet_store.h:16-18`: brakujący wiersz = dryf, nigdy naprawa); węzły Sekretarza w `nodes/links` adresowane z pakietu po `same_as` (`graph_sources.pack`, jak `msg:` = `m_`). Nawigator widzi oboje przez projekcję na żądanie, nic nie jest kopiowane do `nodes`.

### 2.3 Polityki tury, ocena i wybór (P1.2)

Działania, horyzonty, wymiary = listy w `trajectory.pack` (preset: `continue, yield, speed_up, reorder, front_load_key_info, brief_clarification, mute, suspend, resume, defer, abandon`; horyzonty `immediate, rest_of_turn, dialogue, long_term` **albo zdarzeniowe `{until_event}`**; wymiary `reception, info_value, urgency, escalation_cost, autonomy, trust, return_chance, uncertainty, compute_cost`). Trzy polityki jako dane z wersją i hashem: **`always_yield`** (preset), **`explicit_rule`** (reguły `when → then` z zamkniętego zestawu operatorów w kodzie, jak `kb::kConditionOps` `kb.h:94`; kombinacje otwarte), **`estimator`** (rola portów `turn_policy.estimate`; polityka modeli `turn_policy` w `guardian.pack models.policies`: `turn_policy_estimator_model` → rola → brak). Zasada GL12 dosłownie: model estymatora **nigdy nie jest dziedziczony z rozmowy**, ale Ty możesz ustawić ręcznie dowolny model, także model rozmowy (sędziowie 8). Każda polityka zwraca **wszystkich** kandydatów z Q per wymiar × horyzont i wskazuje wybranego; metoda wyboru jest nazwana w śladzie (`choice: rule_order` dla reguł, `choice: {method, dim}` z danych dla estymatora) — rekord decyzji nigdy nie zwija ocen do jednej liczby (sędziowie 15). `trajectory.compare` nigdy nie pisze „lepsza” (brak ocen ludzkich = `measurement_status: unavailable`). Estymator lokalny (JEV, VAP, GRU/TCN) to kolejny adapter tej samej roli — kandydat do porównania, nie sterownik; uczony estymator tylko na **prawdziwych** śladach, gdy już istnieją i gdy powiesz tak (sędziowie 16: nic nie uczymy na symulatorze).

### 2.4 Zobowiązania warunkowe (P0.3)

`defer` → Claim `intends_to_deliver` + zadanie `paused`. Wyzwalacze (ustawienie `trajectory_reassess_triggers`, preset `[user_turn_end]`; do wyboru `silence_at_least {t, unit}`, `goal_mentioned`, `explicit_resume`). `trajectory.reassess` sprawdza warunki (`still_relevant`, `still_urgent`, `user_did_not_cancel`, dalsze z danych) → nowa Decision: `deliver_proposal` (propozycja w rozmowie istniejącym `ActionProposalBlock`, koszt w wierszu) / `keep_waiting` / `abandon` (Claim `superseded`, Decision z powodem; treść zostaje). Wznowienie **zawsze** przez ponowną ocenę, nigdy wykonanie starego polecenia. „Nie” = sygnał `proposal_rejection` Sekretarza; po osiągnięciu `kinds.proposal_rejection.suppress_at` (preset, `guardian.pack:505`; dziś 1,0 przy opisie „dwa odmowy” — liczba czytana z danych, nigdy wpisana w test; sędziowie 9) propozycja milknie w tej rozmowie.

### 2.5 Diagnoza od pierwszego zdarzenia (TL;DR 6)

Operator `diagnose` po **każdej** decyzji (od pierwszej) w granicach budżetu (`trajectory_diagnosis_budget`: `local` preset = zawsze i za darmo; `local_and_estimator`; `off` → na decyzji zapis `diagnosis_skipped {reason}`, żeby późniejsza analiza była możliwa). Szablony hipotez są danymi (`policy_over_yields, reply_layout_provokes_interruption, priority_misjudged, user_goal_changed, value_conflict, estimator_artifact, client_report_missing, budget_blocked …`), każdy z warunkami wsparcia/zaprzeczenia i tekstami „co by to potwierdziło / obaliło”; wynik = konkurencyjne Entities `hypothesis` + Claims `inferred` z `ExpectedProperty`, eksperymenty jako `extrapolated` (nigdy przesłanka). `repeat_count` (int64) jest **cechą**, nie bramką; test grep: brak liczbowego progu powtórzeń w `diagnose.cpp` i `trajectory.pack`. Progi Sekretarza (`guardian.pack:94-98`) bramkują wyłącznie **promocję hipotezy do reguły**, i to dopiero po Twoim „Trafne” (osąd, I4) i przez istniejący `guardian.promote` (sędziowie 13). Mechanizm jest ogólny: wejście = dowolny ciąg (decyzja, rezultat) — głos, przebiegi narzędzi, utrata kontekstu, kosztowne operacje; P39 zasila go trajektorią, projekt B zasila go wynikami symulacji.

### 2.6 Jedna powierzchnia API (M1 §2.6)

Wszystko to wpisy `actions.pack` z `port: actions`, `http`, frazami pl/en, `reads`, `effects`, `human` (sprawdzane `tools/check_action_reads.py`), bez zmian `loom.h` ani JNI (`loom_jni.cpp:391` `invoke`). Tor T: `trajectory.events / record / replay / state / assess / compare / reassess / resume / abandon / diagnose / judge / show`. Tor D: `duplex.simulate`, `duplex.session`, `duplex.probe`. Tor E: `voice.analyse`, `voice.align`, `avatar.state`, `avatar.ab`. Każda z decyzją „da się z rozmowy / nie, bo …” w `chat_coverage.pack` (R54; sędziowie 19 — narzędzia CLI są tylko cienkimi nakładkami na akcje). Web przez `LoomApi`, Android przez `invoke` (`callAsync` dla dużych wyników).

## 3. Trzy tory

Kolejność nadrzędna (Ty, 2026-10-21: „pierwsza rzecz to ekrany”): **M1-W0 idzie pierwsze**. Potem tor T aż do pierwszej bramy; tory D i E są **osobnymi** programami badawczymi — jeden agent naraz, w budżecie S2 (preset 4 agentów, który sam ustawiłeś), bez trzeciej sesji otwieranej przez agenta (sędziowie 27: to pytanie tak/nie do Ciebie, §9). Każdy tor ma własne bramy z czymś do dotknięcia.

### 3.1 Tor T — trajektoria rozmowy z tekstu (bez audio, bez wideo)

Z projektu A, przestawiony tak, żeby pierwszy dotykalny wynik wymagał **czterech** pozycji, nie siedmiu (sędziowie 2): `T-A1 → T-K1 → T-K2 → T-K3` daje w rozmowie propozycję „Dokończyć przerwaną odpowiedź?” i zdania „pokaż zdarzenia tej rozmowy”, „co odłożyłeś”, „porzuć odłożone” — bez pakietu, bez replay, bez panelu. Potem `T-K7 → T-W1` daje ekran w nawigatorze M1 (gdy W0 jest na main) albo w Warsztacie (do tego czasu). Dopiero potem pakiet, polityki, diagnoza (`T-K4 → T-K5 → T-K6`), plan z modelu, porównanie, skala, Android, dokumentacja.

1. **Pierwsze przerwanie jest prawdziwe, nie symulowane**: „stop” w czacie już dziś zapisuje fragment z `metadata.cancelled`; T-K3 dopisuje do tego zdarzenie `interrupt` z pozycjami.
2. **Hak na żywo nie pisze do bazy w wątku wysyłki** (sędziowie 7): `ChatEngine::send` wrzuca zdarzenie do kolejki w pamięci (`TrajectoryInbox`, mutex + wektor) i budzi wątek tła, który zapisuje `EventLog` (`provenance.h:158` — to jest zapis do bazy) i zgłasza zadanie `trajectory.build`. Test liczy zapisy bazy w wątku wysyłki: 0. Znana strata: awaria procesu przed zrzutem gubi zdarzenia z kolejki — ustawienie `trajectory_live_flush` (`background` preset; `inline` = trwałość za cenę jednego zapisu w ścieżce wysyłki; Twój wybór).
3. **Pozycja „wyświetlone” jest uczciwa**: dopóki powłoka nie wyśle `displayed_chars` przy anulowaniu (web w T-K3, Android w T-A2), wiersz mówi „wyświetlone: nieznane (klient nie zgłosił)”, nie liczbę (sędziowie 10).
4. **Pierwszy ekran = nawigator M1**: węzeł `trajectory:<rozmowa>` pod rozmową, lista zdarzeń jako wiersze dzieci, perspektywa „Trajektoria” (układ `timeline`) na mapie, karty decyzji / zobowiązań / hipotez z chipami dowodu i działaniami z `actions.for`. Jeden komponent `TrajectoryTimeline.tsx` montowany w karcie węzła nawigatora (po W0) i w panelu Warsztatu (przed W0) — ta sama projekcja `graph.navigate`/`graph.card` z K0, które są już na main.
5. **Prawdziwe dane lokalnie** (sędziowie 1): `trajectory.events` i `trajectory.compare` uruchamiamy na Twoich rozmowach w bazie i na eksportach lokalnie (T-R2; D3: prawdziwe dane dozwolone i preferowane tam, gdzie jest wybór); do repozytorium trafiają tylko liczby zbiorcze; syntetyczne fikstury zostają jako kontrola mechanizmu.
6. **Diagnoza bez bramki licznikowej** (§2.5).

### 3.2 Tor D — kontroler pełnego dupleksu i symulator

Z projektu B, ale oparty na kontraktach toru T (sędziowie 18: jeden strumień, jedno kodowanie czasu, jedno słownictwo polityk, jedna rola estymatora `turn_policy.estimate`, jedno ustawienie `turn_policy`, jeden rodzaj zadania `trajectory.deferred`, jedna diagnoza). Co bierzemy z B: deterministyczny symulator scenariuszy jako **producent** strumienia; kanały `capture / generation / synthesis / playback / conversation_memory` z własnymi buforami, pozycjami i wersjami; cięcie w środku jednostki z zanikiem; prędkość odtwarzania jako parametr (`num/den`); sondy dostawców „pominięte, nie czerwone”; łańcuch sha256 segmentów; presety prywatności testowane na każdej ścieżce. Co odrzucam: progi Sekretarza jako bramka hipotez (13), Q jako suma ważona (15), `learned_small` uczony na symulatorze (16), `uint64` (17: wszędzie `int64`), osobna tabela `loom_duplex_events` i identyfikatory `ds_…#seq` (18: segment strumienia + wskaźnik JSON), osobny ekran „Dupleks (laboratorium)” (32: wiersze w **jednym** obszarze Eksperymenty i kalibracja przez `calibration.items`, `graph_entry.cpp:494`), sześć niewidocznych pozycji z rzędu (14: pierwsza pozycja D-K1 daje już tabelę w rozmowie i wiersz w Eksperymentach).

Pierwsze **prawdziwe** audio (D-W1) nie wymaga żadnego dostawcy: synteza mowy przeglądarki (`speechSynthesis`, także w WebView Androida) jest adapterem roli `speech.synthesize` o koszcie wiadomym 0, ze zdarzeniami granic słów → `P_played` na poziomie słowa; VAD energetyczny lokalnie; Twoje wejście w słowo tnie odtwarzanie i zapisuje `interrupt` z pozycjami do tego samego strumienia, a reszta odpowiedzi staje się zobowiązaniem (T-K2). Hipoteza H6 (§8) mówi, czy granice słów w WebView są użyteczne — mierzymy na emulatorze, nie zakładamy.

### 3.3 Tor E — ekspresja: głos, twarz, awatar (badania)

Z projektu C, z poprawkami: (22) żadnej czarnej listy słów i żadnego „nigdy” dla stresu — test brzmi: narzędzie nie emituje **twierdzenia o stanie psychicznym** (lista predykatów w danych) z pochodzeniem innym niż `user`; Twoje własne adnotacje („zdziwienie”, „strach jako rodzaj stresu”, „niepewność”) są twierdzeniami `origin: user`; (23) zero wywołań = koszt **0**, nie „nieznany”; (24) słownik stanów asystenta w `avatar.pack` (otwarty), reduktor mapuje zdarzenia na stany **z danych**, nieznane przepuszcza; (25) wynik A/B podawany od pierwszego przebiegu z `n` i niepewnością, **preset zostaje Twój**; (26) synchronizacja na poziomie słów wymaga zmiany `media_providers.cpp` (E-V2 ją zawiera; tabela mówi „słowo” albo „segment”); (28) sieć w narzędziu głosu to ustawienie `voice_tool_network` z presetem `off` i powodem, nie mur; (29) treść polityki artefaktów ustalamy **po** wyciągnięciu Twoich słów z eksportów (E-A1), nie z pamięci.

**AV01 jako hipoteza badawcza** (Twoje słowa z 2026-10-21 + przekazanie §10): zamiast gonić hiperrealizm, awatar celowo pokazuje sztuczność — widoczne szwy siatki, skwantowany ruch, niskie szczegóły, degradację złożoności pod obciążeniem jako cechę, stylizowane cieniowanie, widoczny „glitch myślenia” (lista otwarta, do potwierdzenia na Twoich słowach). Test: pary wariantów oceniane przez Ciebie (`avatar.ab`), wynik z `n`; nic nie zmienia presetu bez Ciebie. Twarz jest **rendererem** (`view_renderers`) stanu z toru T/D: ten sam strumień zdarzeń (słucha / myśli / mówi / ustąpił / ma coś odłożone) steruje mimiką, głosem, sceną. Model embeddingowy twarzy z przekazania §9 (E-F1) wymaga Twojego lokalnego pliku `face_embedding_research_master_prompt_2026-10-19.md` — nie ma go w repozytorium; do dostarczenia (§9).

## 4. Co zobaczysz i dotkniesz, i kiedy

| Brama | Po pozycjach | Na telefonie (390×844) |
|---|---|---|
| **M1 Brama 0** (pierwsza, nie z P39) | M1-W0, M1-A0 | nawigator Graf › ROM / Ustawienia / Role / Ty — warunek, żeby P39 miało gdzie mieszkać |
| **T0 — w rozmowie** | T-A1, T-K1, T-K2, T-K3 | Zatrzymujesz odpowiedź w połowie → wysyłasz następną wiadomość → po odpowiedzi propozycja „Dokończyć przerwaną odpowiedź? Ocena ponowna: cel nadal aktualny · nie odwołałeś · 2 min temu. Koszt: … (szacunek z rejestru albo „nieznany — brak ceny”)” z Tak/Nie; „nie” jest pamiętane (próg z presetu). Zdania: „pokaż zdarzenia tej rozmowy” (lista zdarzeń słowami: kto, kiedy względnie, rodzaj, fragment, chip „wyprowadzone z wiadomości”; przerwanie z „wygenerowane: 412 znaków · wyświetlone: 380 znaków” gdy web zgłosił, inaczej „nieznane (klient nie zgłosił)” · „usłyszane: nieznane (brak estymatora)”), „co odłożyłeś” (lista z warunkami: które zachodzą, które nieznane), „wznów odłożone”, „porzuć odłożone” (potwierdzenie z reguły; potem „porzucone, nic nie skasowane”). Ustawienia › Rozmowa › „Tura i przerwania” (5 pól po ludzku). Graf › Eksperymenty i kalibracja: trzy polityki tury z chipem **kalibracja** (estymator: „niedostępny: brak modelu”) |
| **T1 — ekran** | T-K7, T-W1 | ⋮ › Ta rozmowa w grafie › węzeł **Trajektoria** (liczniki: zdarzenia, decyzje, odłożone, hipotezy; „nieznane (powód)” zamiast 0) → oś czasu: wiersz = zdarzenie (chip uczestnika, czas względny, rodzaj po polsku, fragment); perspektywa „Trajektoria” na mapie (układ lewo–prawo). **Pasek „Odłożone (1)”** nad polem pisania z Wznów / Porzuć (ustawienie `trajectory_strip`). Do czasu scalenia W0 to samo w Warsztat › Trajektoria. Dolna linia uczciwości: „Bez audio: »odtworzone« = wyświetlone znaki; »usłyszane« = hipoteza” |
| **T2 — decyzje i diagnoza** | T-K4, T-K5, T-K6 | Wiersz decyzji rozwijany: wybrane działanie, alternatywy z paskami per wymiar × horyzont (nigdy jedna liczba), polityka + wersja, chip **na żywo / cień** (cień = policzone na starej rozmowie), koszt („lokalnie · 0 · 2 ms” albo model z ceną / „nieznana”), „dlaczego”. Sekcja „Co może być nie tak”: konkurencyjne hipotezy od pierwszej decyzji z pewnością, dowodami, „co by to potwierdziło / obaliło” i przyciskami Trafne / Nietrafne. Przyciski „Odtwórz” (identyczna ✓ `gp_…` albo dryf z powodem) i „Porównaj polityki” (tabela trzech polityk: decyzje, zatrzymane pilne treści, niepożądane upieranie się, opóźnienie p50/p95 zmierzone, koszt; nagłówek „pomiar na Twoich danych, bez ocen ludzkich — nie dowód wyższości”). Karta `model:` / `party:self` dostaje twierdzenia stanu (`trajectory.state`) |
| **T3 — domknięcie** | T-R1, T-R2, T-T1, T-A2, T-D1 | Ustawienia › Rozmowa › „Plan odpowiedzi od modelu” (preset wyłączony, chip kalibracja); w Eksperymentach „ostatnie porównanie: data, fikstury N + Twoje rozmowy M lokalnie, bez ocen ludzkich”; raport ze zrzutami z emulatora `docs/reports/P39_CHECKPOINT_ANDROID.md`; `docs/STATE.md` z tym przejściem słowami |
| **D0 — symulator** | D-K1, D-K2 | Zdanie „zasymuluj przerwanie w połowie słowa” → tabela w rozmowie: oś zdarzeń z pozycjami `wygenerowane 412 · zsyntezowane 380 · odtworzone 203 · usłyszane ~180 (hipoteza)`, decyzje polityki, opóźnienie w µs, koszt 0; wiersz sesji w Eksperymentach i kalibracji; ten sam strumień widać w Trajektorii (T1) z chipem **symulacja** |
| **D1 — prawdziwy głos** | D-W1 | Ustawienie `duplex_voice_loop` włączasz sam → odpowiedź czytana głosem przeglądarki (koszt 0), mówisz w trakcie → głos cichnie w środku słowa, wiersz `interrupt` z pozycjami na osi czasu, pasek „Odłożone (1)”, propozycja „Dokończyć?”. Gdy WebView nie daje granic słów: „odtworzone: segment (powód)” |
| **D2 — dostawcy** | D-K3, D-T1 | Eksperymenty: tabela dostawców mowy (zmiana tempa w trakcie: tak/nie/niezbadane; cięcie: …) z datą sondy i kosztem; sondy tylko po `duplex_probe_network = on` i z kluczem |
| **E0 — głos jako funkcje czasu** | E-V1 (+E-V2) | `+ › Nagraj` → „przeanalizuj ten głos” → tabela cech w czasie (F0, jitter, shimmer, HNR, formanty, MFCC, energia, rytm, pauzy i miejsca oddechu względem tekstu), lokalnie, koszt 0; po E-V2 wiersze per słowo (Google) albo per segment (Groq) — poziom nazwany; Twoje adnotacje („tu zdziwienie”) jako twierdzenia z pochodzeniem `Ty` |
| **E1 — twarz i artefakty** | E-A1, E-A2, E-F2 | Karta `model:` › Widok ▾ › **twarz**: proceduralna twarz 2D reaguje na stan (słucha / myśli / mówi / ustąpił) z toru T/D; suwaki artefaktów (szwy, kwantowanie ruchu, degradacja) jako ustawienia; „Oceń pary” → wynik z `n` i niepewnością; Twoje słowa o artefaktach zacytowane w `SLOWA_WLASCICIELA` po E-A1 |
| **E2 — badania** | E-F1, E-S1 | Eksperymenty: wynik czułości embeddingu (dodatni/ujemny z hashami); projekt sceny/reżyserki jako renderer (dokument, bez kodu) |

## 5. Pozycje pracy (zależności, nakład, agent)

Nakład liczę z tempa `main`, nie z paneli: M1 §6 mierzy 3–6 h pracy na pozycję; M1-D0/D1/K0 na `main` zajęły **3,5 / 10,5 / 7 h zegarowych z przeglądami** (2026-10-16 15:48 → 2026-10-17 13:47, 11 rund przeglądów, dwa okna limitów). Przyjmuję więc **3–6 h pracy + 1–5 h przeglądów na pozycję**; „Fable” tam, gdzie determinizm, współbieżność albo model danych są trudne (Twoje: „wykorzystaj fabla gdzie trzeba”). Plik maszynowy: `docs/reports/P39_WORK_ITEMS.json`.

| Id | Tytuł | Obszar | Zależy od | h pracy | Agent |
|---|---|---|---|---|---|
| T-A1 | `trajectory.pack` + schemat strumienia + fikstury syntetyczne + ustawienia + rodzaje węzłów/krawędzi + rola `turn_policy.estimate` + polityka `turn_policy` | data | M1-W0 (kolejność, nie kod) | 3–5 | Sonnet |
| T-K1 | Strumienie zdarzeń: parse/walidacja (int64 + skala), wyprowadzenie z rozmowy, `trajectory.events` | kernel | T-A1 | 4–6 | Sonnet |
| T-K2 | Zobowiązania warunkowe: `intends_to_deliver` + zadanie `paused` + wyzwalacze + reassess/resume/abandon; minimalny pakiet; polityka `always_yield` przez gniazdo; testy P0.3 | kernel | T-K1 | 5–6 | Fable |
| T-K3 | Hak na żywo: kolejka w pamięci → wątek tła → EventLog + `trajectory.build`; `displayed_chars` z web; propozycja po końcu tury | kernel+web | T-K2 | 4–6 | Fable |
| T-K7 | Źródła projekcji `trajectory:/tevent:/decision:/intention:/hypothesis:` + perspektywa „Trajektoria” + słowa nawigatora | kernel | T-K2 | 3–5 | Sonnet |
| T-W1 | `TrajectoryTimeline` w karcie węzła nawigatora (po W0) i w Warsztacie (przed W0), pasek „Odłożone”, wpis ⋯, testy headless + e2e 390 px | web | T-K3, T-K7 | 5–8 | Sonnet |
| T-K4 | Pakiet pełny: segmenty zamknięte z hashem, METHOD_GRAPH, CAS, replay, `trajectory.record/replay/state` (P0.4) | kernel | T-K2 | 5–6 | Fable |
| T-K5 | Kandydaci, Q per wymiar × horyzont (punkty bazowe), `explicit_rule`, gniazdo estymatora, operator tempa czytania, `trajectory.assess/compare` | kernel | T-K4 | 5–6 | Fable |
| T-K6 | Diagnoza od pierwszej decyzji, hipotezy konkurencyjne, `trajectory.diagnose/judge`, test grep bez progu | kernel | T-K5 | 4–6 | Sonnet |
| T-R1 | Plan interwencji z modelu w przepływie grafowym (atrybuty opcjonalne, `reply_plan` preset off, fikstura) | kernel | T-K5 | 3–5 | Sonnet |
| T-R2 | Protokół porównania + `tools/trajectory/compare.py` + **przebieg na Twoich rozmowach lokalnie** (liczby zbiorcze) | research | T-K5, T-K6 | 3–5 | Sonnet |
| T-T1 | Skala: 10^5 zdarzeń, 10^3 aktorów, id > 2^53, historia ograniczona segmentami, ASan | tests | T-K4, T-K2 | 3–5 | Sonnet |
| T-A2 | Android: smoke (stop → pasek → propozycja → trajektoria → wstecz), `displayed_chars` w JNI, ten sam id pakietu x86/arm64 | android | T-W1 | 3–5 | Sonnet |
| T-D1 | Dokumentacja: INDEX, STATE słowami, LUKI MM01–MM10, pokrycie czatu | docs | T-K7, T-W1 | 2–4 | Sonnet |
| D-K1 | Symulator scenariuszy jako producent strumienia (`duplex.pack`, seed, kanały, pozycje) + `duplex.simulate` + wiersz w Eksperymentach | kernel | T-K1 | 4–6 | Fable |
| D-K2 | Kontroler kanałów (`duplex.h`): maszyny stanów, polecenia jako zdarzenia, cięcie w środku jednostki, tempo num/den, synchronizacja kontekstu z `P_played` | kernel | D-K1, T-K2 | 5–6 | Fable |
| D-W1 | Pętla audio w web: `speechSynthesis` jako adapter (koszt 0, granice słów), VAD lokalny, ciągłe przechwytywanie za zgodą, `duplex_voice_loop` preset off | web | D-K2, T-W1 | 5–8 | Sonnet |
| D-K3 | Sondy dostawców (tempo w trakcie, cięcie, strumień) jako datowane twierdzenia; `duplex_probe_network` preset off; CI pominięte-nie-czerwone | kernel | D-K2 | 3–5 | Sonnet |
| D-T1 | Testy P1.5: opóźnienie, brak internetu, brak estymatora, brak audio, prywatność lokalna; budżet czasu z danych | tests | D-K2 | 3–4 | Sonnet |
| E-V1 | Narzędzie głosu (DSP, `python -I`, numpy) → `loom.voice_features/1` → twierdzenia `measured.*`; `voice.analyse` z czatu; sieć = ustawienie off; test predykatów psychicznych | tools+kernel | — | 5–6 | Sonnet |
| E-V2 | Synchronizacja słów: `media_providers.cpp` zachowuje czasy słów Google (segmenty Groq), `voice.align`, linia bazowa osobnicza jako dane | kernel | E-V1 | 3–5 | Sonnet |
| E-A1 | Twoje słowa o awatarze i artefaktach wyciągnięte lokalnie z eksportów (X-Port), zacytowane w `SLOWA_WLASCICIELA` | docs | — (Twoje tak/nie na wgląd w eksporty) | 2–3 | Sonnet |
| E-A2 | Polityka artefaktów jako dane (`avatar.pack artifacts`, otwarta) + `avatar.ab` (pary, wynik z `n`), preset Twój | data+web | E-A1, E-F2 | 3–5 | Sonnet |
| E-F2 | Minimalny awatar proceduralny 2D jako `view_renderers.face_2d`: parametry blendshape-podobne, stany z danych, lipsync z granic słów / amplitudy, reduktor na zdarzeniach T-K3 | web+data | T-K3, (D-W1 dla lipsync z głosu) | 5–8 | Fable |
| E-F1 | Test czułości embeddingu na kontrolowanych mimikach (master prompt = Twój plik lokalny) — wynik dodatni/ujemny z hashami | research | E-F2, Twój plik | 4–6 | Fable |
| E-S1 | Scena / reżyserka jako kolejny renderer — projekt (dokument), bez kodu | docs | E-F2 | 2–3 | Sonnet |

Sumy (praca, bez przeglądów): tor T 14 pozycji ≈ 52–78 h; brama T0 (4 pozycje) ≈ 16–23 h → **1–2 doby zegarowe** z przeglądami i oknami limitów (nie „1,5 dnia do ekranu” — ekran T1 to +8–13 h); T1 ≈ +8–13 h; T2 ≈ +14–18 h; T3 ≈ +14–24 h. Tor D 5 pozycji ≈ 20–29 h; tor E 7 pozycji ≈ 24–36 h, badawcze — wynik ujemny też jest wynikiem. Całość przy jednym torze S1 i S2 do 4 agentów: ≈ 5–8 dób zegarowych, ograniczone oknami limitów, nie pracą.

## 6. Sprawdzenia odbiorcze

Każda pozycja ma testy w swoim briefie (JSON). Sprawdzenia bram:

**T0.** `ctest`: `unit.test_trajectory` — fikstura `interrupt_mid_word.json` z `t` > 2^53 odczytana i zapisana bajt w bajt; nieznany rodzaj zdarzenia zostaje z `emergent: true`; `derive_from_conversation` dwa razy → ten sam sha256; anulowana wiadomość asystenta → `interrupt` z `generated` ≠ `played` (web) albo `played = unknown(no_client_report)`. P0.3: `new_fact` → wcześniejsza decyzja `superseded`, nowa cytuje obserwację faktu jako kontrę; `goal_change` → `valid_to` celu, zobowiązanie porzucone z powodem `goal_superseded`, segment treści zostaje; `user_turn_end` → reassess → propozycja → „tak” → twierdzenie `delivered` z pozycjami; zadanie zostawione `running` → `recover_interrupted` → `pending` → handler bez wyzwalacza zwraca `Paused` (zobowiązanie przetrwało); „nie” × (`suppress_at` z presetu) → propozycja milknie; reassess nigdy nie wykonuje starej decyzji bez ponownej oceny (zmieniony warunek odwraca wynik). Na żywo (`ScriptedTransport`): anulowanie w trakcie strumienia → kolejka ma zdarzenie, licznik zapisów bazy w wątku wysyłki = 0; `trajectory_live = off` → bajty żądania identyczne ze złotym. Czat: zdania z `actions.pack` rozpoznawane lokalnie (test `chat_coverage`). `check_action_reads.py` 0 problemów; `gen_*.py --check` czyste; golden `settings_fields.json` z jądra. **Ty**: na emulatorze/stronie stop → wiadomość → propozycja z kosztem.

**T1.** `test_graph_projection`: rozmowa → dziecko `trajectory:` z licznikami; bez zapisu → `count_unknown_reason`; hops 2 bez sierot; chipy dowodu per rodzaj (decyzja `derived`, hipoteza `inferred`, estymator `inferred`+`model_knowledge` → nigdy „powiedziałeś”); perspektywa `trajektoria` przez `Perspectives::render` zwraca krawędzie osi czasu. Web headless: czas z `BigInt(t_text)`, brak literalnego „0” dla nieznanej kwoty, chip trybu na każdym wierszu decyzji; e2e 390 px: stop → wiadomość → pasek → propozycja → ekran → wstecz; `scrollWidth ≤ 390`, cele ≥ 44 px; `tsc -b` czysty.

**T2.** Ten sam strumień + ten sam hash packa → ten sam id pakietu i identyczne `encode` (I5); accept dwa razy idempotentne; restart magazynu → replay weryfikuje wiersze; sfałszowany wiersz → dryf, nie naprawa; `always_yield` → `yield + defer`; `explicit_rule` + backchannel → `continue`, wysoki priorytet reszty → `brief_clarification`; estymator bez modelu → `unavailable {reason: no_estimator}` + zapisany zapas; ze skryptowaną odpowiedzią → twierdzenia `model_knowledge`, `measurement_status: unavailable`; `compare` bez pola „better”, pieniądze `0` lokalnie, `unknown` bez ceny; `diagnose` po jednym odroczeniu ≥ 2 hipotezy z różnymi `confirm_if`; żadna hipoteza nie ma klasy `observed`; grep: brak progu powtórzeń; `judge` przeżywa `clear_run` + odbudowę; `trajectory.state {actor: party:self, window}` zwraca tylko twierdzenia z pasującymi kwalifikatorami.

**T3.** `reply_plan` off → bajty żądania bez zmian (golden); predykcja oceniona po późniejszym przerwaniu → `Holds`; po zmianie kolejności → `NotApplicable`; `tools/trajectory/compare.py` na fiksturach bez wywołań dostawcy; przebieg lokalny na Twoich rozmowach zostawia w repozytorium tylko liczby zbiorcze (test grep na `docs/` po przebiegu: brak treści wiadomości); skala: 10^5 zdarzeń, 10^3 aktorów w budżecie z danych, `i` ≥ 2^40 w obie strony; historia pakietu ≤ N/segment; ASan raz, czas w INDEX; Android smoke: zrzuty na pixel_5 i pixel_6 + `font_scale 1.3`, id pakietu identyczny z x86.

**D0/D1/D2.** Ten sam seed → ten sam sha256 strumienia; przy przerwaniu `played < synthesized ≤ generated` i cięcie wewnątrz słowa (pozycja w kodepunktach); polecenia kontrolera zapisane jako zdarzenia `command` → replay identyczny; cięcie działa na granicy najbliższej ramki (rozmiar ramki z danych); tempo `num/den` zmienia odwzorowanie pozycji; `speechSynthesis` niedostępne → „niedostępne: powód”, nie błąd; e2e: głos przeglądarki czyta, `overlap_start` z VAD tnie, `interrupt` ma pozycje słowa; sondy bez `duplex_probe_network` → pominięte z powodem, nie czerwone; P1.5: brak internetu → zdarzenie widoczne, nie cisza; brak estymatora → zapas zapisany; brak audio → tryb tekstowy bez zmiany pakietu; licznik wywołań sieciowych z symulatora i kontrolera = 0.

**E0/E1/E2.** `voice.analyse` na syntetycznym sygnale (ton o znanym F0 + modulacja) zwraca F0 w tolerancji z danych; `time_base {samples, rate}` zachowany, ramki int64; żadne twierdzenie z predykatem z listy `psychological_predicates` nie ma pochodzenia innego niż `user`; sieć wyłączona w presecie → próba połączenia kończy się zdarzeniem „zablokowane ustawieniem `voice_tool_network`” z powodem; po E-V2 czasy słów Google zachowane (fikstura odpowiedzi), tabela nazywa poziom; `face_2d` renderuje w 2D bez WebGL, stany z `avatar.pack`, nieznany stan przepuszczony z chipem; `avatar.ab` po jednej parze zwraca wynik z `n = 1` i niepewnością, preset nietknięty; E-A1: cytaty w `SLOWA_WLASCICIELA` mają datę i źródło (eksport), żadna treść poza cytatami nie trafia do repozytorium.

## 7. Ryzyka

1. **W0 jeszcze nie na main** (`GraphNavigator.tsx` nie istnieje w żadnej gałęzi `claude/m1-*` sprawdzonej 2026-10-21): T-W1 ma dwa punkty montażu (karta nawigatora / Warsztat), ale „jeden nawigator” pojawia się dopiero po scaleniu W0; słowa `presentation.graph_screen` koordynować, żeby nie było konfliktu nakładek.
2. **Walidacja pakietu odtwarza całą historię** → dopisywanie na żywo per zdarzenie byłoby O(n²); mitygacja: segmenty zamknięte + kolejka w pamięci + zadanie tła; T-T1 mierzy, zanim zaufamy presetowi 2000.
3. **Kolejka w pamięci gubi zdarzenia przy awarii** przed zrzutem — zapisane jako znana strata; `trajectory_live_flush = inline` do wyboru.
4. **`displayed_chars`** na Androidzie dopiero w T-A2 — do tego czasu „wyświetlone: nieznane (klient nie zgłosił)”.
5. **Trajektorie „cień”** starych rozmów mają czasy trwania wyprowadzone z tempa — każdy wiersz z chipem `derived` i metodą; ryzyko, że przeczytasz symulację jako historię mimo chipa (H1 na Twoich danych).
6. **Reguła jawna** używa leksykalnych wskazówek priorytetu (pl/en) — słaba na inne języki i ironię; to linia bazowa do porównania, nie polityka produktu.
7. **Estymator** niedostępny, dopóki nie ustawisz modelu/roli; wtedy porównujemy tylko dwie linie bazowe — H2 opiera się na fiksturach i Twoich tak/nie.
8. **Hipotezy od pierwszego zdarzenia** mogą brzmieć jak szum („Co może być nie tak” po jednym stopie) — budżet i nakładka szablonów pozwalają wyciszyć; promocja tylko Twoim osądem.
9. **Zmęczenie propozycjami** — `suppress_at` z presetu i `trajectory_live = record_only`; preset `record_and_propose` to wybór produktu, który możesz odwrócić.
10. **Determinizm x86/arm64** — Q w punktach bazowych (int), bez arytmetyki zmiennoprzecinkowej w regułach; T-T1 i T-A2 porównują id pakietów.
11. **`speechSynthesis` w WebView** może nie dawać granic słów albo zanikania — wtedy `P_played` na poziomie segmentu z powodem (H6).
12. **Narzędzie głosu** na Twoich nagraniach: linia bazowa osobnicza wymaga kilku nagrań — do tego czasu wartości bez odniesienia (H7).
13. **Plik master prompt twarzy** nie jest w repozytorium — E-F1 czeka na Ciebie.
14. **Czas** — sumy w §5 są z tempa `main`; okna limitów wydłużają zegar, nie pracę.

## 8. Hipotezy do sfalsyfikowania

- **H1 — tekst niesie wystarczający sygnał**: oś czasu z Twoich rozmów opisuje przerwania, poprawki, odrzucenia tak, że rozpoznajesz własne zachowanie. Falsyfikacja: lokalny odbiór na Twoich danych (T-R2 liczy, Ty oglądasz); ≥ 1 wiersz „nie tak było” obala dla tego rodzaju; osądy zapisane (`trajectory.judge`).
- **H2 — `always_yield` gubi pilne treści częściej niż `explicit_rule`**: metryka „zatrzymane pilne treści” z `compare` na fiksturach (kontrola) i na Twoich rozmowach (dowód); odsetek „tak” na „Dokończyć?” bliski zera = odkładanie bez wartości.
- **H3 — hipotezy od pierwszego zdarzenia są użyteczne**: stosunek Trafne : Nietrafne per szablon w czasie; bez progu — stale odrzucany szablon mówi to sam.
- **H4 — determinizm między platformami**: ten sam `packet_id` na x86 i arm64.
- **H5 — wniosek nigdy nie udaje obserwacji**: żaden wiersz z `method = model_*` nie renderuje się jako „powiedziałeś”.
- **H6 — granice słów `speechSynthesis` w WebView są użyteczne do `P_played`**: pomiar na emulatorze (D-W1); ujemny wynik = poziom segmentu z powodem.
- **H7 — cechy DSP są stabilne na Twoich nagraniach** (linia bazowa): wariancja między nagraniami tej samej sytuacji; ujemny = cechy bez odniesienia, uczciwie nazwane.
- **H8 — artefakty zmniejszają Twój dyskomfort doliny niesamowitości** (n = 1): `avatar.ab`; wynik z `n` i niepewnością, nigdy zmiana presetu bez Ciebie.
- **H9 — embedding obrazu jest czuły na kontrolowane różnice mimiki** (przekazanie §9): E-F1; ujemny wynik jest wartością.

## 9. Co musisz zrobić Ty (tak/nie albo plik)

1. **Wgląd w eksporty lokalnie** dla E-A1 i T-R2: czytamy Twoje rozmowy na tej maszynie, do repozytorium trafiają tylko cytaty (E-A1, do Twojej akceptacji) i liczby zbiorcze (T-R2) — tak/nie.
2. **Trzecia sesja (S3)** dla torów D/E równolegle z S1 i S2 — tak/nie; bez „tak” tory D/E idą jednym agentem w budżecie S2.
3. **Plik** `face_embedding_research_master_prompt_2026-10-19.md` do `docs/architecture/` (E-F1).
4. **Model estymatora** (`turn_policy_estimator_model`), jeśli chcesz porównania z trzecią polityką — koszt pokazany przed każdym wywołaniem.
5. Bez zmian: ważny klucz, prawdziwy telefon, APK dopiero po M1 Brama 3.

## 10. Czego teraz nie budujemy (jawnie, z powodem)

- **Odtwarzanie PCM dostawców i strumieniowe realtime API** — po D-K3 (sondy mówią, co które API umie); pierwszy głos to synteza przeglądarki (koszt 0).
- **Lokalny JEV / VAP / GRU / TCN** — gniazdo `turn_policy.estimate` i protokół porównania są; Jev Lab nie jest w repozytorium (R38); uczony estymator tylko na prawdziwych śladach, gdy będą.
- **Pełna twarz 3D z blendshape'ami i optymalizacją po embeddingu** — E-F2 daje twarz 2D jako renderer; E-F1 odpowiada, czy embedding w ogóle widzi mimikę; 3D dopiero po dodatnim wyniku.
- **Scena / reżyserka / awatary podagentów** — projekt E-S1, kod po E-F2.
- **Automatyczna promocja hipotez do reguł** — tylko propozycja i Twój osąd; promocja przez `guardian.promote` jako osobny krok.
- **Kamera / biometria rozmówcy** — nie w P39; każde przetwarzanie obrazu wymaga jawnej zgody, lokalnie, bez przechowywania (przekazanie §10).
- **Rozmaitości / projekcje ciągłe** (TL;DR 8) — operatory generowania i referencje są; materializacja po potrzebie.

## 11. Co dodałem do Twoich list i dlaczego (CLAUDE.md „listy to przykłady”)

- **Rodzaje zdarzeń** (poza przekazaniem): `revision` (wersje wiadomości istnieją), `correction` (wystąpienia Sekretarza), `proposal` / `proposal_rejected` (router akcji), `cancel`, `command` (polecenia kontrolera jako zdarzenia — replay), `new_fact`, `goal_change`, `backchannel`, `silence`, `overlap_start`, `yield`, `resume` — bo już mają źródła w aplikacji albo są potrzebne do testów P0.3.
- **Szablony hipotez**: do sześciu z przekazania dodałem `client_report_missing` (w trybie tekstowym „wyświetlone” bywa nieznane) i `budget_blocked` (brak zasobów nie może kasować możliwości badania — §7 przekazania).
- **Ustawienia**: `turn_policy`, `turn_policy_estimator_model`, `trajectory_live`, `trajectory_live_flush`, `trajectory_diagnosis_budget`, `trajectory_reassess_triggers`, `trajectory_strip`, `reply_plan`, `duplex_voice_loop`, `duplex_probe_network`, `voice_tool_network`, `voice_raw_retention`, `avatar_renderer`, `avatar_artifacts` — każde z `human {label, why, levels, effect}`; wszystkie presety do odwrócenia.
- **Scenariusze symulatora**: `three_party` (skala uczestników) i `new_fact_during_reply` (P0.3) poza oczywistymi `interrupt_mid_word`, `backchannel`, `overlap_both_talk`, `silence_then_resume`.
- **Cechy głosu**: do Twoich (F0, jitter, shimmer, HNR, formanty, MFCC, rytm, pauzy, oddech) dodałem energię, tempo mowy z transkrypcji i pozycje pauz względem granic składniowych (Twoja wypowiedź 5: „to co tekst wymusza”).
- **Artefakty awatara** (przykłady do potwierdzenia na Twoich słowach): widoczne szwy siatki, skwantowany ruch, celowo niskie szczegóły, degradacja złożoności pod obciążeniem, stylizowane cieniowanie, widoczny „glitch myślenia”.
- **Stany asystenta** (słownik otwarty w `avatar.pack`): `idle, listening, thinking, speaking, yielded, holding_deferred, awaiting_confirmation, cancelled, unknown` — z przejściami jako dane.
- **Predykaty stanu**: do przekazania (`floor_ownership, attention_distribution, content_reception, speaker_evaluation, argument_update, direction_acceptance, social_dynamics`) dodałem `urgency_of`, `intends_to_deliver`, `delivered`, `has_priority`, `predicts_intervention`, `exists_as`, `participates_as`.

## 12. Decyzje i jak je cofnąć (do `docs/reports/INDEX.md`)

1. Jeden strumień `loom.dialogue_events/1` dla toru T, D i E; segmenty zamknięte z hashem i `prev_sha256`. Cofnięcie: producent może pisać własny format, jeśli dostarczy konwerter do strumienia; nic poza plikiem schematu nie zależy od pola `segment`.
2. Czas `int64 + skala` w `attrs.time`; `Locator.time_*` tylko do wyświetlania. Cofnięcie: zostawić oba — brak konsumenta `attrs.time` nic nie psuje.
3. `ObservationKind` bez zmian; rodzaj zdarzenia w `attrs.event_kind`; `Observation.ordinal` zostaje `int` (indeks w segmencie). Cofnięcie: nic do cofania — brak zmian w `model.h`.
4. Q per wymiar × horyzont w `DecisionAlternative.value` (punkty bazowe, int); `ValueImpact` tylko dla zasad. Cofnięcie: przenieść do osobnych twierdzeń `assesses` — czytelnik `trajectory.show` ma jeden punkt odczytu.
5. Trajektoria = GraphPacket w `loom_kb_*` (CAS, replay), nigdy wiersze `nodes`; T-K2 używa minimalnego pakietu, T-K4 go rozszerza. Cofnięcie: usunąć źródło `trajectory:` z `graph_sources.pack` i katalog `loom/src/trajectory/`.
6. Hak na żywo: kolejka w pamięci + wątek tła (zero zapisów bazy w wątku wysyłki); `trajectory_live_flush` preset `background`. Cofnięcie: `trajectory_live = off` albo `inline`.
7. `trajectory.deferred`: handler bez wyzwalacza zwraca `Paused`; `recover_interrupted` nie zmienia semantyki. Cofnięcie: handler jako zwykłe zadanie ponawiane (wtedy test P0.3 „awaria” trzeba zmienić).
8. Preset polityki `always_yield`; `explicit_rule`, `estimator` do wyboru; rola `turn_policy.estimate` bez adaptera; model estymatora nigdy dziedziczony, zawsze ustawialny (GL12 dosłownie). Cofnięcie: nakładka `profiles/trajectory.pack` albo ustawienie `turn_policy`.
9. Diagnoza po każdej decyzji, bez progu licznikowego; progi Sekretarza tylko dla promocji do reguły po Twoim osądzie. Cofnięcie: `trajectory_diagnosis_budget = off` (zapis `diagnosis_skipped` zostaje).
10. Pierwszy ekran = nawigator M1 (karta węzła + perspektywa `timeline`), komponent montowany też w Warsztacie do scalenia W0. Cofnięcie: usunąć panel `trajectory` z `panels.ts`; źródła projekcji zostają danymi.
11. Eksperymenty i kalibracja mają **jednego** dostawcę (`calibration.items`); polityki, sesje symulatora, sondy, testy głosu i twarzy to wiersze z datowanych twierdzeń. Cofnięcie: osobne źródło projekcji `research:` (nie planowane).
12. Prawdziwe dane: przebiegi lokalne na Twoich rozmowach i eksportach po Twoim tak/nie (§9.1); w repozytorium tylko liczby i zaakceptowane cytaty. Cofnięcie: `--bez-prawdziwych` w narzędziach i wpis w INDEX.
13. Pierwszy głos = `speechSynthesis` przeglądarki jako adapter `speech.synthesize` (koszt 0); dostawcy po sondach. Cofnięcie: adapter w `ports.pack` na `disabled`.
14. Narzędzie głosu: sieć jako ustawienie `voice_tool_network` (preset off), test „brak twierdzeń psychicznych bez pochodzenia `user`”, bez czarnej listy słów. Cofnięcie: `voice_tool_network = on`; test zostaje.
15. Awatar: słownik stanów i artefaktów w `avatar.pack` (otwarte), wynik A/B zawsze z `n`, preset Twój. Cofnięcie: `avatar_renderer = none`.
16. Brak S3: tory D/E jednym agentem w budżecie S2, dopóki nie powiesz inaczej. Cofnięcie: Twoje „tak” w §9.2.
