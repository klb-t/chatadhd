# One-entry sibling-fork tradeoff — before outcomes

After the frozen cold-to-100 trajectory, parent1 retained one packet and kept
all immediate-parent hits, while parent128 retained 101 packets. This separate
mechanism probe tests the preregistered expected tradeoff rather than claiming
one entry universally dominates. No CPU tuning or benchmark source changes.

Use the frozen chain's exact heads99 and100. Create one valid alternative final
event from head99 by changing only the empty diff's proposal_id, retaining all
source/native records and the complete common history. Prime each fresh cache
with head100. Expected: parent128 can reuse the privately retained head99;
parent1 has evicted it and must use strict fallback. Both results must equal
strict native bytes, ordinary application receipt and exact inverse; neither
chooses a semantic consensus or writes a canonical graph. Preserve both first
paths/results. No latency or model quality claim is made from this two-case probe.
