# Ecosystem agent mechanism experiment

This reference adds an injected action/observation loop above Loom's existing
local `LeaseStore`. It is a research seam, not a deployed agent or a replacement
for native `TaskEngine`. ChatADHD, other applications, planner implementations
and execution environments are independent bindings.

Implemented adapters: existing GraphPacket codec callback, real Linux process
with explicit argv/cwd/environment and process-group cleanup, and a synchronous
MCP stdio subset tested against a separate synthetic application process.
`connections.py` compiles/parses OpenRouter protocol data and describes a
selected GCP instance **offline**. It does not load credentials, call a model,
connect to a VM, provision resources or enforce nested provider budgets.

Run from repository root with Python 3.12 and the repository's `jsonschema`
dependency available:

```sh
python -m unittest discover -s loom/tools/agent_runtime_v1 -t . -v
python -m loom.tools.agent_runtime_v1.experiments --output /tmp/new-agent-evidence
```

Five tracks use independent temporary stores and execute concurrently. Returned
evidence includes complete typed domain outputs, exact MCP frames in the
composition track, process byte prefixes/full-stream hashes, crash receipts,
budget admission results and an instrument/source manifest. The deterministic
planner's next process arguments depend on the observed application output.
Completed actions replay without invoking a tool or planner again. An uncertain
dispatch blocks continuation; creating a new session to repeat an effect is an
explicit operator choice and is not made safe by a different identifier.

The runner removes temporary journals after collecting evidence. Use
`AgentRuntime` with a caller-owned durable directory to retain journals. Local
leases are not distributed locks. Process cwd is not a sandbox; detached
descendants can escape process-group cleanup. Process capture limits bound
returned prefixes, not temporary disk growth. File journals deliberately fail
closed on partial/corrupt JSON; automatic repair and power-loss durability of
the complete directory hierarchy are not established by SIGKILL tests.

The MCP adapter rejects unsupported versions, missing tools capabilities,
pagination, missing declared output schemas, unexpected frames, changed tool
descriptors, and transport limits visibly. Full MCP SDK behavior, HTTP, server
notifications, tasks, auth, concurrent session sharing and remote cancellation
are outside this subset. A completed local dispatch receipt establishes that
the callback returned and was recorded; it does not establish content truth or
successful external effects. Tool-error results remain observations; unknown
effects block the loop.

See [protocol](../../../docs/research/ecosystem_agent_v1/PROTOCOL.md) and
[research report](../../../docs/research/ecosystem_agent_v1/REPORT.md).
