## R42 — Which string literals may stay in code (owner, 2026-10-05)
Data does not live in code. The test for any literal is: **could someone want
to change it without changing the algorithm?** If yes, it is data (pack,
profile, setting or preset), never a literal. Allowed in product code (`loom/src`,
`server`, `cli`, `web/src`, `android`) are only literals whose change requires a
code change anyway:
1. **Names of the code↔data contract**: keys the code reads (`"schema"`,
   `"id"`), schema identifiers and versions (`loom.graph_packet/1`), file names
   inside the pack.
2. **Constants fixed by external standards**: HTTP methods/headers, SSE `data:`,
   JSON-RPC/MCP method names, MIME syntax, file signatures, algorithm names
   (`sha256`), grammars of standard formats (RFC 3339, JSON). This includes a
   regex that parses a standard. A regex that recognises domain content (e.g. a
   "decision" in a conversation) is data.
3. **The mechanism's own vocabulary**: lifecycle states, machine error codes,
   engine operation names. The user sees them only through translations in data.
4. **Serialization formats that are part of a contract** (date/number
   formatting required for compatibility).
5. **Minimal bootstrap needed to find the data**: data-dir env var name, data-dir
   sentinel file, pack location.
6. **Developer diagnostics**: logs and assertions, preferably as an event id
   plus parameters, with any human-readable text in data.

Tests and fixtures are not product code. Never allowed in code: user-visible
text (all UI is data, with language versions), prompts and recipes, addresses,
endpoints and model names, domain dictionaries/lists/patterns, thresholds,
weights and limits, categories, type/role names, scenarios. A missing or broken
pack yields an explicit error or the embedded pack *generated from data*, never
hand-written hidden defaults in code. Enforcement: an automated guard test scans
literals in product code and passes only these categories, through an allowlist
whose every entry carries its category and reason (owner: thread 11, building on
its data-in-code inventory). (owner's question answered by Claude and accepted
by the owner)
