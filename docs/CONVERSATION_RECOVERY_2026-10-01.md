# Conversation and artifact recovery — 2026-10-01

Scope: ChatADHD/Loom work from 2026-09-29 through 2026-10-01, including the six
W1–W6 conversation packages and their later integration. This is a bounded
recovery inventory, not a claim to have read every message from 36 agents.
The candidate examined was `33fb30a` on `gpt/night-development-2026-10-01`;
the final integration status and current tests belong to [STATE](STATE.md).

## Coverage and limitations

The six conversation titles are visible in the owner's supplied activity
context. Their published branch heads, receipts, source and test artifacts were
examined. Direct conversation retrieval returned “Personal context is
unavailable for this conversation.” Consequently, the full coordinator
transcript, all child-agent transcripts and all interrupted tool output cannot
be enumerated or certified complete. “Six lanes recovered” does **not** mean
“36 agents fully audited.”

| Conversation/package | Recoverable checkpoint | Finding at candidate `33fb30a` |
|---|---|---|
| Start z W1 / active task | `gpt/w1-active-task-2026-09-30` at `803eb78` | Initial `ba792b9` is integrated. Eight later commits through `803eb78` are not ancestors of the candidate; they need explicit reconciliation, not a wholesale overlay of an incompatible acceptance journal. |
| Lot W2 / retrieval | `gpt/w2-retrieval-2026-09-30` at `f6b44f6` | Branch is an ancestor. Receipt identifies five delegated lanes and cross-review; this is evidence of delegation, not proof of six complete child transcripts. |
| Potwierdź publikację W3 / recipes | `gpt/w3-model-recipes-2026-09-30` at `078378d` | Branch is an ancestor. Original three-commit bundle independently recovered and checked; no extra unpublished implementation in that bundle. New recipe quality remains unmeasured. |
| W4 gotowy do integracji / graph runtime | `gpt/w4-graph-runtime-2026-09-30` at `e7000a9` | Branch is an ancestor. Independent adversarial review/tests are recorded. Its interrupted full-structure log is not a passing result. |
| Publikacja W5 na GitHub / workspace | `gpt/w5-workspace-2026-09-30` at `ecf6931` | Branch is an ancestor. Original four-commit bundle independently recovered and checked; prior publication blockage was already resolved. |
| W6 zapisane / evidence | `gpt/w6-evidence-2026-09-30` at `84b5a83` | Branch is an ancestor. Import, transport, local export audit and cross-reviews are recorded; six semantic fixtures do not constitute an executed quality evaluation. |
| Plan prac ChatADHD / coordinator | Integration `926072c`, then night candidate `33fb30a` | Published integration and N1–N6 assignment records recovered. The candidate's STATE still reports pending combined native verification; the old 94/94 result is for `da77c76`, not this candidate. |
| Earlier handoff/continuity | `6828216`, `2fd21bd` | Existing reports distinguish recovered GPT/Jev results from missing original `semantic_sketch` and shared-lease source. Neither missing prototype was recovered by this pass. |

The artifact-level historical map is preserved at
`33fb30a:docs/coordination/PUBLICATION_STATUS.md`; use `git show` with that
revision if the original receipt directories are archived. Historical test
counts below are retained claims from their specified snapshots, not fresh test
runs by this recovery audit.

## Saved packages checked independently

Both ZIP files were obtained from the owner's existing saved artifacts, unpacked
outside the public source tree and checked against their manifests. Both Git
bundles pass `git bundle verify` against base `b118c80`. The W3 manifest's six
file hashes and W5 manifest's twelve file hashes all match.

| Artifact | Exact SHA-256 | Original history and published equivalent |
|---|---|---|
| `ChatADHD_W3_handoff_2026-09-30.zip` | `4ebf7ea7d5d896ab629c7e575a63b61ba468f8290177ae49d7dfcab05f52ef7f` | Three original commits ending `bdf86d10543c2487b59fc729ee0099341ecd4dab`; tree `cda488ba218a6ee8e07da3a5debf6c615d671fd5` exactly equals published `729699fe8dab6dd696b133bd277d3717c3bbd055`. |
| `W5-workspace-2026-09-30.zip` | `6f13c295b097dc119f24a3f13b9461f2190ba90ff58b36ba2532ae70f40fbecf` | Four original commits ending `e03bd02d42c8235b58f035a8689e593b4d570e95`; tree `9e75bd383a9e75f03dce39bfb48f837f0418f30b` exactly equals published `411b18be4d8a8906d9587b1a5761861aad089f47`. |

W3's later published head `078378d` changes only two documentation files to
record publication. W5's later `ecf6931` adds its publication receipt. The
original bundles' “not pushed” text is therefore historical, not an unresolved
blocker. Original commit histories were restored to local recovery refs
`refs/recovery/library-w3` and `refs/recovery/library-w5`; archive branches must
retain those histories as well as the equivalent published trees.

Three saved STATE copies (`STATE.md`, `STATE(1).md`, `STATE(2).md`) are byte
identical: 11,095 bytes, SHA-256
`0ccbac91b8182b505430b6839b3553f29aba312795d924fbfdb3b96823295d02`.
The three accompanying README copies are also identical: 7,529 bytes, SHA-256
`43795f10bac94fc6b63072f9968be37c83083f9320d4f016d6d66aefc0f77c9d`.
These are shared starting documents, not six independent completion reports.

## Work recovered from interrupted or stale handoffs

| Item | What the evidence resolves | Remaining action/boundary |
|---|---|---|
| W3/W5 publication | Exact implementation trees are already published and included in the candidate. | Preserve original bundle histories; do not reapply their patches or republish duplicate implementation. |
| W1 late compiler increment `c016eb1` | Published source and receipt preserve exact locator/time-ordering work. | Compare with candidate compiler; adopt compatible fixes/tests on the linear integration line. |
| W1 late exceptional/scale increment `13b573c` | Source and receipt preserve authority recurrence/chronology, C ABI cancellation cleanup and coalesced-SSE cancellation fixes. Historical full gate is 89/89 at its own snapshot. | Reconcile with the candidate's distinct authority representation; do not transfer the 89/89 claim to a combined tree. |
| W1→W2 handoff `e853f43`, `803eb78` | Handoff queue item 6 is completed on W1. It explicitly recognizes integration's W2 adapter `6b0d79b` and avoids editing W2. | Retain useful boundary tests and current interface explanation. Do not rebuild the already integrated adapter. |
| W3 directed recipe | `directed_refute_v3`, 48 prepared DEV requests, profile compatibility and replay of 96 historical responses are present. | No fresh quality result follows from replay. Three proposed 48-call batches were prepared, not executed; the original budget does not reset. |
| W4 interrupted structure run | `structure-incomplete.log` lacks a unittest summary. Separate 50/50 coordination, 185/185 contract and 27/27 graph results are recorded. | Combined final CTest is the completion gate; never turn the interrupted log into a pass. |
| W6 importer findings | Bounded OpenAI source-fidelity evidence and synthetic Anthropic counterexamples survived; later integration corrects importer behavior. | Full real-export/real-Anthropic semantic quality remains unestablished; fixtures and source fidelity do not prove it. |
| N1–N6 night continuation | Assignment and published implementation exist, while candidate STATE still cites older verification. | Finish current integration and fresh combined tests; no assumption that the old coordinator remained running. |

The W1 exceptional-path receipt also records two real remaining boundaries:
callback exceptions after provider output do not persist a partial assistant
row, and lineages adding a distinct source at every revision retain quadratic
provenance payload. These are scoped future engineering work, not missing
historical experiments or grounds for inventing an input ceiling.

## Irrecoverable-so-far source and evidence limits

The earlier continuity audit preserves a specification for `semantic_sketch`
(`compile_sketch`, alternative readings, scoped/bound occurrences and exact
UTF-8 source grounding), but its original implementation, final hash and test
sources remain unavailable. Its reported 20 tests and 48-mutation exercise
must not be described as a reproducible current result. A filename scan across
the available scratch workspaces and a bounded saved-artifact search did not
recover those files. Exact searches in the located semantic quotation archive
found neither `semantic_sketch` nor `SEMANTIC_SKETCH_PROTOCOL`. The earlier
audit also decoded two conversation captures without finding them. These are
bounded negative searches, not proof that no private copy exists.

The lost shared-lease checkpoint is a separate incident: local `c26c525`, tree
`f460c2886faec628f63e1e2c0a50f48fa8443cba`, was reported unavailable before a
remote ref update. The historical 851-test structure run included 30 tests from
that unpublished source. New coordination code was subsequently implemented;
it is not recovery of those original bytes. See historical
`33fb30a:docs/research/RECOVERY_COORDINATION_2026-09-30.md` and
`33fb30a:docs/coordination/receipts/CONTINUITY-2026-10-01-bounded-recovery.md`.

No private conversation contents, export bytes, credentials, signed download
URLs or personal-context responses are publication inputs for this document.
No paid inference or product-test rerun was performed by this recovery pass.
