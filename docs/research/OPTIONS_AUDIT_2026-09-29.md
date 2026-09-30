# T8 — options / policy audit (2026-09-29)

Initial read-only audit of `loom/src/context/`, `loom/src/chat/`, `loom/server/` and
`loom/web/src/`. Source inspected on the current development line after
`5816b557`, before the verified context follow-up below. Original line references
identify that audit snapshot; they are historical where the follow-up changes a
file. No model request, secret read or forbidden-branch read was made. The
findings below are **static source evidence**, not measured model quality or a
runtime security audit.

[U] R21 requires coordinated views, complete interpretation and owner options;
R25 requires complementary retrieval channels without an implicit veto; D1
removes Python parity as a reason to freeze policy. I1–I9 and the conceptual
model's closed sets still bind. [P] Names below are proposed keys, **not existing
or implemented configuration**. A provider capability/profile is not a model
accuracy score. Values and recipes need provenance, versioning and evaluation.

## Findings

**23 grouped findings**; this is a count of actionable issues/policy families,
not a count of literals or a completeness claim for the entire repository.
P0 = prevent hidden external calls; P1 = fidelity, coverage or effective owner
control; P2 = useful next-stage policy separation. A defect is not repaired by
adding a switch that permits incorrect behaviour.

| ID / priority | Grounded choice or missing control | Evidence (file:lines) | Candidate data/profile key or required repair |
|---|---|---|---|
| O01 / P0 | A knowledge context **preview can make an external model request**, despite the UI promising otherwise. Low cue confidence `<0.55`, a semantic model and a key are sufficient; the local path has no explicit request budget/consent guard. | `loom/web/src/components/KnowledgeWorkbench.tsx:479,491`; `loom/web/src/api/loom-http.ts:43`; `loom/server/src/app.cpp:942`; `loom/src/context/context_engine.cpp:287–315`. | `policy/context.goal_typing.external_calls = off` by default; explicit operation budget and permission reference, cached first responses. Preserve an offline preview and report requested/actual capability. Merely removing the UI promise is insufficient. |
| O02 / P1 | The knowledge context builder does **not compile the outgoing chat**. Chat sends configured system prompt, active memory, legacy graph context, then every active history message. There is no ContextEngine/ActiveTaskSpec composition in this path. | `loom/src/chat/chat_engine.cpp:202–246,323–339`; independent builder `loom/src/context/context_engine.cpp:877–880`. | `policy/request.context_recipe` plus `policy/request.history_selection`; explicit legacy/simple and knowledge-compiled recipes with a recorded request snapshot. This is an integration gap, not an unused parsed `ContextRequest`. |
| O03 / P1 | Knowledge reach is fixed to claims one edge from supplied target entities, in both directions. The first target becomes the project if no project is supplied. Free-text target resolution, scope expansion and per-item detail controls are absent; token budget alone does not provide R28 scope/detail. | `loom/src/context/context_engine.cpp:388–392,573–621`; parsed fields `142–156`; goal resolutions `95–101`; UI exposes text/target/budget only at `loom/web/src/components/KnowledgeWorkbench.tsx:479,488–490`. | `policy/context.retrieval.{seed_resolution,scope,channels,relation_filters}` and request `scope`/`detail` overrides. Union supported channels with per-channel explanations; do not turn a lexical miss into an exclusion. Preserve current one-hop recipe as baseline. |
| O04 / P1 | Ranking embeds relevance coefficients (`0.6/0.5`, `0.35/0.65`, role-specific constants), a multiplicative objective, subject repetition penalty `0.8`, freshness half-life `365` days and undated score `0.6`. Budget defaults `4000` and missing-band shares `0.15/0.25/0.6` are also literals. These are tunable selection recipes, not universal mathematical constants. | `loom/src/context/context_engine.cpp:104–108,115–126,152,362,451,499–500,539–540,566–567,605–606,631–634`; `loom/src/context/ctx_common.h:36`; `loom/src/context/ctx_common.cpp:58–65`. | `policy/context.scoring.{recipe,weights,diversity,freshness}` and `.budget_defaults`. Existing goal-type budget/resolution/role/evidence settings remain data and must not be duplicated. Compare coverage of required premises and counter-evidence, then precision, against the fixed baseline. |
| O05 / P2 | Cue typing counts substring hits, rewards multiword cues by `1 + 0.4*(words-1)`, breaks ties by ID and defaults to `answer_question` with `0.25` confidence. Optional LLM typing fixes recipe text, temperature `0`, output limit `100` and timeout `15000`; the returned confidence is the model's assertion, not measured calibration. | `loom/src/context/context_engine.cpp:189–243,295–314,323–331`. | `policy/context.goal_typing.{cue_recipe,fallback_type,external_recipe}`; measured per-task ModelProfile separate from raw classifier score. Exact IDs/cues already live in `goals/goal_types.json`. Test cue substrings (e.g. `add` inside an unrelated word), ties, no match and paraphrases before promoting a new recipe. |
| O06 / P1 | Premise closure stops after **three rounds**. Premises not reached after that cap never enter `to_pull`, so the existing missing-premise marker is not sufficient for long chains. A newly fetched principle also does not copy `derived_from` into its candidate premises. | `loom/src/context/context_engine.cpp:698–709,713–716,750–765,768–781`. | Algorithmic repair: traverse to fixpoint with cycle detection, or use `policy/context.dependencies.work_limit` and **mark all unresolved dependencies** on exhaustion. Dependency honesty is mandatory; making silent truncation configurable is unacceptable. |
| O07 / P2 | Text language fallback is requested language → English → first map value. Rendered band headings always receive `""`, even when `lang` was requested. The web also renders English before Polish and does not pass context `lang`. Detail shapes hard-code label length `100`, first support quote `160`, separators and Markdown/comment templates. Token count is a character/4 estimate, not provider tokenization. | `loom/src/context/ctx_common.cpp:18–28`; `loom/src/context/context_engine.cpp:174–176,440,489,596,814–838,841–851`; `loom/web/src/api/knowledge.ts:70–71`; `loom/web/src/components/KnowledgeWorkbench.tsx:479`. | Repair `lang` propagation; `renderers/context.{locale_fallbacks,label_chars,support_projection,format}`, `ui.locale` and capability-selected token estimator. Keep evidence markers and source access. A display quote limit must not alter the stored raw source. |
| O08 / P1 | Chat attachment routing is a fixed extension list; text is reduced to the first **15000 Unicode characters**, unsupported attachments are omitted, missing/read-failed files only log. There is no structured per-attachment disposition/truncation in the request result. | `loom/src/chat/chat_engine.cpp:44–55,168–198`. | `profiles/request_adapter.attachments` (validated MIME/capability handlers) and `policy/request.attachment_projection.{limit,selection}`; record accepted/limited/unavailable per attachment. New suffixes are not a substitute for implemented parsing. Keep raw files and locator-based projections. |
| O09 / P2 | Chat history is all active messages, with weight `>1.5` → `[IMPORTANT]`, `<0.5` → `[low priority]`, and no history token budget or refinement compilation. System, memory and graph material are always separate system messages in that order. | `loom/src/chat/chat_engine.cpp:207–246`. | `policy/request.history_selection`, `.priority_encoding`, `.section_order`. R13's stable/project/goal order remains the default conceptual contract; provider-compatible serialization and within-band choices can be recipe data. Preserve rejected/edited history as source without making it an active instruction. |
| O10 / P1 | Model reasoning capability is guessed from ID substrings (`opus`, `sonnet`, `o1`, etc.); `4.5/4.6` anywhere selects the “new Claude” branch. Effort budgets are `5000/15000/30000`, `max` maps to verbosity, reasoning is always requested in the response. “Deep research” is one web plugin call with ten results and high search context, not an implemented iterative research workflow. Chat timeout is `180000` ms. | `loom/src/chat/chat_engine.cpp:250–275,341–351,363`. | `profiles/providers.<id>.capabilities/reasoning_adapter`, `recipes/request.web_search`, `policy/request.timeout_ms`, `policy/request.reasoning_output`. Match explicit provider/model capability, keep native/equivalent/limited/unavailable honest. No undocumented string heuristic should silently confer capability. |
| O11 / P1 | Batch semantic inference truncates input at `3000`, uses output `800`, temperature `0.1`, three concurrent workers and fixed timeouts. Native Anthropic vs compatible completions is inferred from `base_url` containing `anthropic.com`; native endpoint/version are protocol constants, but dispatch is a profile choice. It reads `api_key`, while Settings offers a distinct `anthropic_batch_key`. | `loom/src/chat/batch_api.cpp:35–46,78,96–98,136–170,237–242`; `loom/web/src/components/SettingsPanel.tsx:166–167`. | `recipes/semantic_batch.{input_projection,output_limit,temperature}`, `policy/semantic_batch.{concurrency,timeouts,budget}`, `profiles/providers.batch_adapter` and credential **reference**. Do not put key values in data packs. Native protocol fields stay in a versioned adapter; make UI key routing truthful. |
| O12 / P1 | The response returned to the caller includes full reasoning, but persisted message metadata retains only **2000 characters**. Topic extraction fixes two topics; auto-title fixes first thirty characters. A projection limit is reasonable, loss of the original response reasoning is a fidelity gap unless separately retained. | `loom/src/chat/chat_engine.cpp:456–459,486,491–499`. | Preserve the complete received response as a source; `policy/response.projections.{reasoning_chars,topic_count,title_recipe}` may control derived display metadata. Do not describe the truncated metadata as the complete provider response. |
| O13 / P1 | Legacy graph canvas unconditionally removes every label of length ≤2 and a fixed noise list, including `data`, `file`, `records`. It filters connected edges too, on load and expansion, with no show-all control. This can hide valid `IO`, `C`, `R` entities. It is presentation-only; the API/database are not changed by this filter. | `loom/web/src/components/GraphView.tsx:42–73,98,295`. | `views/legacy_graph.filters.{noise_lexicon,min_label_length,mode}`; expose hidden counts, reasons and show-all/whitelist. Python-parity commentary at `36–40` is not a D1 justification for locking the filter. A UI filter must never veto retrieval elsewhere. |
| O14 / P2 | Legacy drawers remain mutually exclusive. The **knowledge workbench already permits repeated coordinated views**, so claiming the whole UI has a five-view or one-view limit would be false. Its saved layout persists only pane kinds, not per-view filters, focus, scope/detail, geometry or coupling. | `loom/web/src/App.tsx:47,61,124–140`; `loom/web/src/components/KnowledgeWorkbench.tsx:63,77–78,130–131,139–152`. | `workspace/layout` and `workspace/views` presets: view identity, query/filter/projection, coupling, geometry and controls. Extend the existing T5 contract/one graph; do not build another persistent knowledge store. Retain simple drawer preset as an option. |
| O15 / P1 | Settings exposes a global stream toggle which persists `config.stream`, but live chat uses **only request `opts.stream`**, default true. ChatView sends no override. Request-level `stream` itself is parsed and used; it is not dead. The legacy context slider also controls a preview only and does not update chat depth/token settings. | `loom/web/src/components/SettingsPanel.tsx:135–138`; `loom/src/chat/chat_engine.cpp:90–94,333`; `loom/include/loom/chat_engine.h:85`; `loom/web/src/components/ChatView.tsx:96–98`; `loom/web/src/components/ContextSlider.tsx:23`. | Wire existing config/per-request precedence, then test payloads. `policy/request.stream_default` and effective context-recipe controls must appear in the request snapshot. Label independent probes explicitly; do not imply a preview is the outgoing request. |
| O16 / P1 | Conversation UI fetches a fixed first **100** with no paging. Knowledge collections fetch one limited page (UI choices ≤10000), discard total/has_more and filter locally; resolving a context reference searches only loaded collections. A possible-truncation warning already exists, and candidate/catalog panes already have paging, so the issue is not universal. | `loom/web/src/components/ConversationList.tsx:19,70–90`; `loom/web/src/components/KnowledgeWorkbench.tsx:98–104,128,303,319,485`; query metadata exists at `loom/web/src/api/knowledge.ts:8–11`; candidate/catalog paging `KnowledgeWorkbench.tsx:277,529,561`. | `views/*.page_size` is a preset, while server paging/querying and direct reference fetch are necessary mechanisms. Never infer absence from the first page. Report total/truncated state separately from selection filters. |
| O17 / P1 | Native knowledge judgement/materialize/predict/pack/policy endpoints exist, but the typed web KnowledgeApi and HTTP implementation expose none of them. Inspecting claims does not offer a graph judgement write path. Catalog overrides are implemented and are a separate capability. | `loom/server/src/app.cpp:926–942`; `loom/web/src/api/knowledge.ts:46–58`; `loom/web/src/api/loom-http.ts:39–50`. | Expose existing append-only owner judgement through controlled interactions; `views/claim_inspector.actions` can choose which controls to display, but I4 owner authority/replay is not optional. Policy/profile discovery should use the canonical pack, not duplicate hard-coded UI meanings. |
| O18 / P1 | The UI token-entry section is behind a successful config load. A fresh browser against a token-protected server gets a config error, swallows it and stays at “Loading…”, so it cannot reach the token-entry control in that component. Server auth itself is an appropriate constraint, not a policy to weaken. | `loom/web/src/components/SettingsPanel.tsx:69–70,96,183–203`; `loom/server/src/app.cpp:242–249`; transport storage `loom/web/src/api/loom-http.ts:53–74`. | Repair auth bootstrap/error state independently of authenticated data. `profiles/interface.connection_setup` may choose presentation; token checks and safe credential handling remain mechanism. Test fresh session + required token, then invalid/valid transitions. |
| O19 / P1 | Graph expansion deduplicates edges by `src->dst` only. Two different relation types between the same entities collapse in the projection, even though both remain in the returned graph. | `loom/web/src/components/GraphView.tsx:310–316`. | Correct projection identity using full edge identity. This is a data-loss defect in a view, not an optional alternative meaning of identity. Keep the canonical graph untouched. |
| O20 / P2 | Catalog UI scan fixes `retain_raw:"selected"`, while score/select fixes `llm:"off"`. Import scope and copy/link retention are already live controls. An offline scoring default is appropriate; explicit alternative recipes remain unavailable in this view. | `loom/web/src/components/KnowledgeWorkbench.tsx:545,547,563–566`. | `views/catalog.scan_defaults.retain_raw`, `views/catalog.score_profile`; provider/capability/budget-gated alternatives. Changing a profile alone must not authorize external calls. |
| O21 / P2 | Server payload limit defaults to 64 MiB and is an internal ServerOptions field, but has no operator CLI/environment setting. It bounds importable exports even though import supports larger archives elsewhere. | `loom/server/src/app.h:19`; `loom/server/src/app.cpp:197`; `loom/server/src/main.cpp:25–29,35–69`. | `server.max_body_bytes` through validated deployment configuration, not an unlimited client override. This is a resource limit rather than a semantic relevance policy. |
| O22 / P2 | Other consequential presentation/analysis presets remain literal: preview depth/budget defaults and ranges; graph depth/node-limit menus; initial panes and latest-done run selection; candidate page sizes; semantic budget defaults and representation choices; theme variants; log/status polling and log count. Several limits are already user-adjustable and engine safety caps still apply. | `loom/web/src/components/ContextSlider.tsx:10–12,48–69`; `GraphView.tsx:86,356–363`; `KnowledgeWorkbench.tsx:31,86,171–174,218,255–257,277,336–339,362–365,469,489,529,561`; `loom/web/src/App.tsx:13,58`; `loom/web/src/components/LogPanel.tsx:4,13`; `SemanticStatus.tsx:5`. | `views/*.controls/defaults`, `workspace/presets`, `recipes/analysis`, `ui.themes`, `ui.refresh_policy`. Data selects implemented controls/representations within validated operational caps; no blanket “every number is an owner preference”. |
| O23 / P1 | A decision lacking its underlying claim is assigned archive origin, observed evidence and confidence `0.7`; a status record gets archive/observed/`0.75`. These fallbacks are not measured ModelProfile reliability. Missing assessment must not be upgraded by a convenient constant. | `loom/src/context/context_engine.cpp:509–524,557–559`. | Preserve the source assessment/provenance or return explicit insufficient evidence; trace fallback reasons. A recipe may choose inclusion of unverified material, but cannot freely relabel it observed or calibrated. Do not expose a “confidence” slider as the repair. |

## Acceptance tests for the high-impact fixes

[P] Add these only when implementing the relevant change. They are mechanism
tests; none is a claim about semantic model quality.

1. **O01:** ScriptedTransport records zero network requests for preview with
   semantic model and key configured but permission absent, budget zero, or
   offline mode. Positive authorized test captures request, budget accounting
   and first response; failure returns a labelled cue fallback. Use fixtures,
   never a paid endpoint.
2. **O02/O03/O15:** capture the actual serialized chat request for two context
   recipes. A changed scope/detail/depth/stream setting affects that request or
   returns explicit unsupported capability. Preview and transmitted context
   have matching hashes only when they truly share a compiler invocation.
   History/source bytes remain unchanged.
3. **O04/O05:** freeze a development corpus and separate validation split.
   Report required-item recall, irrelevant inclusion precision, missing
   counter-evidence and their denominators; compare one-hop/fixed-score/cue
   baseline. Every retrieval channel contributes evidence independently. No
   tuning on holdout or treating cue/model confidence as calibrated truth.
4. **O06:** chain of at least five premises, a principle derivation chain, a
   cycle, missing ID and tight-budget sweep. Every dependency is included or
   explicitly unresolved; a traversal limit cannot produce an apparently
   complete item. Existing dependency markers remain green.
5. **O08/O12:** attachment >15000 Unicode characters, missing/unsupported
   attachment and response reasoning >2000 characters. Raw source hashes and
   bytes survive; projection limits/dispositions are recorded with locators.
6. **O10/O11:** table-driven capability fixtures include current, unknown and
   misleading substring IDs/base URLs. Assert adapter, credential reference,
   recipe limits, no unauthorized call and deterministic retry/fallback.
   A new provider profile must not inherit an old model's error profile.
7. **O13/O14/O19:** graph fixture with `IO`, `C`, `R`, `data` and a real noise word;
   show-all restores all nodes/edges without altering graph storage. Save and
   restore multiple views of the same kind, independent filters and a coupled
   focus/scope/detail change without closing other views. Two differently typed
   edges with the same endpoints survive expansion/reload.
8. **O16:** 101 conversations, collection >10000 and context ref outside the
   first page. Paging/search/ref lookup reaches each, and the UI exposes its
   known total/truncation. Validate candidate/catalog paging separately.
9. **O17/O18:** web judgement reaches the existing API and survives rebuild;
   a fresh browser can enter a token before config loads, and unauthenticated
   API access remains rejected. No secret value is written to pack/log.
10. **O20/O21/O23:** scan/score profile capture preserves explicit offline
    permission; two deployment payload limits reject/accept at the exact
    configured boundary; missing underlying assessment is visibly unresolved
    and does not acquire calibrated confidence from a fallback constant.

## Already data-driven, or deliberately kept in code

- Goal types/cues/roles/evidence/resolutions/budget shares are already
  `goals/goal_types.json`; evidence Markdown/origin rendering already reads
  `policy/evidence_encoding.json` (`ctx_common.cpp:88–107`). Do not fork them
  into UI or profile copies. ContextEngine's hard-coded **fallbacks** and
  objective remain the issue in O04–O07.
- Chat model, base URL, temperature, max output tokens and system prompt are
  effective config or request overrides (`chat_engine.cpp:207,326,332–339`).
  The historical default `anthropic/claude-sonnet-4-20250514` is a preset
  candidate (`loom/src/core/config.cpp:196–209`), **not a locked model choice**.
  `semantic_model` is live in the audited goal/batch paths. The separate
  `anthropic_batch_key` and global `stream` controls have routing gaps above.
- Evidence classes, origins, resolutions, validation statuses and universal
  roles are closed-set mechanism. I3's non-observation marking and no
  extrapolated premises, I4 owner authority, source preservation, deterministic
  tie-breaking and dependency honesty are constraints, not optional knobs.
- MIME/protocol serialization, Anthropic API version, status codes, JSON/SSE
  framing, error sanitization, token comparison, path/filename protection and
  archive output boundaries belong to validated adapters/security mechanisms.
  A profile chooses a supported adapter; a pack must not arbitrarily override
  protocol validity or authentication.
- Server bind/port/data/static/token settings already accept environment/CLI
  (`loom/server/src/main.cpp:34–60`). Payload limit is already a ServerOptions
  field (O21); keep-alive count `100` (`loom/server/src/app.cpp:198`) is an
  operational tuning constant, lower priority than coverage/control defects.
  Ordinary CSS spacing, pointer hit tolerances, batching/render cadence and
  buffer sizes were not promoted into owner-facing semantic policy.

## Integration boundary and next step

[P] Implement O01 first, then O06 and request/compiler control (O02/O03/O15),
with captured offline requests and baseline comparisons. Keep pack defaults,
per-request overrides and provider capability profiles distinct. Resolve the
effective recipe once, record its versions/hash in the request snapshot, and
make UI controls report that effective state. Workspace presets contain
queries, references and rendering controls, **not copied claims/sources**;
projections remain rebuildable views of the canonical graph.

No tests were run for this report-only increment; line evidence and endpoint
chains were inspected independently by two agents. Runtime trigger behaviour,
provider correctness and UI browser behaviour remain to be verified by the
acceptance tests above. No threshold/gate was changed.

## Verified context follow-up (2026-09-30)

O01 and O06 were independently reproduced before implementation. With an
installed model/key, the default preview made a ScriptedTransport request;
dependency chains selected only **4/6 claims** and **0/5 principles**. The first
red run is retained in `/tmp/loom-session-validation/context-defects-first.log`.
These are mechanism counterexamples, not measurements of model quality.

The context increment now keeps `type_goal`, `select`, `build` and the existing
context C ABI/HTTP preview offline regardless of configured capabilities.
Optional model typing remains available through a separate native
`type_goal_with_model` call with an explicit one-request input/output/response/
timeout budget; it is not enabled by ContextRequest JSON. The caller owns any
session/monetary authorization. The first received body is retained in the
existing source/blob store before interpretation, including failed responses;
oversize responses retain an explicitly incomplete prefix without interpretation
or retry. Diagnostics carry references and error categories, not response text
or credentials. Storage failure preserves the spent-attempt trace and cue
fallback. Provider confidence requires a finite numeric value in `[0,1]` and is
labelled uncalibrated self-report; cue margin is also uncalibrated. Missing or
invalid confidence never becomes a fabricated provider assertion.

Premise closure expands each accepted reference once to a finite fixpoint,
including newly fetched principles' `derived_from`; cycles terminate and
budget-limited omissions retain the existing missing-premise marker. The long
chain fixture now selects **6/6 claims** and **5/5 principles**. No additional
source/projection store or configurable permission to truncate dependencies was
introduced. O05's cue recipe/threshold and the other audit findings remain open.

Final verification: independent context suite **18/18 cases, 1065/1065
assertions**; five adjacent C ABI/context/chat/semantic suites pass. Unfiltered
`env TMPDIR=/var/tmp ctest --preset dev --output-on-failure` passes **72/74**,
matching the baseline: `compat.test_candidate_graph_native` and
`research.candidate_graph_protocol` cannot load the omitted historical
`tests/fixtures/eval/independent_candidate_graph_v1/initial_report.json`.
Credential-path guards and quality thresholds were not changed. Catalog
synthetic development metrics remain recall **31/45**, precision **31/31** and
trap false positives **0/5**; these are not holdout results.

Logs: `/tmp/loom-session-validation/context-independent-final.log`,
`context-adjacent-confidence.log`, `native_final_context_confidence_ctest.log`
and `catalog-eval-final-context.log` in the same directory. The earlier broad
run with an unwritable `/var/tmp` is retained separately and is not the final
regression result. No live model call, calibration experiment or browser test
was performed. O02/O03/O15 request/compiler integration remains the next
high-impact work after this scoped repair.

Durable copies of the first failure and final focused/CTest/catalog output are
retained in `docs/research/context_controls_v1/`. They contain synthetic
fixture diagnostics only; missing-history failures remain visible.
