# Shared ecosystem agent: frozen first experiment protocol

Date: 2026-10-01. Base: `33fb30a5e8c226b64e67ee6531304046392b6ad2`.
Scope: this directory, `loom/tools/agent_runtime_v1/`, and one scoped coordination receipt. No native ABI, database migration, shared integration ledger, paid provider call, cloud provisioning, or private archive publication.

## Owner requirement

The manual machine bootstrap is separate from the agent. The agent is an integral, attachable ecosystem component for applications and systems. ChatADHD is one client. An application adapter, planning implementation, and execution environment must be independently replaceable. A user may select a machine even when another environment would be more convenient. Unavailable selected environments must produce an explicit result rather than silent fallback.

## Hypotheses and predetermined observations

1. One action/observation loop can compose an existing Loom graph operation, a real OS process, and a separate application exposed through MCP stdio. Record each intermediate typed result and a final result derived from it. Use an injected deterministic planner; this measures composition, not model planning quality.
2. Existing local `LeaseStore` can prevent an implicit duplicate dispatch after loss of acknowledgement. Kill a worker after a real file effect; restart the same action and check that the file effect occurs once and the task remains `outcome_unknown`. This is local coordination, not globally exactly-once effects.
3. Explicit environment and schema bindings survive restart. Check unknown environment, capability mismatch, changed descriptor, invalid input/output, typed unit mismatch, and new application registration. Preserve the first planner response and tool return before validation; retain failures.
4. A process adapter preserves byte output, exit status and timeout evidence. Test non-UTF8 output, nonzero exit and a process group containing a child. A local process is not a sandbox; report available isolation mechanisms without claiming VM/container isolation.
5. Existing `AnalysisPlan` admission can wrap the shared loop. Test admitted and exhausted budget with no dispatch in the latter. Do not claim a transferred nested budget or production metering.

Independent tracks will run concurrently where their temporary stores and outputs are disjoint. Use only synthetic fixtures already public in the repository and new synthetic application data. Record all failures, source identity, environment facts, test count, raw observations and money/provider/network counts. No tuning based on a hidden evaluation set, no sealed holdout access.

## Decision gates

Successful local experiments justify an adapter seam, not a production integration or new service. MCP can map tool discovery/calls; A2A can later map independently deployed agents. Preserve domain-specific GraphPacket fields and application units. A common envelope must not flatten them into an untyped graph. Execution choices include in-process, local process, container, VM and remote endpoint as capabilities; this experiment only implements the first two. Browser/GUI, credentials, actual model planning, remote cancellation, physical devices and provider budgets require separate evidence before claiming support.

## Reproduction

Run `python -m unittest discover -s loom/tools/agent_runtime_v1 -t . -v`, then `python -m loom.tools.agent_runtime_v1.experiments --output <new-directory>`. Python dependency: `jsonschema` (also required by existing AnalysisPlan). Results must reference the implementation commit; generated result changes follow a separate commit.
