# First actual native C++ source-only baseline

The current frozen native catalog→extract pipeline ran on all24 source-only DEV conversations in each of two explicitly declared format arms, with no supplied nodes, aliases, judgment queries or gold. All48 actual native runs completed; whole series wall time was22.259245983s and external API cost wasUSD0. Native code,42-document pack, source text and extraction policy were unchanged. New validation and older holdouts were not read. These are the same previously inspected DEV sources, not a blind generalization experiment.

| Measured output | Native-readable transport role=user | Literal fixture actor in author.role control |
|---|---:|---:|
| Completed cases / planned |24/24|24/24|
| Original source turns |72|72|
| Exact full raw-container blob copies |24/24|24/24|
| Core imported text messages / original turns |72/72|0/72|
| Cases with exact imported text multiset |24/24|0/24|
| Native observations |84|0|
| Unique source turns with full utterance observation |72/72|0/72|
| Observations with exact pointer + UTF8 slice + raw-source hash binding |84/84|unavailable|
| Observations with original source date |84/84|unavailable|
| Observations with original named speaker identity in speaker field |0/84|unavailable|
| Entities |26|0|
| Claims |13|0|
| Principles / status records |6 /2|0 /0|
| Current extractor outputs with implies/causes predicate |0|0|
| Semantic typed-edge precision / recall |unavailable|unavailable|

The primary84 observations comprise72 whole utterances and12 sentence fragments. All have native speaker=user. Original speaker names, source IDs, turn IDs and literal known_at strings remain in the immutable raw OpenAI-shaped JSON metadata and author.name. Date equality measures preservation of source metadata, not temporal truth or claim validity. No date is promoted to a verified fact.

The literal-role arm deliberately inserts fixture actors such as reporter into the OpenAI author.role field. This is an explicitly malformed transport-role control, not a representative real OpenAI export and not a claim that ordinary user/assistant exports lose messages. Both raw containers are retained; the current parser/import route accepts no such actor-role messages. The primary transport projection preserves all text, but the derived native observation identity collapses to user. This distinguishes raw preservation from speaker-identity preservation in the graph projection.

The native13 claims use4 has_decision,2 has_status,1 mentioned_in and6 states_principle predicates. All13 have nonempty source support and observation references in the same case; all13 EvidenceRef quotes occur inside the resolved locator span. Exact quote equality with the entire locator slice holds for10/13: the other3 use a narrower quote and a whole-observation locator. This is retained as a precision-of-location diagnostic, not silently declared fully exact or called a contract defect without a strict quote-span requirement.

A concrete semantic diagnostic appears in007/010: “If the sensor reports no error, the record is not discarded” and its Polish counterpart are tagged has_decision/rejected_option. A source-grounded quotation does not validate that semantic category. Similarly, released in conditional propositions produces has_status=implemented in019. No post-result keyword tuning or native code/pack changes were made. These are examples for investigation, not an estimated native false-positive rate.

The current stock software-domain extractor policy emits no target logical relations. The graph storage ABI can hold general predicates; this experiment measures the actual current extractor, not a fundamental representational impossibility. Mapping generic decisions/statuses to implication/causation would invent a comparison. Native relation precision and recall therefore remain null, rather than0% or a claimed win/loss.

The actual FREEv1 instrument did emit a source-only logical-assertion ABI on these same24 sources: its original strict compiler produced6TP/43FP/54FN (P6/49,R6/60), with18/24 cases compiled. A separate posthoc exact turn-ID string evidence projection on the same original responses produced38TP/11FP/22FN (P38/49,R38/60), preserving the six unavailable cases and the original strict outcome. See ../recipe_experiments_2026-09-30/evidence_format_projection_v1/RESULTS.md. This establishes a measured difference in output capability and interface-format loss; it does not make native generic predicates numerically comparable to those logical edges. No extra model attempt belongs to this native experiment.

| Computational measurement | Primary24 native runs | Literal-role24 native runs |
|---|---:|---:|
| Sum outer driver wall seconds |11.020973857|11.023097638|
| Sum native user CPU seconds |9.724095|9.469604|
| Sum native system CPU seconds |0.317456|0.341698|
| Maximum native process RSS KiB |19080|18928|
| Native-child wall range seconds |0.373100057–0.497093723|0.349141512–0.537030772|

Each fresh Python supervisor measures exactly one native child with Linux RUSAGE_CHILDREN; native CPU/RSS exclude the supervisor. Outer wall includes supervisor startup, catalog/import, extraction and snapshot work. This series runs sequentially on a shared host, not a production latency promise. Earlier local ranking and cross-encoder costs have different scope and initialization policies; no speed ratio is inferred.

First failures and corrections are retained. The fixture-envelope prepare bug occurred before input freezing or native outputs. The initial launcher encountered missing /usr/bin/time;48 first unavailable instrumentation rows and executed configs are preserved, and no native child had started. An explicit v2 instrumentation protocol and freeze preceded all actual native source outputs. The first inventory scorer expected assessment.evidence, while native JSON uses evidence_class; its frozen code and failed outcome remain, and the read-only corrected v3 scorer reuses exactly the same native outputs. A stricter claim-quote equality hypothesis failed on3/13 supports; the first audit source/outcome remain and the second recount preserves the10/13 versus13/13 distinction. No native rerun replaces a first actual native output.

Reproducible first checkpoint: native_source_free_v1/first_evidence.zip contains342 exact-byte measurement payloads,939020 raw bytes, compressed to305542 bytes, SHA256 ec997245df3caf656c5cc805ec5d6ab79201e62f5888480a4aab78450c28f200. All342 hashes verify. It includes the prepared source-only wrappers, first instrumentation failure configs/ledger, all48 actual output streams/configs/process metrics/read-only knowledge snapshots, first results and source-binding recount. It excludes databases, credentials, model weights and validation.

Fourteen second-launcher mechanism tests passed before actual native outputs; the corrected scorer's14 tests also pass. These are wrapper, binary-stream, child-exit, UTF8 and provenance mechanism checks, not native semantic-quality measurements. The same-author source audit separately verifies241 first raw output files and all84 observation bindings; it is not an independent review. A read-only verification command recomputes the full saved inventory and source-binding result exactly, verifies all342 archived payloads, and runs no native process or API.

Decision: keep this current native baseline and its raw/source preservation measures. Investigate actor identity in the derived observation contract, stock predicate/category scope, and EvidenceRef locator granularity before designing a new native relation extractor. A future extractor experiment should freeze a source-only typed/actor/polarity ABI and development protocol first, compare source assertions with separate precision/recall and unknowns, preserve this stock baseline, and leave sealed validation unread until the independent integration owner authorizes its one-shot use.
