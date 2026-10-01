# W1 — scale and exceptional-path hardening

Date: 2026-10-01 Europe/Amsterdam. Scope: W1 overnight queue item 5.

## Result

Measured and hardened the durable ActiveTask authority, streaming cancellation
and callback-exception paths without adding a product input ceiling, changing a
public ABI or calling a model.

The durable authority previously validated inherited evidence by walking the
entire predecessor chain for every accepted snapshot. A synthetic linear chain
showed load-only times of 149 ms at 100 revisions, 643 ms at 250, 1.784 s at
500, 3.589 s at 1,000 and 16.822 s at 2,000 on the retained local probe. The
validator now checks the equivalent local recurrence: current sources, then the
direct predecessor's sources, then its already-retained inherited evidence,
with the same message-ID deduplication and order. Every acceptance, including a
compatible replay of an existing product identity, is still validated. This
removes the repeated ancestry traversal; growth caused by evidence that is
actually stored in the snapshots remains visible rather than hidden by a hard
limit.

Independent review found a separate fail-closed defect while reviewing that
change. A successor acceptance could appear before its predecessor in the
append-only event sequence and become valid retroactively when the predecessor
was appended later. Explicit acceptances now require an earlier acceptance
sequence for their predecessor. Legacy upgrade observations remain exempt from
this chronological claim because their append order is an observation order,
not their historical acceptance order.

Two exceptional-path failures were reproduced and fixed:

- a C++ exception thrown by a C ABI stream callback was caught at the ABI
  boundary but left its `request_id` in the cancellation registry; the pre-fix
  `loom_chat_cancel("rq_throw")` returned `LOOM_OK` after `loom_chat_ex` had
  ended. An allocation-free RAII registration now removes the entry on every
  exit, unregisters before the terminal callback as before, and keeps callback
  invocation outside the registry mutex;
- when two SSE events arrived in one raw transport chunk, cancellation from the
  first callback still delivered and persisted the second event. The reproduced
  pre-fix result was `cancelled=1`, delivered/result/assistant text `AB` after
  cancelling on `A`. Cancellation is now checked per event and after each user
  callback, aborts the sink immediately, and is also rechecked when an
  unterminated final event is dispatched by `SseParser::finish`.

## Changed files

- `loom/src/chat/active_task_acceptance.cpp`: local inherited-evidence
  recurrence and causal sequence validation for explicit successors.
- `loom/src/chat/chat_engine.cpp`: per-event, post-callback, post-feed and final
  parser cancellation checks.
- `loom/src/capi/capi_chat.cpp`: exception-safe request registration lifecycle
  and duplicate callback outside the mutex.
- `loom/tests/test_chat_active_task_acceptance_scale.cpp`: 1,001-revision,
  three-page authority gate, malicious compatible replay and reversed causal
  order rejection.
- `loom/tests/test_chat_active_task_exceptional.cpp`: pre-cancel, coalesced SSE
  cancellation, content/reasoning callback exceptions, exact replay and
  unterminated-final-event cancellation.
- `loom/tests/test_capi.cpp`: public C ABI callback-exception cleanup proof.

No file, source, branch, worktree, history, log or checkpoint was deleted. No
frozen instrument, sealed/holdout corpus, shared `docs/STATE.md`, coordination
index, main, integration branch, PR6 or W2 file was changed.

## Scale evidence

Two independent read-only probes agreed on the quadratic ancestry-walk
bottleneck. A second profile measured 100/250/500/1,000 revisions at
0.159–0.182/0.587–0.852/1.385–1.416/4.20–4.93 seconds. Sequential loopback
acceptance reached 15.60 seconds for 50 revisions, including the growing chat
history and HTTP fixture. A 1,201-replay authority crossed three EventLog pages
without a paging correctness loss.

The compiler itself showed no scale defect or recursive overflow through
25,000 flat statements and a 25,000-node supersession chain. Representative
times were 3.258 s and 3.074 s with high-water RSS 78.2 MiB and 68.4 MiB,
respectively. These are synthetic samples, not supported maxima or policy
ceilings.

The committed 1,001-revision authority test, including event construction and
two authority loads, passed 2/2 cases and 2,016/2,016 assertions in 0.768 s in
a focused run. The final full run reported that test at 0.94 s.

## Verification

The exact implementation tree was built in the preserved Release build at
`/dev/shm/w1-build`; the overlay filesystem was already full, so no prior build,
checkpoint or probe was removed. Provider-facing tests used only the in-memory
scripted transport or local loopback.

```text
unit.test_chat_active_task_acceptance_scale
2/2 cases, 2016/2016 assertions passed

unit.test_chat_active_task_exceptional
4/4 cases, 45/45 assertions passed

C ABI callback-exception filter
1/1 case, 7/7 assertions passed
```

```text
89/89 configured CTest entries passed
0 failed, 0 skipped, 0 disabled
67.44 s
```

JUnit evidence at `/dev/shm/w1-scale-exceptional-full-final2.xml` reports
`tests=89`, `failures=0`, `skipped=0`, `disabled=0`; SHA-256:
`91419e82e29f05de85c218602168f5fa4a09d503bfab62e2416b36382ab0f8d0`.
The public contract/reference discovery suite also passed 185/185 in 26.085 s.
There were zero paid inference calls and zero remote provider requests.

One earlier broad CTest attempt is not counted as a pass: `research.structure`
timed out at 60 s while independent builds were contending for the full
workspace. It passed alone in 35.45 s, then the exact final tree passed the
complete 89-test run above.

Tested source hashes:

| File | SHA-256 |
|---|---|
| `loom/src/capi/capi_chat.cpp` | `e0efd3bb287c251da2fce394725042a98cbc2649700df76a21fac9a0f099f915` |
| `loom/src/chat/active_task_acceptance.cpp` | `bbc6cf2576373fdba4db024b38657793ceb2d1a3710ebaf2c7930274426fefbe` |
| `loom/src/chat/chat_engine.cpp` | `3fc7f956295fe6b85456849341b7e3a87b654dd4997c4cdf4751c93886b7a191` |
| `loom/tests/test_capi.cpp` | `dd4d7a8957d95c137dcebbcd3a34678b60a2191d8ab8694bd474ea89412dff82` |
| `loom/tests/test_chat_active_task_acceptance_scale.cpp` | `1f287c7126aa744fc09411fc88aa6e04acb8daaa8103fff15c23270b736ede5f` |
| `loom/tests/test_chat_active_task_exceptional.cpp` | `389b547af4747b069c6ea8c9f535ddc85544d38ca35a9258b3f791a61321b24e` |

## Independent review

Separate agents independently profiled authority and compiler scale, audited
native and C ABI cancellation/exception paths, designed the scale gate and
reviewed the final changes. Review found and caused fixes for reversed
acceptance chronology, duplicate callback-under-mutex deadlock, terminal
registration lifetime and final unterminated SSE cancellation. The final C
ABI/stream review found no blocker; the authority review found the local
recurrence equivalent to the prior full walk when every acceptance is checked.

## Remaining risks

- A lineage that introduces a different unique source at every revision still
  stores and validates a quadratic amount of inherited JSON. That is real
  provenance payload, not the removed redundant walk, and needs a representation
  decision rather than an arbitrary rejection ceiling.
- Active acceptance still reloads authority more than once around preparation,
  baseline assurance and the transaction recheck. The per-load ancestry defect
  is fixed, but coalescing those reads is further optimization work.
- When an application callback itself throws after provider output begins, the
  accepted user row and authority remain durable and in-flight state is
  released, but a partial assistant row is not persisted. A retry may therefore
  make another provider attempt without a durable partial-output record.
- Direct reentrant tests for the duplicate-ID terminal callback and terminal
  `loom_chat_cancel == LOOM_E_NOT_FOUND` are not yet present, although the
  independently reviewed callback is now outside the lock and normal
  unregister occurs before terminal delivery.
- Scale samples used synthetic local histories and do not claim extraction
  quality or real-provider latency.

Queue item 5 is complete. The next bounded work is queue item 6: prepare the W2
handoff without editing W2 or shared integration state.

## Publication

Immediately before publication, the approved public W1 branch pointed to
remote commit `86056452477659b69568b9ff9286f4d78b84baa4`, tree
`4a5431f07cfc48359281c80bffc86af94acd1727`.

The frozen local implementation checkpoint is
`7831aa11c8997110c03e0cd0d8e8f5acd4879a45`, tree
`4e4ff3662c70e8dc6e19aa92c03e7d1a3f72ba9e`. GitHub created remote commit
`13b573ccc238db40a9f40bca3dc22c4a95ae2b55` with parent
`86056452477659b69568b9ff9286f4d78b84baa4` and the exact same complete tree
`4e4ff3662c70e8dc6e19aa92c03e7d1a3f72ba9e`. The branch ref was advanced with
`force=false` and read back at the remote implementation commit. Local and
remote commit SHAs differ because the GitHub connector authored the remote
commit; complete-tree equality proves content identity.
