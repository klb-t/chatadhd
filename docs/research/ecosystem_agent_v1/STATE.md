# Ecosystem agent research checkpoint

Owner correction: a manual machine bootstrap is separate from the attachable
ecosystem agent. This track implements and evaluates a shared agent mechanism,
not a ChatADHD-only VM agent.

- Base: `33fb30a5e8c226b64e67ee6531304046392b6ad2`.
- Frozen protocol: `f6012e87ad4f6bc9f07ba69c316d1082fe74c4b9`.
- First implementation: `59bf64827cbf29d5228967f4b3dd3c1e85802b1f`.
- Final tested implementation: `f8dabcc008416137b8b46492c072f3e7f16764d4`.
- Final instrument metadata: [metadata_first.json](evidence/final/metadata_first.json).
- Final five concurrent tracks: [summary_first.json](evidence/final/summary_first.json).
- Validation: **41/41** new agent/connection checks; **50/50** existing
  coordination checks; **50/50** existing GraphPacket and AnalysisPlan checks.
  Total **141/141**. These are Python mechanism checks, not a repeated full
  native CTest baseline. Logs and file SHA manifest are in [evidence](evidence/).
- Final instrument source/fixture hashes match committed files; working tree
  was clean when metadata was recorded. Output hashes match the track summary.
- Paid provider calls **0**; cloud provisioning calls **0**. No new budget,
  account connection, native integration or production deployment.

First failed exploratory attempts and the first successful uncommitted attempt
are retained and clearly labeled. The second startup failure is a reconstructed
summary of observed console output, not a fabricated raw log. Two failed test
logs preserve corrections to timeout expectations (kernel pipe capacity and
error label); subsequent tests pass. The earlier checkpoint results are also
retained. Temporary databases/journals were collected as outcomes and removed
by the runner; the published evidence is not an executable SQLite backup.

Implemented: interchangeable registry/planner/environment bindings, typed
multi-tool composition, durable per-action dispatch and replay, no automatic
retry of uncertain effects, local process and tested MCP stdio subset. Offline
OpenRouter compiler/parser preserves multiple tool calls and metadata; GCP
descriptor preserves selected instance without claiming connectivity.

Next gated experiments: actual OpenRouter planning with per-request durable
budget/receipt and configured secret; same workload on an owner-selected GCP
instance; real application adapters; remote recovery/cancellation; tested OS/VM
isolation and browser/GUI. [Access contract](ACCESS.md) records missing inputs.
An available Bubblewrap binary failed the isolation probe; no isolation claim.

This scoped STATE and receipt belong to this branch. Shared integration STATE,
indices, schemas, C++ and ABI are owned by the existing integrator and are not
modified by this research checkpoint. The branch is reviewable independently.
