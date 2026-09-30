# Structural seeding v2: local operator premises, preregistered after v1

This is a **development correction**, chosen after inspecting v1 errors; the
same development corpus is reused and no generalisation gain is established.
The immutable v1 protocol/code/policy/outputs remain in
`loom/tools/seeding/results/synthetic_lopo_v1_first/`.

V1 measured top-1 recall 7/46, precision 7/61, below the most-frequent baseline
(8/46, 8/61). Capability recall was 0/10 even though 8/10 hidden operator labels
occur in training vocabulary. Generic project-level relation overlap prioritises
operators whose *own premises* are absent from the target. This is a mapping
representation defect: common `intent`/`resource` roles do not justify a cost
gate or a repeated-chore operator.

## Hypothesis and intervention (frozen before v2 outcomes)

For capability transfer, preserve the local chain
`training decision → principle_evidence → operator application`, by requiring
its nonempty set of justifying principles to be contained in the target's visible
principle edges. No target decision content or hidden operator is supplied.
Record each preserved principle-to-operator relation in the candidate mapping.
Global project overlap ranks surviving mappings only. Roles and features retain
v1 behaviour. Frequency and random baselines are unchanged.

The input principle edges are oracle annotations and remain visible when an
operator is masked. This intervention therefore tests completion of a graph
with supplied premise labels, not extraction, causality, future prediction or
the full generator requested by R36. Missing premise evidence causes structural
abstention; another independent channel remains free to propose alternatives.

Use the same masks, no-removal controls, budgets 1/3, random seeds 0–31, separate
precision/recall denominators, LOPO partition, hashes and write-before-score
boundary. Save v2 in a new immutable run directory. No thresholds are searched.
Expected observation: better capability reconstruction and fewer control false
positives; remaining misses include donor-vocabulary absence and operators with
no recorded premise edges. Report all failures even if results improve.

Keep as an optional data policy if the mechanism tests pass and capability
precision/recall improves over v1; otherwise revert the policy addition, retaining
the measured result. Either outcome still requires fresh independent validation
before production promotion. No subsequent target-tuned search is planned.
