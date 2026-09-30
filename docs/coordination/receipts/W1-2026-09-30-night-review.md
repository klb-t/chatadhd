# W1 overnight independent source review

Date: 2026-09-30. Reviewer: separate `concurrency_validation` agent.
Base SHA: `83ed579e94e48edc4cba68104ff0b22cfb66ec4e`.
Review target: the coordinator's uncommitted next increment; final head SHA and
execution evidence belong to the coordinator's implementation receipt.

This receipt records source inspection and independently authored synthetic
counterexamples. This reviewer did not build the code, run tests, invoke a
model, inspect sealed evaluation data, commit changes or publish a branch.
Do not interpret the case inventory below as an execution result.

## Scope

- `loom/src/chat/chat_engine.cpp`: retained task validation, exact revision
  ancestry, inherited source coverage, callback persistence and lock scopes.
- `loom/include/loom/chat_engine.h`: pending acceptance state when present.
- `loom/tests/test_chat_active_task_concurrency.cpp`: four independently
  authored thread/callback scenarios, owned by this reviewer.
- The separate revision and retention test suites provide additional fixtures;
  their authorship and execution are not claimed by this reviewer.

## Findings

1. **Native source version coverage must follow the newest accepted ancestor.**
   A three-revision counterexample exposed an over-strict sibling check: v1
   binds source A; a native edit creates A2; v2 explicitly binds A2; v3 binds
   only a new correction C. Looking only at v3's explicit binding set rejects
   A2 even though the newer ancestor v2 already covers it. The correction is
   to consult accumulated coverage, with current explicit sources first and
   then ancestors from newest to oldest. This changes coverage only; it does
   not infer, combine or resurrect statement content.

2. **Restoring callback metadata after the callback is insufficient to protect
   concurrent acceptance.** A accepts product v1; its callback clears
   `metadata.active_task`; a nested/concurrent B can then accept a distinct v1
   before A restores its metadata. Both requests can reach transport, leaving
   competing roots. A separate process-local pending-acceptance authority is
   required to close this in-flight window without holding the database or
   ChatEngine mutex across callbacks. Inspected correction: the same-engine
   in-flight map is published under the database lock before callbacks,
   overrides mutable rows during subsequent preparation, and is removed by
   an RAII guard on every return/exception. The map is protected by Database,
   not the engine mutex. This closes the identified `on_start`/message-created
   window within one engine instance.

3. **Accepted metadata needs recovery even when the current message changed.**
   A callback that replaces metadata and changes the current text must not
   erase the already accepted task merely because transport is correctly
   rejected. Recovery must precede the changed-turn rejection, while retaining
   conflicting callback metadata as evidence. This is acceptance provenance;
   it is not evidence that a provider received the request. Inspected recovery
   now occurs before the changed-text/role/conversation/attachment rejection.

4. **Lock and lifetime checks.** Exact ancestry pointers refer to the completed
   local collection of retained task records, which is not mutated during
   traversal. Database coordination spans task
   validation, message construction and initial user-message persistence.
   Message-created handlers, `on_start`, and provider calls remain outside
   both Database and ChatEngine locks. The context builder is copied under
   the engine mutex and then invoked after that mutex is released; active-task
   preparation still holds the recursive Database lock around that builder.

## Follow-up findings and final disposition

5. **Reentrant context construction can invalidate pre-acceptance checks.**
   The custom `KnowledgeContextBuilder` executes after initial task preparation
   and before outer acceptance. Because Database is recursively locked, a
   builder can reenter `send` with a distinct v1 and omit knowledge context on
   the nested call. That nested task can complete before the outer task reaches
   the in-flight map. Inspected correction: `send` revalidates the task under
   the existing Database lock after `build_messages`, compares the full fresh
   task snapshot to the prepared snapshot, and rejects changes before creating
   the outer user row. Retention tests exercise both competing nested v1 and
   source text/role mutation during construction.

6. **Throwing callbacks must recover acceptance before dropping transient
   authority.** A callback that clears metadata and throws bypassed ordinary
   post-callback recovery. Inspected correction: callback execution is wrapped
   with exception recovery using the same metadata-retention helper before
   rethrowing the original exception. The RAII guard then removes transient
   authority. Retention tests exercise metadata replacement plus throw, preserve
   the original exception message, and check that transient entries disappear
   on success, provider failure and exception. Recovery remains best effort
   when the callback removed the row or storage itself fails.

The final inspected source also validates retained source UTF-8 and optional
quote occurrence against retained text, in addition to digest/binding consistency.
No remaining blocking issue was identified within the stated same-engine,
process-local boundary. This is source-review signoff, conditional on the
coordinator's build and execution results; no test pass is claimed here.

## Independent test scenarios authored

- Two distinct v2 products start from the same accepted v1: exactly one is
  accepted, persisted and transported; the losing follow-up is not persisted.
- An accepted v1 pauses in `on_start` while v2 finishes. Reversed transport
  order must preserve each request's own compiled instruction and trace.
- `on_start` permits another thread to acquire Database then inspect ChatEngine
  and edit a source; the accepted request retains the original source snapshot.
- A knowledge-context builder holds Database while another thread reads the
  engine and replaces the builder. The in-progress request uses its copied
  builder, and preview persists no accepted revision.

Synchronization uses promises/shared futures and bounded waits, without sleep
based scheduling. Workers retain shared ownership of their fixtures; a failed
bounded wait cannot cause access to destroyed stack state. Successful workers
are joined. This checks the specified interleavings and lock scopes, not every
possible scheduler interleaving, formal deadlock freedom or distributed locking.

## Remaining boundaries

- Native source binding verifies source identity and optional quote occurrence;
  it does not establish semantic entailment of caller-authored statements.
- Retained metadata is mutable application storage, not a tamper-proof journal.
  The in-flight map protects the same-engine callback window but cannot
  recover arbitrarily removed historical records, another engine instance's
  independent state, another process's writes, or acceptance state lost in a
  process crash. A callback that moves the originating row retains metadata
  on that moved row and stops transport; this does not create an independent
  acceptance journal in the original conversation.
- Coverage removes selected source messages from the history channel. Other
  memory/context channels retain their independent behavior.
- Snapshot files/attachment references do not freeze the external bytes of an
  attachment for future requests.
- An accepted specification and compiled trace do not prove provider receipt
  or completion. Provider failure does not undo acceptance.
- Build/test counts, final commit identity, and publication status must be read
  from the coordinator's execution receipt, not inferred from this review.
