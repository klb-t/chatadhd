# Measured capability matrix

Pinned main: `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`.
Source fixtures are generated and nonprivate. No LLM-quality claim is made.

| Capability | Actual execution | Boundary |
|---|---|---|
| Attach unknown/unavailable source | Zero-read descriptor + valid GraphPacket | No parser required at attach |
| Local / controlled HTTP(S) | Source reads; real loopback HTTP regression | Allowlisted origin, no ambient credentials; HTTPS uses standard verified TLS, no live external fetch test |
| ZIP / ZIP in ZIP | Same member tree as direct/inline source | Bounded whole outer byte fetch; selected member expansion |
| JSON / YAML / XML / CSV | Existing parsers, unknown fields retained | Syntax only; lexical losses explicit, source bytes authoritative |
| Reference / inline / snapshot / cache / index | Independent combinations and changes tested | Parsed TTL cache may intentionally serve captured revision; no persistent autonomous watcher |
| Demand / eager structure | Same field values; selected-only packet materialization | Standard syntax parser reads a complete bounded document |
| Truly partial source read | JSONL: 33,184,000 byte source, 8,192 bytes read for 2 rows | Local POSIX; row-bounded parse; distant rows need bounded scan or offsets |
| Unknown structure adaptation | Automatically generated schema/sample map + registration + packet | No fixture-specific branch; structural validation, domain semantics unknown |
| Online documentation | Injected controlled fetch, dated provenance, schema used as data | Explicit URL/schema; no autonomous web search or model call |
| Executable adapter lifecycle | Trusted digest test, timeout/resources, separate activation | Not a hostile-code sandbox; unknown downloaded code rejected |
| Conversation parity | Actual existing native OpenAI/Anthropic import; local/remote/nested bytes | Optional bridge uses temporary native materialization; pure public seam offered separately |
| Profile → field → setting → consumer | Existing Config.get, same source hash, graph edges | Demonstrated read; no automatic native runtime activation |
| Packet/schema roundtrip | Existing codec | Distinct from native execution |
| Native store | Actual accept → shutdown → reopen → replay + row-drift checks | Explicit opt-in; no new database/schema |
| E handoff | Safe generated packet through existing contract | No dependency on renderer/E for tests |
| Source write-back | Capability reports false, overlay is proposal | Read does not establish lossless write |

Security checks cover ZIP traversal/ambiguous paths, symlinks/duplicates, expansion
budgets, recursive eager preflight, XML DTD/external entities, unauthorized redirects,
ambient credential exclusion, safe diagnostics and local root confinement. The extension
uses an authorized local filesystem; it is not an OS sandbox against hostile local users.
