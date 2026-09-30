# W1–W6 — publication and integration status

Updated 2026-10-01, Europe/Amsterdam, by ROOT after the owner asked to reconcile
the separate conversations with GitHub. Repository: `klb-t/chatadhd`.

**All six latest located deliverables are now published on their own branches.
They are not yet integrated into one tested product revision.** W5 was the only
unpublished completed package found in this audit; ROOT recovered and published it.

The integration baseline is `b118c80e981c08ec6d7f9ab6aacc177979186cf2` on
`gpt/research-2026-09-30`. The publication-status update after it changes only
documentation. Its existence must not be mistaken for merging W1–W6 code.
`main` is not the current Loom integration line.

## One map of the six conversations

| Lane / remote branch | Published HEAD | Delivered | Remaining work |
|---|---|---|---|
| W1 — `gpt/w1-active-task-2026-09-30` | `ba792b920fba36fb738ff39421d27b6d965aa11a` | Supplied ActiveTaskSpec compiled into real chat, source coverage, revisions, replay/edit/rebinding, C ABI and HTTP/SSE path | Durable acceptance history: two reproduced metadata/message relocation failures. Does not automatically infer an ActiveTaskSpec from conversation prose. |
| W2 — `gpt/w2-retrieval-2026-09-30` | `f6b44f6059777892474b45e54e7fae516bd1eb04` | Per-thesis scope/detail, explicit claim/counter-evidence selection, graph/TF-IDF channels, lexical shadow and diagnostics through `loom_context_build` | Chat's option parser still rejects the new fields. Integrate with W1; real-export semantic quality remains unestablished. |
| W3 — `gpt/w3-model-recipes-2026-09-30` | `078378d5fa65225b5978e2641dd1849cd4830536` | `directed_refute_v3`, 48 prepared DEV requests, compatible profiles and replay of 96 historical responses | No new model calls or measured effectiveness of the new recipe. Any live run retains existing budget/first-response accounting. |
| W4 — `gpt/w4-graph-runtime-2026-09-30` | `e7000a9beda08769254f1893da5ed3a5097bfe58` | Local `analysis-graph` CLI links AnalysisPlan, resource budgets, dependent graph transforms and durable coordination | Output is GraphPacket projection; writing results to the native graph/store remains unconnected. |
| W5 — `gpt/w5-workspace-2026-09-30` | `ecf6931bdde797799d2a1081ff898d3d0f1d3c8c` | Durable views, independent references, parameter couplings, saved perspective and run-correct inspectors | Browser-origin persistence, one perspective slot, no cross-device sync or immutable server snapshot. Integration/browser regression after concurrent web changes remains. |
| W6 — `gpt/w6-evidence-2026-09-30` | `84b5a83ccbdbb83d3b154545c3bd5e32213a8dcd` | Published import/transport instruments, counterexamples and bounded real OpenAI export checks | Anthropic ordering/exact reconstruction, self-contained source locators and structured unknown wrapper fields. Real Anthropic was not measured after a download timeout. |

Each listed branch is based on the same integration baseline. No lane is
contained in that baseline; W1 and W2 are also not merged with each other.
These are observed checkpoints, not a claim that no agent could have newer
uncommitted work outside the audited sources.

## What the retained evidence actually supports

| Lane | Evidence checked in this publication audit |
|---|---|
| W1 | Stored full CTest **83/83**, 0 failures/skips; source/artifact hashes match. Separately documented negative examples still expose the durable-acceptance gap. |
| W2 | Stored full CTest **84/84**, 0 failures/skips; 24 source hashes match. DEV ranking failures are retained. |
| W3 | **20/20** included in **841/841** structure tests; separate profile suite **35/35**. Replay evidence is not a fresh model-quality experiment. |
| W4 | **50/50** coordination, **185/185** contracts, **27/27** graph tests. `structure-incomplete.log` is incomplete and does not establish a full structure pass. |
| W5 | Build passes; **8/8** state groups, **10/10** native browser scenarios, mocked semantic-controls pass. **16/16** browser regression is explicitly an earlier checkpoint. Seven implementation hashes, six logs and 258 native source hashes match. |
| W6 | Bounded real OpenAI slice: **100/100** conversations, **730/730** raw messages, **9020/9020** typed atoms with source-byte fidelity. Import checks **47/59**, later **23/24** with Anthropic exact reconstruction failing. Latest transport **72/72**; earlier **68/68** is separate. Six semantic cases are fixtures, not an executed quality evaluation. |

The audit read the retained evidence and checked hashes/counts. It did not rerun
product tests or model calls. Denominators overlap and must not be summed. The
integration baseline's earlier **77/77** is not a test of all six branches merged.
Real OpenAI evidence covers a limited slice without asset bytes, not the whole
export. W6's historical harness checkout `62639f7e7b5fcfabffbd56ed74a80bcf759b9800`
was unavailable in fetched refs; the instrument's SHA-256 matches the published
source and its three binaries match the baseline verification record.

## W5 recovery and publication

The source conversation stopped after an automatic approval rejection; the saved
`W5-workspace-2026-09-30.zip` held four local commits and their evidence. ROOT
verified the package checksums and Git bundle, restored the branch, checked the
destination repository and existing publishing authorization, and obtained an
independent read-only payload review. The 15 changed paths contain web code and
synthetic test evidence, no deleted paths, private source archives or credentials.

Git transport had no available credentials. Publication through the authenticated
repository connection recreated the four source commits with new commit IDs;
**every corresponding Git tree is identical**. Original commits remain in the
saved source bundle. A fifth, documentation-only commit records publication and
supersedes the original receipt's historical “NOT pushed” label.

The W5 branch records original → published SHAs in
`docs/coordination/receipts/W5-2026-10-01-publication.json`.
Original payload HEAD: `e03bd02d42c8235b58f035a8689e593b4d570e95`;
published equivalent: `411b18be4d8a8906d9587b1a5761861aad089f47`;
identical tree: `9e75bd383a9e75f03dce39bfb48f837f0418f30b`.

## Older-thread continuity is a separate issue

`gpt/continuity-audit-2026-09-30` is published at
`6828216b23b465f59055ccd8ae3e665abf6bce97`, but not merged. Its receipt is
`docs/coordination/receipts/CONTINUITY-2026-09-30-old-thread.md`.
All 847 paths from the older Codex branch remain in the integration baseline;
790 blobs are identical and 57 changed. Earlier GPT/Jev results and scorer are
present; they do not need a paid rerun to “recover” them.

The older `semantic_sketch` prototype remains a bounded recovery gap: the audit
found its design/status description but not the original source, tests or final
hash. Its historical 20-test claim is not a current gate. This is separate from
W1's ActiveTaskSpec compiler and from the older lost shared-lease implementation.
Current new coordination code is already saved. Do not claim that all historical
unpublished prototypes have been recovered merely because W1–W6 are now on GitHub.

## Integrator queue

1. Keep this map and `../STATE.md` as the entry points; inspect fresh remote HEADs
   before continuing a lane. Do not restart already-delivered primitives.
2. Resolve W1's acceptance persistence boundary, then combine W1/W2 contracts and
   admit W2's new options through the real chat path. Preserve the counterexamples.
3. Integrate W3/W4/W5/W6 by scoped review, preserving each original receipt; run
   the relevant combined regression before marking a shared revision verified.
4. Route W6's import failures to the importer and the older semantic-sketch gap
   to recovery/coverage review. Keep unavailable real Anthropic data distinct from
   an importer success or failure measured on real Anthropic.

ROOT owns integration and STATE. No force-push, branch deletion, main merge,
deployment, new paid calls, or private archive publication occurred in this audit.
