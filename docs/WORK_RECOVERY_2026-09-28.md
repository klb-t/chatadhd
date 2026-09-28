# Work session interruption and recovery

The owner reported that the interface displayed active agents for several hours
without usable progress; pressing Stop revealed unfinished work. This is an
incident record, not a confirmed root-cause diagnosis of ChatGPT infrastructure.

## Evidence available after Stop

- The initial agent inventory contained only the coordinator. No old child agent
  remained available to query. This cannot reconstruct its state before Stop.
- The previous local repository and build directory were absent at the documented
  workspace path. Uploaded reports were present. This is consistent with a
  replaced/cleared workspace; neither its cause nor its timing is established.
- GitHub still held `b818366ffabdd6b1cbe14aab6e062805115a780b` on
  `codex/loom-handoff-2026-09-28`, tree
  `7352a6d7984bd94e097ab1834ffe848fe194987f`.
- Eighty selected research/configuration files were recovered from that exact
  remote snapshot and every Git blob hash verified. The local Git history is a
  synthetic partial snapshot. **Never push this local history.** Use the existing
  remote parent/tree and explicit file changes, preserving all omitted files.
- The previous native result, 71/72 CTest gates with the unchanged catalog recall
  failure, remains a recorded historical result. No native build was recovered
  or rerun in this research-only continuation.

There are no client console/network traces, app version, backend event timeline,
pre-Stop child inventory or approval queue logs available here. A stale UI status,
a backend/tool wait, lost event delivery and an agent coordination stall remain
distinct possibilities. We cannot identify which occurred or blame follow-up
messages, a browser, model choice or reasoning effort from the supplied evidence.

## Official guidance checked

OpenAI's [troubleshooting page](https://learn.chatgpt.com/docs/reference/troubleshooting)
advises checking pending approvals, testing whether a basic command still runs,
and recovering in a focused chat. It also describes feedback with a shared
session ID. That page is desktop-oriented; its menu/log paths need not exist in
the web client.

The [official changelog](https://learn.chatgpt.com/docs/changelog) records repairs
to interrupted streams/subagent-completion handling in Codex and to stale task
states/reconnections on other clients (including iOS 1.2026.244, September 8).
Those entries show that this class of failure exists. They do **not** establish
that this incident is the same bug or that a particular update fixes this web
session. Client updates help only where an applicable fix is actually shipped.

## Measures adopted for this repository

1. Break the programme into finite increments with observable files, tests and a
   checkpoint. Preserve the wider research backlog independently of each increment.
2. Give each child explicit file ownership, a small deliverable and a requirement
   to return partial findings/blockers early. Keep only useful concurrent work.
3. Check activity after a stalled expected milestone; do independent work instead
   of waiting indefinitely. Request a partial handoff, then interrupt a child
   that cannot make progress. Preserve its written changes for inspection.
4. Use bounded shell commands and short output waits. Progress means observable
   output or artifacts, not merely a UI `running` label.
5. Keep a concrete work-status file and push completed increments before starting
   another broad exploration. Verify remote file hashes after publishing.
6. Before ending a coordinator turn, reconcile child statuses and record anything
   unfinished. Never imply that unverified children will continue indefinitely.

These are recoverability and coordination measures, not a watchdog that can
repair the hosting platform when the coordinator itself is suspended.

## If the interface freezes again

Capture the last visible operation, approximate start/end time with timezone,
task/thread ID and client version if available. Check for a pending approval.
If no progress is observable, Stop and resume from the repository's work-status
file rather than trusting the activity animation. Use the product's available
feedback/report control and include the session identifier; no report has been
sent on the owner's behalf. Only the operator with client/backend diagnostics
can distinguish UI desynchronization from an execution stall conclusively.

Suggested issue text, for review before sending:

> During a long ChatGPT Work session with delegated agents, the UI continued to
> show agents as running for several hours without visible progress. Pressing
> Stop exposed unfinished work. Please correlate the thread/session event log
> with child task completion/cancellation and tool-call states. The repository
> checkpoint survived on GitHub; the prior execution workspace was unavailable
> when the task resumed. The latter observation may be unrelated. Attach the
> actual client version, thread ID, timestamps and last visible operation.
