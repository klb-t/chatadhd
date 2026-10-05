# STATE — ChatADHD / Loom

Updated 2026-10-04. **The owner authorized autonomous development and integration
of the application profile cycle described below.** Claude retains the broader
next-cycle handoff outside this owner-requested scope.
The owner additionally requested cheap-model analysis experiments in this
recovery session. [432 paired requests are prepared](research/analysis_optimization_2026-10-02/README.md),
with zero new model calls; restoring this programme's credential is the live
execution blocker. This temporary research task is distinct from a permanent
GPT integration/helper assignment.
**The recovered base is published on `main` in the existing public
`klb-t/chatadhd` repository, together with the verified profile increments.**
The first integration `ab581923` directly continues original main `b5f7eac`
with one parent; N3 is a later increment on that line. The 40 original
main-line commits retain their identities. Read
[the Claude handoff](HANDOFF_2026-10-02_TO_CLAUDE.md).

## Source and ownership

- Accepted source: recovered checkpoint `af3c81a5ddce70bdf4346a2c8c808ce6cc97b4d0`,
  containing night development and the compatible late-W1/test repairs.
- Original main is `b5f7eacdead11101e5c2a08e1864195c74034d45`. Selected
  content directly continues that original public history with one parent,
  retaining all 39 original paths. Verified complete backups also preserve
  every original branch. A separately prepared local metadata-corrected
  bundle is an optional derivative, not this hosted main's ancestry.
- [Archive and exact selection](archive/README.md) retain full source,
  first failures, unused experiments and original W3/W5 Git bundles.
  Completed hosted branches were renamed under `archive/`: 33 renames,
  35 archived branches in total, all original tips preserved. Four active or
  supporting branches and two isolated evaluation branches remain separate.
  No remote branch has been deleted or force-pushed by this recovery.
- The owner-requested profile cycle is complete. No GPT helper has an ongoing
  assignment from this recovery; Claude chooses any broader later helper's
  scope, branch, deadline and integration boundary.
- The owner explicitly keeps the existing repository public. Historical
  author/committer metadata still contains the contact finding in 26 commits;
  this integration does not rewrite or hide it. The selected source contains
  no confirmed private-content finding in the scoped audit. See
  [the privacy audit](PUBLICATION_PRIVACY_2026-10-02.md).
  No private conversation/export or credential is a publication input.
  No new paid provider calls were made.

## What is integrated

### Owner-requested application profile increment (2026-10-04)

The first increment directly continued public main
`421415f8b9a5c29e25fc4c5fda5b87ba8f5fb9cd` and was integrated as
`9d15d2dd0733274356f768816e207e26e13845e4` through
[PR8](https://github.com/klb-t/chatadhd/pull/8). The autonomous extension follows
that commit through [PR9](https://github.com/klb-t/chatadhd/pull/9).
Its measured implementation commit is
`95e5049382a6025ca0c591e9b6dba49f90af3946`; subsequent documentation commits
retain that implementation. Main advances without rewriting prior history.

[Application profile contract and handoff](APPLICATION_PROFILES.md) describe
versioned JSON profiles, simultaneous views, actual Loom adapters and declarative
workflow inputs/result bindings. Revision-1 examples remain intact; revision-2
workflows retain created conversation IDs. Separate source-backed profiles pin
LibreChat 0.8.8 and NextChat 2.16.1. These are partial mappings; original service
and arbitrary historical-version fidelity are not established.

Imported source blocks are inspectable separately from current edited text,
including excluded rows and saved versions. Exact valid UTF-8 profile source,
including BOM/whitespace, can be explicitly accepted through the existing native
GraphPacket store, read back with receipt/drift checks and restored after client
storage loss. Existing core/KB schema and C ABI are unchanged. Native object
comparison now ignores key order but preserves arrays and exact numeric values;
unauthorized POST responses close the connection to prevent reused-body failures.

Changing a view preserves explicit model/context controls. Saving a profile
creates a completed canonical knowledge run, so automatic latest-run queries
may select it; that write effect is disclosed in the UI and contract. Android
JNI storage, specialized media/artifact views, complete descendant navigation,
individually queryable capability links and durable server workflow recovery
remain explicit gaps. Local view/workflow persistence is not cross-device sync.

Fresh measured checks are recorded in
[the extended verification](verification/application-profiles-extended-2026-10-04/RESULTS.md):
108 CTest entries covered across 106 + 2 stages, native-store FFI 18/18,
Python contracts 209/209, runtime 19/19, adapters 9/9, native graph 11/11,
profile browser 15/15, native imported-source display 4/4 and existing browser
16/16. The earlier prototype and historical native measurements remain separate.
No paid provider calls or GitHub Actions runs were used.

### Previously integrated on public main

| Area | Current behavior and boundary |
|---|---|
| W1 / N1 — task acceptance | Explicit ActiveTaskSpec reaches real chat. Acceptance and its user row share the existing EventLog transaction. Exact locator/time ordering, durable source checks, exceptional/C ABI cancellation cleanup, large revision/timestamp tests, history ABA and scope identity fixes are included. The incompatible second W1 journal is archived. This is not automatic task inference from prose. |
| W2 / N2 — retrieval | Per-thesis scope/detail, claims/counters and graph/TF-IDF channels reach actual chat. Exact built-in TF-IDF corpus vectors are reused across plan queries. Plan provenance is caller-declared; verified W1→W2 plan authority is still missing. |
| W3 — recipes | `directed_refute_v3`, 48 prepared DEV requests and historical-response replay are retained. No fresh live model-quality measurement is claimed. |
| W4 / N3 — graph workflow | Python AnalysisPlan, GraphPacket transforms and durable coordination work. Explicitly selected entities/claims/observations now reach native KnowledgeStore atomically, with immutable full-packet receipts, owner judgement replay, CAS and checked readback/replay. Native CandidateGraph remains distinct; full reversible-history validation remains in Python. See the [store contract](NATIVE_GRAPH_PACKET_STORE.md). |
| W5 — workspace | Native views/references/couplings and browser-local perspective restoration work. Plan execution remains an explicit user choice. This is not cross-device perspective synchronization. |
| W6 / N4 — import | Source-array and traversal order stay distinct; parent bindings, actual-root pointers, wrapper fields and member indices are retained. ZIP raw member materialization and the large-whitespace sniff fix are included. Raw source remains preserved. |
| N5 / N6 | Historical resource peaks survive reservation release/crash boundaries. Latest completed native knowledge-run lookup no longer loses an older completion behind many unfinished runs. |

Existing chat context, provenance, semantic metadata preservation, confidence,
workspace contracts, ABI and import behavior remain subject to regression gates.
Python compatibility is no longer a product requirement, but existing sentinels
remain until a deliberate migration. N3 adds `loom_graph_packet_store` to the
C ABI and migrates KB schema 2→3 with a receipt table; core schema remains v4.
Existing KB records are preserved by the tested migration. The optional W3 source-record
fields `resolved_commit` and `git_tree_sha` also permit exact historical byte
verification in the separately prepared metadata-corrected bundle. The public
repository retains the original commit identities.

## Fresh verification

The latest profile-cycle measurements and exact source/binary hashes are in
[verification/application-profiles-extended-2026-10-04/RESULTS.md](verification/application-profiles-extended-2026-10-04/RESULTS.md).
The preceding recovery measurements remain pinned to their source in
[verification/current-2026-10-03/RESULTS.md](verification/current-2026-10-03/RESULTS.md):
108/108 CTest entries and 13/13 actual native-store FFI regressions. The initial
15 environment failures and their passing rerun are retained. Previous web
verification remains attached to its unchanged web source. Historical 77/77, 94/94 and 851-test
claims are not transferred to the recovered source.

## Research and money

[Research/cost audit](RESEARCH_AND_COSTS_2026-10-01.md) separates retrieval,
extraction, commitment policy, replay and live inference. BM25 52/60 evidence
ranking and Jev 45/48 supplied-candidate selection are different DEV tasks;
neither establishes free graph extraction quality or a general model ranking.
Free graph extraction retains its negative first results in the archive.

992 saved ledger rows deduplicate to 631 attempts. All 629 available response
hashes and 628 usage-cost values reconcile; three costs remain unknown.
The campaign arithmetic is **$0.133931438** including the opening balance.
A later saved key reading is **$1.23206716**, with **$1.098135722 unassigned**;
therefore $0.134 is not certified total account spend. These are historical,
not current balances. The authorized cheap/Jev programme has a non-resetting
**$2** limit; a new conversation does not reset it. No new frontier pilot is
authorized. Reconcile live identity, usage and outstanding reservations before
resuming paid work.
The recovered credential envelope expired on 2026-10-01. The required private
decryption/session artifacts are unavailable; its expiration is not bypassed.
The 432 requests remain unexecuted pending a valid authorized credential and
fresh account reconciliation.

## Open engineering and evidence boundaries

The complete [limits and wiring inventory](LIMITS_AND_WIRING_2026-10-01.md)
is a source audit, not a claim that all limits have been removed. Its line
references pin `33fb30a`; the recovered delta and final tests are described
in the handoff. Priorities for Claude to scope are:

1. Convert hard product/resource ceilings into explicit caller policy/presets;
   implement expected ×10 usage confirmation end to end. Preserve necessary
   representation and transaction invariants, and preserve unknown source data.
2. Connect model goal typing and a real vector provider to production callers;
   provide candidate-channel controls and capability reporting in the UI.
3. Connect checked task→retrieval provenance and scope any broader native
   GraphPacket history/transformation support. N3 acceptance/read/replay is
   delivered; it does not replace Python's complete history validator.
4. W1 callback exceptions after output still do not retain a partial assistant
   row; many distinct lineage sources can produce quadratic provenance payload.
5. Recipe efficacy, real full-export/Anthropic semantic quality and independent
   evaluation remain unproved. Do not use the sealed holdout as DEV.

`gpt/ecosystem-agent-research-2026-10-01` / draft PR7 is an isolated Python
prototype, not a production service or a launched cloud VM. The archived `wip/precision`
is an unverified historical experiment. Neither is silently promoted.
The original `semantic_sketch` and shared-lease `c26c525` bytes remain missing;
later replacements do not recover those originals. Full transcripts of all
36 historical child agents are unavailable. See the bounded
[recovery inventory](CONVERSATION_RECOVERY_2026-10-01.md).
The additional bounded search of 68 recent saved files on 2026-10-03 found no
missing originals or complete child-agent transcripts.

## Integrator9 — równoległy cykl1–11 (2026-10-04)

Baza źródła: `161cc22dfb84fe863389d6b90323bd44516a68dc`, z całym INTERFEJS/PR9.
Ten checkpoint dodaje wyłącznie stan i raporty integratora; **0 gałęzi kodu
przyjętych**. Wstrzymane implementacje nie są przodkami tego przyrostu.
Dotychczasowe wpisy i pliki profili pozostają zachowane.

Aktualny [indeks11 wątków](reports/INDEX.md) przypina sprawdzone commity,
raporty, konkretne błędy oraz przekazania „Do wątku N”. Inwentarz11:
695 grup w559 plikach,39496 kandydatów mechanicznych; to zakres migracji,
nie liczba wdrożonych zmian. Ponownie przydzielono1 prompty/metody w grafie,
a2 presety config/startup. Odbiór zadań przez autorów nie jest potwierdzony.

Najnowsze polecenie właściciela obowiązuje w tym cyklu: metody, wersje,
przepisy/prompty z hashem, parametry, presety i kombinacje są bytami w grafie;
wyniki mają krawędź do konkretnej wersji metody, a oceny są datowanymi
twierdzeniami z dowodami. **Wspólny kontrakt3/4 i regresja między ich API są
warunkiem odbioru przed scaleniem**. Domyślne metody/polityka pochodzą z danych
packa/profilu, z nakładką użytkownika; nie z ręcznych presetów C++.

W2 ma zielone niezależne bramki poprzedniego przyrostu (19/19 kontraktów,
108/108 CTest,659 native/1276 Python, web85 modułów), ale nadal nie spełnia
wymogu presetu jako danych. W4 iW5 po ostatnim fetch dostarczyły
poprawki wykazanych błędów replay/checkpointu; przegląd kodu je potwierdza,
ale niezależny rerun nowych regresji oraz bramki po rebase pozostają otwarte.
W4 opublikował packet-side format metod, którego W3 jeszcze nie produkuje.
W7 naprawił wykazany orphan-response błąd, lecz nie ukończył całego zakresu.
W6 sam wstrzymał checkpoint po nieprzejściu jakości; negatyw jest w archive.
W8 dev/ASan receipts są kompletne, vendored Clang wymaga poprawki w10.

Osobny build/web/fullCTest czystej linii161cc22 z tymi dokumentami:
**zielony:108/108 CTest,659 native/24465 assertions,1276 Python/0 skips;
web85 modułów**. [Oryginalne dowody](reports/integrator-main-verification-2026-10-04/README.md)
obejmują pełny przebieg351.36s oraz1276 rzeczywiście wykonanych przypadków Python.
[Raport9](reports/integrator-2026-10-04.md) rozdziela wyniki różnych źródeł.
Kolejność kodu pozostaje2→3/4/5→reszta. README czeka na odbiór zmian;
szkic8 nie jest dowodem ukończenia aplikacji.

Płatne badania tylko w7 w osobnym budżecie5€ i na kluczu z limitem5€;
integrator wykonał0 płatnych wywołań. Nie czytał ślepego korpusu ani
`eval/real-holdout-key`; starsze zapisy badań powyżej pozostają historią.

## Integrator9 — wznowienie i R39–R41 (2026-10-04, druga sesja)

Powyższy stan7282437 jest historyczny. Dokumentacyjny przyrost Claude62cfd3a
(R39–R41,48 linii) przyjęto po czystym rebase jako ba6eaf6, bez cofania INTERFEJS.
Pełny CTest108/108315.71s, guard659 native/24465 assertions/1276 Python0skip
i web85 zielone; [dowód](reports/integrator-requirements-verification-2026-10-04/README.md).

**Przyjęto pierwszy przyrost2: usage policy z autorytatywnych danych**. Autorytatywny usage_policy.pack i checked decoder zastępują
wszystkie6 ręcznych wartości C++; zgodne5 default paths i config overlay/restart.
Niezależny actual129-object build,25/25 kontraktów,5/5 generatora,4/4 source-edit
i bad-preset variants; 108/108265.33s;107 executed,659 native/24465 assertions,1276 Python0skip; web85.
[Dowód odbioru](reports/integrator-usage-acceptance-2026-10-04/README.md).
Stare próby i pierwszy nowy107/108 (lokalny CLI0644, hash bez zmian po0755)
zostały w archive; pełny nowy przebieg nie jest sumą pojedynczych retry.
Startup/config0/5, shared public export i dalsze profily nie są tym odbiorem.

3 ma grafowy rejestr/wykonanie/eksporter golden;4 ma jeden kontrakt i konsumenta.
Golden i natywny consumer PASS są w32e2381; raw producer1/97 i pełny121/1604 też zapisane; acknowledgment4
i mixed gates nadal wymagane. Autorzy mogą wykonać je offline
na obecnym2, bez czekania na wzajemny main. Pierwotny P1 replay4 niezależnie
przeszedł: drugie wykonanie false, ledger calls=1/unresolved;
[dowód](reports/integrator-packet-fix-2026-10-04/README.md).2/3/4 wypchnęły nowe refs w tej sesji;
nie stwierdzono przerwy≥2h; INDEX rozdziela committer time od obserwacji fetch.

5 checkpoint naprawiony i niezależnie sprawdzony (2 scenariusze/29 kontroli),
ale nowy actual mock replay wykazał format OCR utracony przy immutable blob:
MIME1/3, dwa błędne import paths. Negatyw wyłącznie w archive;5 nadalwstrzymany
do poprawki i mixed gates. Raport/runbook5 już istnieją.11 opublikował loader,
CLI profile i naprawił materialize; dawne blockers zastąpiono aktualnymi zadaniami.
8/10 Clang nadal niezielony.6 dalej tylko DEV, blind nie używany ponownie.

[INDEX](reports/INDEX.md) i [raport9](reports/integrator-2026-10-04.md) zawierają
aktualne przekazania R39–R41 i właściwe commity. Kolejność kodu2→3/4/5→reszta.
README czeka na odbiór funkcji; kod innych wątków nie jest dopisywany przez9.
Nowe płatne wywołania0;7 dokańcza offline i czeka wyłącznie z płatnym wykonaniem
na osobny klucz5€ przekazany przez właściciela.

## Integrator9 — przyrost3+4 (2026-10-05)

Powyższe wpisy pozostają historią. Przyjęto przyrost3+4, testowane źródło `a1be689dc026367783473133de3c25cf5ae60971` po rebase.
Pełny CTest110/110 (149.03s), guard663 native/24489asercji, 1303Python/0skip; webPASS.
Wspólny kontrakt METHOD_GRAPH i kanoniczny golden uzgodnione. Integrator: registry/wykonanie/ślad121/1604, canonical+freshnativeconsumerPASS, alias/nested/nested+alias3/3PASS, KBwindowPASS. 3 czystyrebase na30ad7d3; bez merytorycznych zmian. Historyczne negatywy4 pozostają na archive; domyślneprofile/legacywiring to dalszyprzyrost.
[Dowód9](reports/integrator-intake-2026-10-05/README.md); [bieżący INDEX](reports/INDEX.md). Cały INTERFEJS i wcześniejsze wpisy zachowane;0paidcalls.

## Integrator9 — przyrost 5 (2026-10-05)

Przyjęto po rebase; testowane źródło `0a81480bd1a70b394bac72e98f6b5e7cb49f5d0b`. Pełny CTest 117/117 (225.60s), 710 native / 26399 asercji, 1322 Python / 0 skips; build web PASS.

31wybranych źródeł bez zmian merytorycznych po rebase; OCR3/3 MIME i22/22kontrole, checkpoint2/2scenariusze i31/31kontrole na rzeczywistym kernelu. Forward-only adnotacje i audit przyjęte; runbook autora przypięty w INDEX. Pierwsze timeouty zachowane w archive, progi bez zmian.

[Dowody](reports/integrator-intake-2026-10-05/README.md) i [INDEX](reports/INDEX.md). Zachowano cały INTERFEJS i wcześniejsze wpisy; zero płatnych wywołań.
