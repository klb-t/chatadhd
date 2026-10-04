# Thread 6: read-only cross-thread handoff, 2026-10-04

This is a read-only proposal and audit for thread 6. No source, policy, public header, database, branch or blind evaluation was changed by this audit. It does not introduce a MethodVersion schema. Thread 6 should consume the common methods/run-graph contract when threads 3/4 and the integrator define it.

## Pinned inputs

All reads for this cross-thread audit happened after root fetch 61234 completed. Only named public thread branches and their API/report files were read. The auditing agent did not open the blind branch, holdout key, private archive, model call or research dataset. A separate agent subsequently performed the one final blind first look; see the final thread report. Its negative result did not lead to source or policy changes.

| Thread | Ref | Commit |
|---|---|---|
| 2 | `origin/gpt/usage-policy-2026-10-04` | `34cc920dd3cdb0c0fca0a514569b19111429583f` |
| 3 | `origin/gpt/chat-selector-2026-10-04` | `36ec2a8f6340ba3d7873d3c4864e3af23178bcd6` |
| 4 | `origin/gpt/native-graph-packet-2026-10-04` | `3caa6b4dcfb6412294bf816c1105bcf99afacdbc` |
| 11 | `origin/gpt/data-profiles-2026-10-04` | `c21e664c8145568e0505f9218fda67a75616056f` |

Inventory baseline is `161cc22dfb84fe863389d6b90323bd44516a68dc`. Its 31 groups describe that baseline, rather than the final thread-6 source. Native measurement is frozen to final delivery library SHA-256 `5ded94c8771d79105c32d8b7de38c80a7500ec7cb475198e49148a9bb49a1fe5`: default DEV remains TP31/FN14/FP0/TN20, auxiliary3/3. The 14 label-derived mock vector cases verify admission/fusion only, not model efficacy. All calls were offline.

## Contracts actually available

- **2:** `loom::UsagePolicy::open/preview/request/confirm/complete/cancel/inspect`, configuration key `loom_usage_policy`; options are presets. `loom_usage_policy_json` is static-kernel only at this ref. Shared FFI export belongs to the integrator. The admission ledger is neither provider-call permission nor exactly-once dispatch.
- **3:** source-private `providers::EmbeddingRequest`, `EmbeddingReply`, `embedding_identity`, `embedding_capability`, `provider_embed`. Identity binds endpoint/model/options/account as an opaque digest; credentials are not exposed. Request has `calls_authorized`, `expected_identity`. At the pinned public ref only the provider adapter is committed; context/cache/goal-typing/run-graph wiring is still a plan in its report. No public `ProviderRegistry::embed` API exists there.
- **4:** source-private `loom::packet::execute`, C ABI `loom_packet`, HTTP `POST /api/packet`. Commands cover make/validate/encode/decode, empty_diff/preview/apply/invert, capture, compile_reply/validate_compilation/apply_compiled_reply. `loom.graph_packet/1` retains definitions/entities/claims/sources/task/provenance/history. `definition.kind`, `definition.attrs`, task and descriptive kinds remain data. This is a generic packet algebra, not a defined analysis-method or execution-run vocabulary.
- Existing main `loom_graph_packet_store` / `POST /api/graph/packets/store` remains explicit accept/read/replay with immutable receipts/CAS and owner judgement replay. Definitions are retained in the packet rather than materialized as native knowledge rows. Packet acceptance does not establish content truth. Thread 6 must not add a second graph store or automatic canonical acceptance.
- **11:** inventory gives 31 catalog groups DIC-0452 through DIC-0482. Proposed `loom/data/presets/catalog.json` and `loom/data/profiles/catalog-sources.json` do not exist in current main. They are proposed destinations, not implemented APIs.

No `MethodVersion`, `method_version`, `method_run`, `run_graph`, `method_graph` or `execution_graph` contract was found in the inspected headers/contracts/reports/context/packet paths at these four pinned refs. This is a bounded audit, not a claim about every repository file or unpushed work.

## Thread 6 data available for the future common graph

Current supported runtime input is `catalog_semantic_candidates` with schema `loom.catalog_semantic_candidates/1`: enabled, profile_input_hash, channel/model/method, query, records with unit_id/content_hash/vector, and policy bias/weight/tau_relevant/fusion. Disabled input preserves the baseline run identity. All active records are validated before score/link writes; unknown/stale IDs, stale profile/content hashes, invalid finite vectors and mismatched dimensions reject the input.

| Boundary | Existing identifiers and values | Meaning to preserve | Unestablished facts / loss |
|---|---|---|---|
| Raw source to catalog unit | source hash, Unit ID, locator, content_hash, sketch | Address/source binding; immutable raw source remains authority | Normalized message/prose and wrapper fallback can be projections/reserialization; no source authenticity claim |
| Unit/profile to supplied vectors | profile_input_hash, unit_id, content_hash, model/method/channel, query_hash/vector_hash/configuration_hash | Caller supplied vectors bound to exact declared inputs and numeric envelope | No proof that a named model produced them, no recorded provider response, no proof all records share a vector space beyond caller declaration |
| Vectors to channel rank | cosine; bias/weight; score; score_kind=`retrieval_rank` | Ranking is a configured numerical transformation; absent record is missing evidence, not a negative score | Sigmoid score is not calibrated truth/evidence confidence |
| Channel rank to selection | fusion, tau_relevant, rank_delta, score_before/score_after; final label; selected/decided_by/reasons | Union independent of lexical endorsement; owner override still wins; no invented identity/project hit | Rounded public ranks/features lose numerical precision; threshold/selection is a decision, not a truth assertion |
| Local legacy semantic path | word/char TF-IDF method string, seeds/passes/floor/ref/vocabulary; profile+pack+ScoreConfig fingerprint | Local lexical representation/centroid recipe; evidence version3 | Sketch/top-K sampling, stemming/char projection, centroid/normalization and rounding are lossy; this is not an independent embedding model |
| Run/replay | run_id fingerprint contains profile hash, pack hash, ScoreConfig, scoring_evidence_version and active external configuration_hash | Deterministic score recipe reference | run_id does not fingerprint the complete ordered unit/sketch corpus; source changes under a retained profile/recipe are not fully described by that short ID alone. It is not a full execution graph or receipt |

Hashes in thread6 use SHA-256 of `loom::json::canonical`, except source/content hashes and explicitly measured raw bytes. GraphPacket at pinned thread4 claims Python-compatible canonical hashing. Consumers must record the hash algorithm/encoder and validate bindings; do not assume canonical JSON implementations agree for every numeric representation. The receipt tests validate JSON-number representation hashes for query/vector.

Scores/features/reasons persist in `loom_cat_scores`; the full active semantic envelope and the native score summary are not separately immutable historical records. The DEV runner preserves the exact envelope/config/source/binary snapshots as test artifacts. A later common run graph needs retained input/output artifacts, not only hashes of potentially replaced runtime settings.

## Exact handoffs (proposal, no new schema)

| Owner | Needed contract or action | Thread 6 contribution / acceptance boundary |
|---|---|---|
| 3 | Define/retain vector-generation input projection, provider/model/space identity, exact content and query inputs, cache recipe and first/provider response capture. Reuse UsagePolicy before authorized provider dispatch, preserve admission/confirmation/actual receipts and dispatch state. | 6 consumes hash-bound vectors from that path using the existing envelope; no direct provider invocation added. Keep model/method/channel declarations separate from verified generator origin. Do not use DEV labels or corpus IDs to generate production vectors. |
| 4 | Define common method definition/version/run vocabulary over existing GraphPacket algebra together with integrator; explicit input/output and transformation-loss/augmentation records. | 6 maps its existing fields above into that common vocabulary once available. Use packet make/validate/diff/apply; avoid a private MethodVersion schema and a new packet engine. A packet trace must not promote retrieval ranks to calibrated assessments. |
| 9 | Integrate2 before dependent3/4 paths; scope shared UsagePolicy export if chosen client needs it. Assign common vocabulary/pack-generation ownership and reconcile effective policy identities across stages. | Preserve final DEV receipts and all owner overrides. Record corpus/input manifest independently of run_id. Packet storage is separately explicit acceptance; automatic catalog run must not write canonical claims. |
| 11 | Own new profile/preset schema, pack manifest/loading/validation/generation, and inventory corrections below; coordinate profiles/self and lexicon language handling with their owners. | 6 can later move owned consumer logic into generic operations while preserving exact effective defaults and all existing tests. Proposed new data files are outside6's allowed data-file list; do not create them here. |

Suggested common-contract requirements are conceptual, not field names: method definition and effective recipe; implementation/version references; operation/run identity; exact input artifacts and projections; output artifacts; typed numerical rank; declared vs verified origin; transformation loss/augmentation; owner decisions; usage receipts. Their schema belongs to the common owners, not thread6.

## Triage of all 31 inventory groups

**R** = real adjustable recipe/domain/diagnostic preset. **D** = already data-backed effective behavior; fallback duplication remains. **M** = mixed preset and protocol/math constraints. These classify individual groups, not a blanket instruction to move every literal. Preserve current defaults; changing presets is not a quality improvement by itself.

| ID | Triage | Current finding and future action |
|---|---|---|
| DIC-0452 | M | Dot directories and __MACOSX are source-selection presets. Excluding active data and .loom-archive also prevents self-ingestion; preserve that separately as declared recursion/exclusion behavior. Empty path is a validity condition. New source/skip preset needs11+pack owner; consumer is6. |
| DIC-0453 | R | Export kind→platform/unit kind/external-ID mapping is format profile knowledge. Move mapping to a registered source profile; generic dispatch must still require an implemented parser/capability. No arbitrary mode becomes executable by listing a name. |
| DIC-0454 | R | Wrapper names and first-recognized-array order are format profile policy. Preserve detection order and exact/derived-source locator disclosures. |
| DIC-0455 | R | 4MiB sketch/prose/code sample is a resource preset, not a retention ceiling. Preserve truncation diagnostics and effective recipe hash. |
| DIC-0456 | R | 32MiB per-element DOM/reassembly cap is resource policy. Larger configured values require an estimate/streaming/capability path, not a new hidden ceiling. |
| DIC-0457 | R | 256KiB read chunk is buffering/performance preset. Positive buffer size is execution validity; value can be configured. |
| DIC-0458 | M/D | exact_max_units, session_max_neighbours, project_max_clique already read thresholds; effective exact_max_units is0, not code fallback2000. LSH rows2/bucket256 remain internal defaults. Positive band geometry and matching signature recipe are algorithm validity; candidate window budgets are presets. |
| DIC-0459 | R | Max4000 eligible repo stems is sampling/resource policy. Current walk truncates without a dedicated profile coverage report; future recipe should retain coverage/loss and deterministic traversal. |
| DIC-0460 | R | Extension set is repository-profile inclusion policy. New extensions need no domain-specific code when existing text handling supports them. |
| DIC-0461 | M | .loom-archive/.git exclusions and minimum4-byte stem are profile filters. Duplicate-stem suppression is a declared set/dedup operation, not a spending cap. Preserve distinction between byte and code-point length. |
| DIC-0462 | M/D | relevance.term_class_weights already supplies BM25 class weights. Profile/identity term creation still independently writes path2/alias3/principle1.5; these affect identity weights/provenance. Consolidate one effective recipe rather than assume changing relevance alone changes every path. |
| DIC-0463 | R | ±60-byte mention excerpt is diagnostic-window preset. Byte slicing can cut UTF-8; future generic projection should preserve spans and disclose/truncate safely without altering raw source. |
| DIC-0464 | D/R | AliasIndex reads explicit per-term weight; principle1.5/alias3 are fallback defaults. Effective term weights live in profile body/hash, but default creation still in0462. Centralize defaults, preserve explicit overrides. |
| DIC-0465 | M | pl/en suffix-table enumeration is language policy; entries already live in stemming lexicon. Alias matching additionally hardcodes Lang::Pl, so enumerating every language alone does not implement language semantics. Requires generic language dispatch with lexicon/header owners. |
| DIC-0466 | M | FPR clamp1e-6…0.5 and maximum16 hashes are presets. Mathematical0<FPR<1 and positive hash count remain validity; uint64 storage/word rounding is representation. Minimum64 bits mixes allocation granularity with preset choice. Keep algorithm/format identity explicit. |
| DIC-0467 | R | MinHash shingles are actually3 tokens; public header comment mentions5. Recipe is adjustable and must be part of sketch identity/version; hash constants and byte/word representation are protocol, not domain data. |
| DIC-0468 | R | Misra-Gries4×topK is memory/accuracy recipe. Positive capacity and counter algorithm invariants remain code; multiplier preset must report approximate projection loss. |
| DIC-0469 | R | Phrase fragment cap6 is a query recipe affecting contribution. Move to relevance/effective recipe, preserve value and full profile source. |
| DIC-0470 | D | k1=1.2, b=.75, percentile99 already overridden by relevance.bm25. This is duplicate fallback, not an unconfigurable ceiling. Remove scattered fallback only after central defaults/validation exist. BM25 arithmetic remains universal code. |
| DIC-0471 | D/R | Most thresholds/expansion/link/semantic parameters already read thresholds.catalog; effective sem_ref_percentile75 differs from code fallback50. link_rounds, feedback_cycles and consensus_min_* can be supplied through that group but are absent from checked-in default file. Central default ownership is outside6 for thresholds. |
| DIC-0472 | M/D | cfg.max_passes is clamped to already data-backed pack max_expansion_passes. Positive link_rounds is currently forced to≥1;0 can meaningfully disable propagation, so it is a preset assumption, not a math invariant. Nonnegative cycle/count validity remains. Report effective values and owner override precedence. |
| DIC-0473 | D | Current and baseline score reads relevance.term_class_weights.expansion;0.8 is fallback. Inventory wording 'fixed .8' overstates it. query.cpp separate literal remains0480. |
| DIC-0474 | R | At most5 evidence-unit IDs is trace retention/loss policy. Keep total count/omission information and reconstructible source references; do not mistake shortened trace for all evidence. |
| DIC-0475 | R | Local semantic N≥4 activation is a recipe preset, not TF-IDF mathematical validity. External supplied vector channel has no such catalog-size gate. Tiny-corpus behavior requires explicit unavailable vs score distinctions. |
| DIC-0476 | R | .05 gates local corroboration/link evidence, not final catalog tau_relevant. Move under effective method recipe; no external semantic hit should require this lexical/local endorsement. |
| DIC-0477 | M | Lift trace2-decimal rounding is output projection/presentation loss; expansion decision uses unrounded lift. Preserve raw/loss metadata in common run output. A representation's supported finite numbers remain capability. |
| DIC-0478 | R/correction | Code does NOT implement plain DF<20: smoothed BM25 IDF>ln(N/20), with floor0 for N≤20.20 is recipe parameter; retain formula/smoothing as named algorithm and exact old behavior. Shared-rare minimum is already a separate threshold. |
| DIC-0479 | R | Fixed .5/.5 word/gram similarity fusion is method recipe. Named weights need explicit zero-channel/normalization behavior; value is not confidence. |
| DIC-0480 | R/D | query add_profile_terms writes expansion weight.8 despite data-backed score0473. Consume the same term recipe; preserve explicit caller augmentation/source provenance rather than infer fact. |
| DIC-0481 | R | First-user head selection and200-code-point clip are projection preset. Role semantics come from source profile, clipping loss belongs in projection record. |
| DIC-0482 | M | Page size100 is preset. score/date/id sorting and selective/full/copy/link are implemented execution capability names in the current API; arbitrary new strings must remain unsupported until a handler exists. Descriptor/registry can expose capabilities without deleting validation or source retention/import invariants. Public catalog/ABI headers and CLI are outside6. |

There is no whole-group instruction to move math/protocol invariants into owner presets. Pure SHA/FNV constants, UTF-8 validity, vector equal dimensions/finite numbers/nonzero norms, IDs/hash binding, owner override precedence and immutable-source semantics are not adjustable corpus limits. None of the31 groups is wholly discarded as an irrelevant false positive;0470/0473 are chiefly corrections to claims of missing configurability,0482 includes capability invariants,0478 needs formula correction.

## Scope and generated-pack work

Already data-backed: relevance bias/feature/channel/class weights/BM25; ordered selection rules; thresholds catalog parameters above; profile aliases/context gates/date prior; stemming lexicon entries; supplied channel policy in runtime config. The new external channel has no private model-quality calibration or corpus-size ceiling.

Within6 literal edit paths, subject to root's later authorization: catalog consumer operations; relevance/selection_rules/candidate_graph policy only. Outside6: proposed source profiles/presets, profiles/self, lexicon entries, thresholds, public headers, CLI, C ABI, pack validation/manifest/generator/generated pack. `gen_kb_pack.py` embeds only paths listed by loom/data/pack.json; new schema/path requires owner review and regenerated pack, not only creating JSON. Pack validation currently constrains relevance bias[-100,100] and BM25 k1[0,10] independently of catalog consumers; those ceilings need KB/pack-owner triage. Threshold validator only accepts numeric values in groups, so null-as-unbounded policy would need that owner's contract change.

candidate_graph.json is experimental structural-expression vocabulary with no inference/truth promotion. Its listed budgets/native lower-only ceilings belong to extraction/pack owners for execution; it must not become an archive-specific synonym bank or pretend to define GraphPacket methods.

Additional current literals outside the31-list coverage noticed during this audit: score identity mention50/version window80, title mentions5/class-diversity cap5, BM25 normalized-feature cap2, aggregated project hubs4, round4 score/features. Record them as follow-up inventory candidates, not silently claim the31 are exhaustive or change them during a frozen verification run.

## Source/hash receipt

Branch-file SHA-256 values (raw git blobs):

| Thread | Path | SHA-256 |
|---|---|---|
| 2 | `loom/include/loom/usage_policy.h` | `89dfad23470928a45900ff00c2cb6197fa183f9c2a7ab96f89bb832cb99273ca` |
| 2 | `loom/src/policy/README.md` | `9098d1165b73a8fb4d2b1a2770ccbef613226548413de2b4cfa4e5e272a9c92b` |
| 3 | `loom/src/providers/embedding.h` | `2587a0409a57bcb4d829b0afed63634613e166556066f2507188c025e1ee625c` |
| 3 | `docs/reports/chat-selector-2026-10-04.md` | `b229e6e65f37ae9cee3abfe945f8e96966e48ad3a82a4cd8da7e168fd617b14b` |
| 4 | `loom/src/packet/packet.h` | `d82162f0380f4e8164b056dcf2649412083ecbfd80232b918937832882229c5d` |
| 4 | `loom/src/packet/README.md` | `17151cc45a129c8f7e68af6b59eeabc6b64b5ff11315068f3fe4da17d66f552e` |
| 11 | `docs/reports/data-in-code/thread-6.md` | `7c6934e25a47130f6c3628311d493af7bb9a86ab4ae283b98fca8347ae61f11a` |

Current local code/data/generator SHA-256 values used for triage (some are outside6 read-only dependencies):

| Path | SHA-256 |
|---|---|
| `loom/src/catalog/semantic_candidates.h` | `2c1330e382e0cd2a691fb74630a2e4b86f7b5ad2313fa24cafda6fadfab308e8` |
| `loom/src/catalog/semantic_candidates.cpp` | `53f2783e67932aa54a6e843e1835f82370407f0629dc0e9fa8238b621ba3d6c2` |
| `loom/src/catalog/score.cpp` | `ea8c81a0ae6cfcdc0e88e6f2dee964071adb412db3d423eb1957cc690b63992d` |
| `loom/data/policy/relevance.json` | `992aa0a4ca657ea4b0ccd32039ea0a93639d228b064f44f0ff81f24de4cb387f` |
| `loom/data/policy/selection_rules.json` | `b98da5d1c509e8bbafee8f13fc6c5fe4230217bb14300ffc1281803109189685` |
| `loom/data/policy/thresholds.json` | `b527c94ca4ff65651f46505420f236f958313524754b611e9a418013bdc72451` |
| `loom/tools/gen_kb_pack.py` | `026917708d4a74d34229b784ca329b8ee0e2f7fda678805363dd8632f9e5c1db` |
| `loom/src/kb/pack.cpp` | `4e8122b32840fbd82d81c0f018d087e738e35d8cc98fb4f6e58b70daaae9ac2e` |

## Later fetch supplement — 2026-10-04

Read-only report review after the final fetch pinned thread 2 at
`03b0c4ed954e53cb72ec7947fe6e5d9ad28d495f`, 11 at
`096028e498290a3e4d7d2f67b03ea378318747a4`, 8 at
`2544cf3a75c1477797213328f7279b3e8576579e`, and 9 at
`b8e91a56355237a1db1cd56d55ece5fd2f3162bf`. Main remains `161cc22`.

- Baseline archive `4df4c56926c49012b0cea85d42e8fb33d49262cf`
  reports 106/108 with the same two 60-second research timeouts on unchanged
  main. This establishes prior occurrence, not one proven cause or a green gate.
- Thread 2 adds `usage_policy_settings`, settings hashes and `preview_settings`.
  The preset still reports `legacy_code_pending_pack_migration`; the shared
  dispatcher export and real pack migration remain integration dependencies.
- Thread 11 supplies RuntimeProfile descriptors in `.pack`, overlays,
  validation and inspection/hash. The catalog's 31 inventory groups and missing
  common method/run graph contract remain. No catalog migration was assumed.
- Thread 8 reports dev/ASan 108/108, JNI and web passing; its broader matrix
  remains blocked on Clang captures. These are not tests of thread 6's source.
- Thread 9 retains the common 3/4 contract gate; its INDEX still references the
  first thread 6 helper commit. The final report and later branch commits
  supersede that early status, without claiming readiness for main.

## Do wątku N

- **3:** supply producer/cache evidence and adopt the agreed method-version/run graph contract.
- **4:** define that shared contract and durable input/output artifacts with3; catalog ranks remain ranks.
- **11:** take the31-group corrections above into the inventory/preset/profile schema; defaults must compare identically.
- **9:** retain the current104/106 CTest timing block and coordinate the common3/4 format before integration.
