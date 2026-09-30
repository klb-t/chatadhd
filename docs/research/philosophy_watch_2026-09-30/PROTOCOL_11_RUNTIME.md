# Independent runtime invariants: protocol 11

2026-09-30. Freeze immutable runtime/adapter code copies and dependency hashes
before authored scripted responses. No credentials or network; fabricated costs
are not actual usage. Use empty native graph packets, not sealed validation.

Cases: malformed tool arguments with known provider cost; unknown cost with its
retained declared reservation; identity rejection with known cost; denied tool
execution despite declared capability; two authorized tools where the first
completes and the second raises; replay after changing initial request content
while retaining its stored hash; symbolic million-subagent preparation.

Required invariants: cost remains accounted independently of semantic success;
no tool executes without permission; the result of each completed tool survives
a later failure; replay verifies content against declared initial identity;
symbolic multiplicity does not eagerly allocate workers. Runtime request
construction cannot establish actual hosted service availability or correctness.

Preserve every first result and scripted artifact inventory. Producer changes
receive another separately named retest; no edits outside this audit directory.
