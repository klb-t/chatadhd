# D — resource graph — checkpoint 1

Base main: `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`.
Branch: `gpt/resource-graph-2026-10-09`; main and other branches untouched.
Ready SHA: this checkpoint commit (resolve `git rev-parse HEAD` at checkout).

Owner correction is authoritative: generic composable demand-driven resource access,
not an Office feature programme. No source or native-store copy is required to attach,
address or project a resource. Format implementations verify the mechanism.

Implemented first slice: local/controlled HTTP access, nested ZIP, explicit source
identity/version/content hash/selectors, configurable budgets/cache/snapshot/embedding,
JSON/YAML/XML/CSV syntax adapters, bounded projection through existing GraphPacket,
existing native store accept/reopen/replay bridge. No new graph/config/workflow engine.
Unknown/unavailable resources remain attachable and failure states remain distinct.

Focused verification: 54 tests, all passed, zero skips, including 7 actual native
store tests. Foundation log: `foundation-test.txt`. Native GCC13/WERROR/shared
library built from pinned base with vendored SQLite. This is a scoped extension
verification, not a new full repository CTest/ASan result.

Next: commit verified generic discovery/data mapping and seekable JSONL adapter;
finish real lossless conversation importer parity; CLI scenario, E packet and B hook.
No paid/model calls. Generated fixtures are synthetic, not owner exports or LLM evidence.
