# Interfejs współpracy — kąt koordynatora i podwykonawców: narzędzia, rutyny, makra, szablony jako dane (Fable, 2026-10-23)

Stan na 2026-10-23 ≈ 18:3x UTC. Jeden z trzech projektów R90 (`tools/workflows/panel-interface.js`); buduje na syntezie R89
(`docs/architecture/INTERFEJS_WSPOLPRACY_2026-10-23.md`), nie powtarza jej. Każda konkretna nazwa, liczba i lista niżej to **przykład**
albo **preset** (oznaczone tak w tekście); fakt o teraźniejszości ma datę; słowa właściciela tylko z R-id i dosłownie. Identyfikatory po
angielsku, tekst po polsku.

## §1 Teza

Warsztat koordynatora jest dziś KODEM i PROZĄ: 9 skryptów workflow w JS (92 KB, 21 wywołań `agent()`), 21 briefów pisanych ręcznie
(mediana 6,6 tys. znaków) i dziennik (`QUEUE_S1.md`, `INDEX.md`) pisany ręcznie przez najdroższy model. Zasada KOD ≠ DANE, którą apka
stosuje do rozmów, nie została jeszcze zastosowana do pracy nad apką. Teza: **rutyna = akcja złożona w danych**, w tym kształcie, który
jądro już ma (`actions.pack`: kroki, `when`, `variants`, bramki `confirm`, trwały bieg `loom.action_run/1` z paragonem); **narzędzia
warsztatu = źródło narzędzi** (jak serwer MCP w `tool_sources.h`: jedno działanie na narzędzie, efekty, kto może uruchomić); **szablon =
rekord z odwołaniami** (`spec:<id>`, dokument, decyzja, pomiar), nie wklejony tekst; **makro = słowo/przycisk → rutyna z parametrami**.
Bieg rutyny zostawia dokument, z którego linie statusu dla właściciela są RENDEREM, nie ręcznym wpisem. Interfejs od strony pracy to
perspektywa tego samego grafu, co strona spec: rutyny, biegi, narzędzia, „czeka na koordynatora”. Tani operator (Sonnet/Haiku)
uruchamia rutyny z twardym kryterium odbioru; drogi koordynator dostaje to, co ma pole `judgement`. Jego przykład („cała filozofia do
kontekstu”) jest jedną instancją jednego kroku (`context_pack`) jednej rutyny (`subcontractor.start`) — mechanizm jest ogólny.

## §2 Dowody z jego słów i pomiarów

| Źródło | Dosłownie / pomiar | Co z tego wynika dla warsztatu |
|---|---|---|
| R90 | „to go trzeba na bogato zrobić, wyeksponować wszystkie narzędzia, może jakieś rutyny makra szablony czy jak to zwał wprowadzić dla określonych czynności, przykład: przy starcie nowego podwykonawcy całą filozofię mu zapodać do kontekstu. przypominam przykład to jest” | rzeczy do zbudowania jako dane (lista otwarta): katalog narzędzi, rutyny, szablony, makra (K9); „filozofia do kontekstu” = JEDEN przykład pakietu kontekstu (K3) |
| R90 | „i tak jak mówiłem - administrujesz tamtą maszyną” | maszyny (chmura, VM) są środowiskami biegów; jądro ma już węzły `environment:*` (`environments.h`, 23.10) |
| R88 | „pracowali po prostu na jednej specyfikacji w ustalonym formacie”; „żonglerka modelami w zależności od stopnia złożonościi [sic] nietypowości mojej korekty”; „sekretarz ustala co zostało faktycznie ustalone”; „nie bierz tego schematu dosłownie” | format = dane; dobór modelu po cechach = routing jako dane; sekretarz = pierwsza rutyna operatora (zadanie od 23.10 09:35 UTC, R88 v1); schemat to przykład |
| R89 | „ty jako fejby być zażądał pracą Sonet byłby do konwersacji na przykład” [ASR, sic]; „to nie jest specyfikacja bo ty lepiej zna swój flow” | role to presety; projekt przepływu należy do koordynatora |
| R61 | „droga praca, to startujesz po upewnieniu się że dobrze zrozumiałeś, parafrazując wymagania i prosząc o ewentualne korekty i zatwierdzenie” | parafraza (`understanding` w spec) = krok rutyny pakietu, nie obyczaj |
| R62 / R75 | „jeśli wiesz z dużą dozą prawdopodobieństwa dobrze co robić żebym był zadowolony, to działaj śmiało”; „ostrożnie podejmij wstępną taką żeby się dało z małymi kosztami w razie w wycofać” | pauza biegu bez niego ma „co zrobię bez odpowiedzi i kiedy” i koszt odwrotu; decyzja = rekord z `reversal` |
| R63 | „jak zmieniam model na lepszy to domyślnie możesz poluzować troszkę wymagania” | progi w rutynach (ile pytać, ile czekać) to presety per model koordynatora, nie stałe |
| R73 | „żebym widział co pamiętasz, na żywo” | bieg widoczny w trakcie (kroki, stany), nie po fakcie |
| CLAUDE.md, „Thread style” (23.10) | „w tym wątku wpisz tylko co zaczynasz implementować, co kończysz, co budujesz, co nie wyszło, co agenci wymyślili oryginalnego” | to jest schemat zdarzeń biegu: started / finished / building / failed / original_ideas → linie statusu = render dziennika biegów (K7) |
| pomiar 23.10 (ten projekt, `tools/workflows/briefs/`) | 21 briefów: mediana 6 628 znaków, razem 146 KB; „CACHE RULE” w 20/21, „context small” 18/21, „never kill” 17/21, „Private data” 17/21, `INDEX.md` 14/21; **`spec:` w 0/21** | te same reguły wklejane ręcznie do każdego briefu — szablon istnieje, ale w głowie koordynatora; mechanizm cytowania słów właściciela (SPEC1, `spec.py cite`) nieużywany przez żaden brief |
| pomiar 23.10 (ten projekt) | 9 skryptów workflow, 92 KB JS, 21 wywołań `agent()`; 37 z 60 ostatnich commitów gałęzi to księgowość (`queue:`, `ledger:`, `ops:`, `gate:`, `docs:`, `spec:`) | warsztat = kod + proza pisana przez Fable; zapis biegu nie istnieje jako dane (plik `sekretarz_runs.jsonl` z P1 jeszcze nie powstał — obserwacja 23.10) |
| R89 §0, §4.2, §4.7 (nasze-01…30) | koszt agenta ∝ długość² (nasze-01); 24 % wydatku agentów to nie praca: 7 % prefiks, 17 % konteksty przepisane po pauzie > 5 min (nasze-02); 95 % zużycia to agenci (nasze-04); pakiet 0,43–0,5 pkt, projekt Fable 1,2–1,6 pkt (nasze-06); agenci 2–5× planu wywołań (nasze-07); trzy restarty ≈ 0,9 pkt (nasze-08); czytacz logu 1,5 mln (nasze-16); kontrola Haiku 14–17 wywołań (nasze-24); pętla przeglądów 54 % (nasze-30); koordynacja ≈ 20 tur × 0,28 mln ≈ 0,25 pkt na pakiet (jeden pomiar) | prefiks wspólny bajt w bajt (szablon), korekta jako addendum (nie restart), wycinek logu, kontrola jako tani krok, tury koordynatora na kroki mechaniczne → operator |
| R89 §9 | kontrola Haiku projektu Fable: 36 znalezisk, 13 blokujących, wszystkie zastosowane jako addendum | `check.before_use` to działająca rutyna; brakuje jej zapisu biegu i miejsca w cyklu pakietu |
| `TELEPORT_S1.md` §5, `ZADANIA_ZAPLANOWANE_2026-10-23.md` | 5 narzędzi sesji z zamiennikami na VM, „Nic z tego nie było sprawdzone na VM”; zadania zaplanowane „NIE jadą same” | katalog narzędzi z dostępnością per maszyna (datowaną) i rutyna odtwarzania zadań — dziś dwie ręczne tabele |
| CLAUDE.md, kontrola marnotrawstwa 6 | „a running workflow agent cannot be updated — its prompt is fixed at start”; `resumeFromRunId` odtwarza tylko niezmieniony prefiks | korekta = rutyna `addendum.apply`, nie restart; wyniki etapów w plikach jako `args` → rutyny z wejściem/wyjściem w plikach |

## §3 Propozycje

Jeden mechanizm, nie dziesięć: rekord rutyny **`loom.routine/1`** ma ten sam kształt, co akcja złożona w `actions.pack` (id, wersja,
`steps` z `when`/`variants`/`inputs`, bramki, `measurements`, `record`), więc import do grafu apki (R89 P7) jest 1:1, a do tego czasu czyta
go cienki runner (`tools/routine.py` — nazwa przykładowa). Dom: **`tools/routines/*.json` w repo** (dane obok kodu `tools/workflows/`;
git = historia; teleport przenosi pliki; serwer apki nie jest dziś osiągalny z telefonu — obserwacja 23.10, R89 §4.3).

### 3.1 Co jądro już ma (obserwacja 23.10; pojęcie warsztatu → istniejąca abstrakcja)

| Pojęcie | W jądrze dziś | Uwaga |
|---|---|---|
| rutyna | akcja złożona: `actions.pack` `kind: composite`, interpreter `action_run.h` (kroki, `when`, `variants`, `inputs`, `$arg`) | „Mechanism vs data: the interpreter knows how to run steps; which actions have which steps, prompts, models and variants is data” |
| narzędzie | działanie atomowe; zewnętrzne przez `tool_sources.h` (MCP → jedno działanie na narzędzie, efekty nieznane = najgorszy przypadek, reguła `confirm:tool_source.unknown`) | ten sam wzorzec dla CLI repo i narzędzi sesji |
| pauza / zgoda | `action_rules.pack` `confirm`, paragon, bieg trwały (RunStore), wznowienie po dowolnym czasie | nic nie dzieje się po cichu |
| propozycja Tak/Nie | `agent.run` polityka run / propose / deny; propozycje łańcuchów (R43) „nothing here executes an action” | operator = `agent.run` z ograniczonym `actions` i polityką |
| zapis biegu | `loom.action_run/1`, kroki `loom.action_step/1` (hashe wejść/wyjść, proweniencja wywołań modelu) | wzorzec dla `loom.routine_run/1` |
| długie zadanie z checkpointem | `tasks.h` (pending → running → paused/done/failed, `recover_interrupted`) | bramka 40–90 min, agent 50 wywołań |
| maszyna / sesja / usługa | `environments.h` (`environment:runtime|service|directory|storage`, `capability`, `access` → `vault:secrets`) | chmura, VM, S2, GitHub jako węzły |
| słowa → działanie | `chat_ops.pack`, `intent_routing.pack`, reguła przed modelem (P34) | makra |
| koszt na paragonie | `action_estimate.h`, `economy.pack` (P38, stawki z datą i źródłem) | waluty — kąt ekonomii |
| filozofia jako dane | `loom/data/philosophy/principles.json`, `operators.json` (`loom.kb.operators/1`) | pakiet kontekstu może odwoływać się tu, nie tylko do `CLAUDE.md` |

### 3.2 Propozycje K1–K9

| # | Co | Dla kogo | Mechanizm ogólny | Przykład (jeden z wielu) | Jak mierzyć | Koszt | Odwrót |
|---|---|---|---|---|---|---|---|
| K1 | **Katalog narzędzi warsztatu** jako dane: jedno działanie na narzędzie — id, co robi, parametry, efekty (`reads_only`, pisze repo, pisze dane właściciela, kosztuje tokeny, … — lista otwarta), kto może uruchomić (zbiór otwarty: operator / koordynator / właściciel / …), klasa kosztu, dostępność per maszyna z datą obserwacji i zamiennikiem | koordynator, operator, nowa sesja (teleport) | jak `tool_sources.h`: „źródło” = CLI repo + narzędzia sesji; efekty nieznane = najgorszy przypadek (preset) + `confirm` | `spec.py {show,cite,propose,html}`, `pace.py`, `gate.sh`, `usage_report.py`, `clang_inc_check.py`, `lint_asan_range_for.py`, `vm_oneliner.sh`; sesyjne: `Workflow`, `create_trigger`, `send_message`, `list_events`, `Artifact`, GitHub MCP, `gh`; tabela TELEPORT §5 jest ziarnem | pokrycie: narzędzia użyte w QUEUE z 7 dni / wpisy katalogu; czas nowej sesji do pierwszego poprawnego użycia narzędzia | 1 pakiet Sonnet (~30 wywołań — preset) | plik to dokument; TELEPORT §5 zostaje |
| K2 | **Rutyny jako dane** (`loom.routine/1`): kroki = narzędzia z K1 albo inne rutyny; `when` (bramka zielona → FF), `variants`, warunki wstępne jako dane z powodem i datą (R84: wymusza je rzecz sama, np. dysk), pauzy = reguły `confirm` (preset: kto potwierdza), pola `operator_ok` i `judgement` | koordynator; operator; później jądro | interpreter z `action_run.h`; v1 = runner w Pythonie czytający ten sam JSON (renderuje prompt/komendę, zapisuje bieg) | `package.run` (dzisiejszy `single-package.js`), `check.before_use`, `gate.full`, `ci.read`, `ff.main`, `smoke.read`, `apk.release`, `pace.check`, `ledger.sample`, `secretary.run`, `addendum.apply`, `teleport.refresh`, `tasks.recreate`, `vm.keepalive`, `status.lines` | udział działań koordynatora w QUEUE będących biegiem rutyny vs ad hoc; liczba ręcznych briefów po wdrożeniu | 1–2 pakiety Sonnet | skrypty JS zostają wykonywalne; rutyna = sam opis |
| K3 | **Pakiet kontekstu** (uogólnienie jego przykładu): nazwana, wersjonowana lista ODWOŁAŃ rozwiązywanych przy starcie, z hashem w zapisie biegu; presety per rola (`podwykonawca`, `kontroler`, `operator`, `sesja-S1-VM`, …) | podwykonawca (agent), nowa sesja, operator | dziś: `CLAUDE.md` wstrzykiwane. Brak (pomiar §2): `spec:<id>` (0/21), dokument projektowy obszaru (12/21 ręcznie), decyzje INDEX obszaru (14/21 ręcznie), pomiary (nigdzie systematycznie), format karty (w skrypcie, nie w danych). Rozwiązanie: `spec.py cite --full`, sekcje dokumentu po nagłówku, wpisy INDEX po kluczu `S1 / <id>`, wiersze ledgera, schemat karty; agent może dobrać więcej (krok `context.more`, zapisany) | „cała filozofia do kontekstu” = `context_pack: filozofia` = `CLAUDE.md` (hash) + `spec:R58 R59 R60 R83 R84` + `principles.json`; `VM_START_PROMPT_2026-10-23.md` i `TELEPORT_S1.md` to dwie ręczne instancje pakietu `sesja` | tokeny prefiksu pakietu vs dzisiejszy brief (mediana 6,6 tys. znaków); znaleziska `check.before_use` na pakiet przed/po; `calls_used/plan_calls` (baza 2–5×) | 1 pakiet Sonnet | pakiet = dosłowny wklej (dzisiejszy stan) |
| K4 | **Szablon briefu jako rekord** `loom.brief/1`: dzisiejsze 8 pól (`wt, branch, remote_branch, id, model, scope, accept, rules_extra`) + `spec[]`, `design_doc`, `decisions[]`, `measurements[]`, `hypothesis`, `plan_calls`, `context_pack`, `card_schema`, `operator_ok`, `judgement` (lista otwarta); `brief.render` → prompt z częścią wspólną bajt w bajt (hash) i własną (R89 P4); `spec.py cite` jako krok walidacji; brief w repo PRZED startem (dziś praktyka `briefs/README.md`) | koordynator pisze, agent czyta, kontroler sprawdza | szablon = dane + render; walidacja = istniejące narzędzie | brief K2c-1 (9,9 tys. znaków) rozpada się na pakiet kontekstu `jądro`, odwołania `spec:`, 1 dokument, FAKTY jako datowane obserwacje i własną część ~2 tys. znaków | diff części wspólnej dwóch agentów = 0; hash w karcie; długość części własnej | w K2 | `args` jak dziś |
| K5 | **Cykl życia podwykonawcy jako rutyna z pomiarami** `package.lifecycle`: brief → `check.before_use(brief)` → `pace.check` → start agenta → karty → `check.before_use(dokument)` → `gate.full` → `ff.main` → `ci.read` → `smoke.read` → `apk.release`; każdy krok: warunki wstępne (≥ 5 GB wolne, jeden katalog buildu, jeden piszący na worktree, jedna bramka na maszynie — dane z powodem i datą z CLAUDE.md 23.10), pomiary (czas, `calls_used/plan_calls`, pauzy > progu, czerwone/zielone, powtórki, udział przepisanych kontekstów z `usage_report`), pauzy `confirm` (preset: FF na `main` potwierdza koordynator; push na gałąź dokumentów bez pytania) | koordynator, operator | rutyna złożona z rutyn K2; `when` na wynikach kroków (czerwona bramka → `gate.triage`: znane wzorce jako dane, np. „przestarzałe oczekiwanie e2e”, „timeout zadania dev” z QUEUE 23.10; nieznany wzorzec → `judgement` = koordynator) | pakiet M1-K2c-1 odtworzony jako bieg (brief w repo, karta, bramka, FF) | baza kosztu i czasu per krok z pierwszych 5 biegów; histogram przyczyn czerwonych bramek; udział kroków wykonanych przez operatora | 2 pakiety Sonnet | kroki ręcznie jak dziś |
| K6 | **Operator na tanim modelu**: uruchamia rutyny z `operator_ok` i twardym kryterium odbioru; wszystko z `judgement` albo nieznanym wzorcem → propozycja do koordynatora (nic nie wykonane) | koordynator oszczędza tury; właściciel — tańsza godzina pracy | w jądrze już jest: `agent.run` z polityką run / propose / deny i podglądem „co by uruchomił” (`tools`); v1 = zadanie zaplanowane „Operator” (świeża sesja Sonnet, jak „Sekretarz spec”) czytające kolejkę żądań rutyn (`routine_requests` w bazie strony albo plik w repo — wzorzec `spec_inbox`); routing po cechach sytuacji z rekordem (R89 P8) | `operator_ok` (przykłady): `pace.check`, `gate.full` + odczyt, `ci.read`, `ff.main` gdy zielono i reguła pozwala, `smoke.read`, `secretary.run`, `ledger.sample`, `check.before_use`, `card.write`, `brief.render`, `teleport.refresh`, `tasks.recreate`, `vm.keepalive`, `log.excerpt`, `status.lines`; `judgement` (przykłady): projekt, zakres pakietu, triage nieznanej czerwieni, sprzeczność z filozofią, decyzja z wysokim kosztem odwrotu | tury koordynatora na pakiet (baza ≈ 20 × 0,28 mln) przed/po; liczba eskalacji; błędy operatora na 10 biegów; koszt pustego biegu (jak P1) | 1 pakiet + pomiar | zadanie wyłączone → koordynator uruchamia rutyny sam |
| K7 | **Zapis biegu** `loom.routine_run/1` (≈ `loom.action_run/1`): rutyna id + wersja + hash, parametry, hash pakietu kontekstu, start/koniec, kroki ze stanem, hashe wejść/wyjść, commity, karty, koszt (waluty — kąt ekonomii), decyzje z `reversal`, eskalacje, advisories, `original_ideas`; v1: `docs/reports/runs/<rok-mies>/<run_id>.json` + indeks `runs.jsonl` na gałęzi dokumentów (`sekretarz_runs.jsonl` z P1 = pierwsza instancja, scalić); **linie statusu w QUEUE i czacie = render** (`status.lines`: started / finished / building / failed / original_ideas — jego lista ze stylu wątku); v2: węzły grafu `routine_run --part_of--> package --implements--> requirement` | właściciel (status), koordynator, nowa sesja | ten sam dokument biegu, który interpreter pisze dla akcji; dziennik → render, nigdy odwrotnie | 37/60 commitów księgowych → jeden commit biegu z wygenerowaną sekcją QUEUE | linie QUEUE pisane ręcznie vs generowane; czas nowej sesji do odpowiedzi „co biegnie” | 1 pakiet Sonnet | proza QUEUE jak dziś |
| K8 | **Wersjonowanie i odwrót rutyn**: `version`, `since`, `supersedes`, `changelog[{date, who, why}]`, presety w sekcji `presets` z `_history` (wzorzec `pace_plan.json`); bieg nazywa wersję; zmiana presetu nie zmienia starych zapisów; stara wersja zostaje (nic nie znika); rutyny i szablony przechodzą `check.before_use` jak briefy | koordynator, kontrola | git + pola rekordu; jak `understanding.history` w spec | zmiana progu pauzy ~4 min → wpis `_history`; biegi sprzed zmiany bez zmian | liczba wersji per rutyna; biegi na nieaktualnej wersji (advisory, nie błąd) | w K2 | — (git) |
| K9 | **Widok „Warsztat”** = perspektywa od strony pracy: rutyny (przycisk „uruchom” → `routine_requests`), biegi (stan, koszt, co czeka), narzędzia (dostępność per maszyna, datowana), „czeka na koordynatora”; **makra** = słowa/przyciski → rutyna z parametrami (dane jak `chat_ops.pack`/`intent_routing.pack`; reguła przed modelem, P34) | koordynator, operator; właściciel widzi filtr z R89 §4.4 | v1: sekcja/strona generowana z danych obok strony spec (ten sam wzorzec cienkiego renderera; baza strony jako skrzynka); v2: perspektywa grafu po P7 | makra (przykłady): „odpal bramkę”, „status”, „co czeka”, „nowy podwykonawca na <spec>”; przycisk „Zatwierdzam” ze skrzynki R88 to już makro → `spec.py approve` | żądania rutyn ze strony vs z czatu; trafienia/chybienia tabeli słów | 1 pakiet Sonnet | strona niegenerowana; dane zostają |

### 3.3 Przykład rekordu rutyny (przykład; każda wartość to preset albo ilustracja kształtu)

```json
{ "schema": "loom.routine/1", "id": "package.lifecycle", "version": 1, "since": "2026-10-23",
  "title": {"pl": "Cykl życia pakietu podwykonawcy"}, "judgement": ["scope", "red_gate_unknown"],
  "params": {"brief": "loom.brief/1", "wt": "path", "model": {"preset": "sonnet", "_history": []}},
  "context_pack": "podwykonawca",
  "preconditions": [{"check": "disk_free_gb >= 5", "why": "restart kontenera przy ~95 % dysku", "since": "2026-10-23"},
                    {"check": "build_dirs_on_machine <= 1", "why": "jak wyżej", "since": "2026-10-23"}],
  "steps": [
    {"id": "check_brief", "action": "check.before_use", "with": {"doc": {"$arg": "brief"}}, "operator_ok": true},
    {"id": "pace",   "action": "pace.check",   "when": {"step": "check_brief", "status": "ok"}, "operator_ok": true},
    {"id": "agent",  "action": "package.run",  "with": {"brief": {"$arg": "brief"}},
                     "measurements": ["calls_used/plan_calls", "duration", "pauses_over_threshold", "rewritten_context_share"]},
    {"id": "gate",   "action": "gate.full",    "when": {"step": "agent", "output": "card.done"}, "gate_as": ["one_gate_per_machine"], "operator_ok": true},
    {"id": "triage", "action": "gate.triage",  "when": {"step": "gate", "status": "red"},
                     "variants": [{"known_patterns": "tools/routines/gate_patterns.json"}, {"escalate": "coordinator"}], "mode": "first_success"},
    {"id": "ff",     "action": "ff.main",      "when": {"step": "gate", "status": "green"}, "confirm": {"preset": "coordinator"}},
    {"id": "ci",     "action": "ci.read",      "when": {"step": "ff", "status": "ok"}, "operator_ok": true},
    {"id": "smoke",  "action": "smoke.read",   "when": {"step": "ci", "status": "green"}, "operator_ok": true},
    {"id": "apk",    "action": "apk.release",  "when": {"step": "smoke", "status": "green"}, "confirm": {"preset": "owner_if_cost_to_reverse_high"}}
  ],
  "record": {"path": "docs/reports/runs/", "render": ["status.lines", "queue.section"]},
  "presets": {"plan_calls": 50, "pause_threshold_min": 4, "_history": []} }
```

Pakiet kontekstu (przykład): `{"id": "podwykonawca", "version": 1, "refs": [{"file": "CLAUDE.md", "hash": true}, {"spec": ["R58","R59","R60","R83","R84"],
"via": "spec.py cite --full"}, {"doc": "$brief.design_doc", "sections": "$brief.sections"}, {"index": "S1 / $brief.id"}, {"ledger": "last 3 rows"},
{"schema": "loom.card/1"}], "more": "allowed, recorded"}` — żaden tekst nie jest kopiowany; przy starcie rozwiązuje się odwołania i zapisuje hash.

### 3.4 Decyzje tego kąta (koordynator, 23.10) i pierwszy krok

| Decyzja | Dlaczego | Odwrót |
|---|---|---|
| Dom rutyn = `tools/routines/` w repo; kształt = akcja złożona z `actions.pack` | KOD ≠ DANE; git = historia; teleport przenosi pliki; import do grafu 1:1 (P7) | osobny format (koszt: własny importer) |
| Najpierw dane + cienki runner; jądro przez import P7 i port `tool_source` | jedyna maszyna buildu jest zajęta pakietami M1; dokument i skrypt są tanie do wycofania (R75) | od razu w jądrze (koszt: build, bramka) |
| Operator = zadanie zaplanowane na wzór „Sekretarza spec”, nie nowa stała sesja | świeża sesja = mały kontekst (nasze-01); zmierzone: zadanie już działa | stała sesja operatora (koszt: kontekst do cache’owania) |
| Linie statusu generowane z biegów | 37/60 commitów to księgowość Fable; rodzaje linii, które sam wymienił (lista otwarta), to gotowy schemat | ręczne, jak dziś |
| Wszystkie progi (50 wywołań, ~4 min, 5 GB, 1 recenzent, częstość operatora) to presety z `_history` | R83/R84, R63 (progi per model koordynatora) | — |

**Pierwszy krok (najtańszy — preset kolejności):** K1 + K3 (katalog narzędzi + pakiet kontekstu): dokumenty i jeden skrypt, zero jądra,
mierzalne na następnym pakiecie (`spec:` w briefie z 0/21 na 1/1; prefiks z hashem; znaleziska kontroli przed/po). Potem K7 (zapis biegu;
scala P1), K2/K4, K5, K6, K9. Każdy pakiet ~50 wywołań (preset), z kartą i pomiarem „po”; brak pomiaru = advisory w karcie, nie blokada.

## §4 Czego nie robić i dlaczego

| Nie | Dlaczego |
|---|---|
| Nie budować osobnego „orkiestratora” z własnym modelem danych | KOD ≠ DANE i R89 §6: interfejs = perspektywa grafu; jądro ma interpreter, bramki, paragony, propozycje, zadania, środowiska — drugi silnik to drugi zestaw błędów i synchronizacja |
| Nie wpisywać przypisania rola → model do rutyny na stałe | R88 „żonglerka modelami”, R63 „poluzować” przy lepszym modelu: przypisanie to rekord z datą i terminem odświeżenia (R89 §4.2) |
| Nie przenosić reguł z `CLAUDE.md` do briefów (wklejanie) | pomiar: 20/21 briefów powtarza „CACHE RULE”; każda poprawka reguły to 21 edycji; odwołanie + hash zamiast kopii (kontrola marnotrawstwa 6b) |
| Nie pozwalać rutynie wykonać kroku piszącego dane właściciela, pushującego na `main` ani kosztującego ×10 bez reguły `confirm` | zasada ×10 (CLAUDE.md, 2026-09-30), R75 „tania do wycofania”; pauza z paragonem już istnieje w interpreterze — użyć jej |
| Nie restartować agenta po korekcie; nie robić z korekty nowego briefu | nasze-08: 3 restarty ≈ 0,9 pkt; rutyna `addendum.apply` po karcie (CLAUDE.md 6c) |
| Nie dawać operatorowi decyzji o sprzeczności z filozofią ani o nieznanej czerwieni bramki | R88: nietypowe do drogiego modelu; routing po cechach z rekordem i przyciskiem „jednak do Fable” (R89 P8) |
| Nie zamykać list: kroków cyklu, ról, efektów, walut, pól briefu, „kto może uruchomić” | R83/R84; każda lista wyżej jest przykładem; enum tylko tam, gdzie wymusza go interpreter (stany kroku) — z powodem i datą |
| Nie usuwać starych wersji rutyn ani zapisów biegów | nic nie znika po cichu (R89 §6); odwrót = `supersedes` |
| Nie trzymać zapisu biegu tylko w kontekście sesji ani w scratchpadzie | R68/TELEPORT: scratchpad ginie z maszyną; 21 briefów ocalało tylko dlatego, że je skopiowano do repo |
| Nie uruchamiać dwu bramek ani dwu katalogów buildu na jednej maszynie | CLAUDE.md 23.10: dwa restarty kontenera — warunek wstępny jako dane z powodem (dysk), nie wiedza plemienna |
| Nie liczyć oszczędności liczbami z sieci | R59: tylko własne pomiary; każda rutyna ma `measurements` i bazę „przed” |
| Nie generować linii statusu, gdy nie niosą nowości | R62: jego czas czytania; `status.lines` renderuje tylko zdarzenia zmieniające stan (preset) |

## §5 Pytania do dwóch pozostałych kątów

Do kąta właściciela:
1. Które pauzy biegu rutyny mają trafiać na jego stronę („Czeka na Ciebie”), a które tylko do koordynatora? Propozycja: klasa decyzji ×
   koszt odwrotu jako dane, z jego `if_silent` (R75). Czy chce widzieć biegi (K7) w ogóle, czy tylko ich render do rodzajów linii, które sam wymienił (lista otwarta) — ze
   stylu wątku?
2. Makra z telefonu: czy jedno dotknięcie „odpal bramkę” / „nowy podwykonawca na <R-id>” ma być dostępne jemu (z paragonem kosztu przed
   startem), czy tylko koordynatorowi? Jakich słów on sam używa (z cytatów) — tabela słów K9 ma wyjść z jego języka, nie z moich nazw.
3. „Co pamiętasz” (R73) na poziomie pracy: czy widok rutyn i biegów jest tym, czego chce, gdy pyta „co robisz / co biegnie”, czy to osobny
   widok?

Do kąta ekonomii i kanałów:
4. Pola kosztu zapisu biegu (K7): jakie waluty na paragonie biegu (tokeny/punkty, czas czytania właściciela, czas maszyny, dysk, …) i jak
   przypisać próbki `rate_limit_event` całego konta do pojedynczych biegów (dziś: tylko tempo sumy, `LIMITS_LEDGER.md`)?
5. Gdzie biegnie operator (K6): świeża sesja w chmurze (ma `create_trigger`, `send_message`, MCP GitHub) czy `cron` + `claude -p` na VM
   (nic z narzędzi sesji nie sprawdzone na VM — TELEPORT §5)? Czy `vm.keepalive` (30 min bezczynności, R90) to rutyna operatora, czy
   ustawienie instancji — i kto mierzy, że działa?
6. Baza „przed” dla K5/K6: ile tur koordynatora idzie dziś na kroki mechaniczne (jeden pomiar ≈ 20 tur × 0,28 mln) — czy ledger ma już
   rozbicie na tury, czy to pomiar do zrobienia w P1/P3?
7. Teleport: czy zapis biegów na gałęzi dokumentów (commit per bieg) jest akceptowalnym kosztem (push, CI dokumentów), czy wolisz
   `runs.jsonl` dopisywany bez commitu i commit zbiorczy co N biegów (preset)?
