# EA — Shared ecosystem agent mechanism and access contracts

Base: `33fb30a5e8c226b64e67ee6531304046392b6ad2`.
Branch: `gpt/ecosystem-agent-research-2026-10-01`.
Protocol: `f6012e87ad4f6bc9f07ba69c316d1082fe74c4b9`.
Tested implementation: `f8dabcc008416137b8b46492c072f3e7f16764d4`.

Owner: shared ecosystem executor, independently attachable to applications and
systems; manual VM bootstrap is separate. User selection of environment is
preserved even when unavailable or less convenient.

Scope is only `loom/tools/agent_runtime_v1/`,
`docs/research/ecosystem_agent_v1/` and this receipt. No native code, ABI,
contract schema, migration, existing Python module, global STATE/index/budget
ledger, private archive or deployed application changes. Built on current
night-development checkpoint to avoid changing the active integration branch.

Mechanism: injected planning loop + tool/environment descriptors, actual
GraphPacket codec, local subprocess and limited MCP stdio adapter. Existing
LeaseStore owns action leases, receipts, replay and uncertain-effect blocking.
Existing AnalysisPlan demonstrates admission around the loop; it does not
transfer/enforce a child provider budget. OpenRouter and GCP connection data
compile/parse offline. See [report](../../research/ecosystem_agent_v1/REPORT.md).

Validation: 41 new tests + 50 coordination regressions + 50 GraphPacket/plan
regressions = 141 passed. Five concurrent experiments completed with verified
output hashes on a clean source checkpoint. SIGKILL before effect yielded 0
effects; after effect yielded 1; neither repeated on restart. Graph/MCP/process
composition replayed without repeated dispatch; budget denial invoked no agent.
Zero paid calls and VM provisioning. Bubblewrap isolation probe failed on
NETLINK_ROUTE permission; local process is not a sandbox. No native CTest claim.

Integrator can review these files independently. No patch to canonical graph or
native scheduling path needs integration. Live model/VM adapters and production
embedding remain separate next work requiring credentials, selected resources
and an explicit budget. [Scoped state and evidence](../../research/ecosystem_agent_v1/STATE.md).
