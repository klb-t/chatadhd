# AGEDS connector (read, off by default)

ChatADHD can ask the AGEDS Evidence Workbench questions about a case and show
the answers as **anchored quotes** — exact stored text with its artifact id,
transcript version id, selector and `quote_sha256` — instead of model prose.
This is bridge **M1 AGEDS → ChatADHD** from
[ECOSYSTEM_INTEGRATION_2026-10-11](architecture/ECOSYSTEM_INTEGRATION_2026-10-11.md)
in its smallest useful form ("Function 1.5": the model parses the question into
a typed intent; AGEDS executes it deterministically; nothing is synthesized).

Code: `engine/ageds_adapter.py` (stub client), tests
`engine/tests/test_ageds_adapter.py` (fake transport, no network:
`python3 -m unittest engine.tests.test_ageds_adapter`). Server contract:
AGEDS `docs/READ_API.md` and `docs/ECOSYSTEM_ADAPTERS.md` (in the AGEDS repo).

## What it does and does not do

| Does | Does not |
|---|---|
| Sends a **typed intent** (`search`, `timeline_window`, `artifact`, `citation`, `quote`) with a bearer token to `/api/read/v1/intents/execute` | Send the conversation, memory tree or graph to AGEDS |
| Returns anchored items verbatim; `render_quotes` prints them with their anchors | Accept a result whose `mode` is not `anchored` or whose `prose` is not `null` (`AgedsContractError`) |
| Fetches citation packets and verifies a quote locally (`verify_quote`: hash, selector, version) | Download media or source bytes (the read API never serves them) |
| Asks AGEDS to **label** an interpretive answer (`validate_interpretive`): claims without known item ids come back `unsupported` | Call any model itself; the model lives in the chat engine, and interpretive prose is only ever shown with that label |
| Submits an **annotation proposal** (`propose_annotation`, needs the `propose` scope); AGEDS stores it as `proposed` for a human | Write annotations, links or anything else into AGEDS directly |
| Reads settings with `config.get(key, preset)` and the token from `secrets.get("ageds_read_token")` | Add keys to `engine.config.DEFAULTS` (compat invariant: `config.json` layout stays unchanged) |

Hash agreement is byte identity of a stored transcript selection; it is not
spoken truth, acoustic alignment or authorship.

## Enabling it (user settings; nothing is contacted before)

1. On the AGEDS host create a token:
   `python -m server.app.cli token-create --label "ChatADHD" --scope read --client-id chatadhd`
   (add `--scope propose` to allow annotation proposals). The value is printed once.
2. In ChatADHD's data directory set, in `config.json`:
   `"ageds_enabled": true`, `"ageds_endpoint": "http://<ageds-host>:8000"`
   (presets: `ageds_timeout_s` 30, `ageds_client_id` "chatadhd", `ageds_search_limit` 50 — all user values, not limits),
   and in `secrets.json`: `"ageds_read_token": "ageds_<key>.<secret>"`.
3. `engine.ageds_adapter.from_settings(config, secrets)` returns a client; with
   the connector off or incomplete it raises `AgedsDisabled` without any request.

The connector descriptor (`connector_descriptor(config, secrets)`) is shaped
like an entry of `loom/data/runtime/tool_sources.pack`: `id`, `kind`,
`transport`, `url`, `access: {secret: <vault key name>}`, `effects`
(`reads_only`, `sends_off_device: true` to the AGEDS host, `touches_private_data`),
`tools`, and the graph address `connector:ageds`. It never contains the token.

## Error model

`AgedsDisabled` (off/incomplete), `AgedsAuthError` (401/403 with the server
`code`: `missing_token`, `unknown_token`, `revoked_token`, `expired_token`,
`insufficient_scope`), `AgedsHttpError` (other statuses with `code`, e.g.
`work_limit`, `not_found`, `invalid_intent`), `AgedsContractError` (response
outside the contract). Messages are redacted: the token never appears in
exceptions, logs or the UI.

## Why `engine/` and not Loom (decision, reversible)

Loom is the kernel and models external tools as data (`tool_sources.pack`: an
MCP server per source, tools become catalogue actions confirmed by the user).
A C++ `ToolSource` kind for AGEDS would need a new adapter in `loom/src` and a
graph node per the M1 spec; that is outside this increment's scope and would
duplicate a protocol that the MCP kind already covers. The chosen path:

- the Python stub in `engine/` (the Python app still runs in parallel, DUCH §6)
  carries the contract and the tests now;
- the descriptor already uses the tool-source vocabulary, so the next step on
  the Loom side is an **MCP shim over the AGEDS read API** (listed in AGEDS
  `ECOSYSTEM_ADAPTERS.md`, proposal 11): no C++ change, every call confirmed by
  the user through the existing `confirm:tool_source.unknown` rule.

Reverse: delete `engine/ageds_adapter.py` and this document; no setting, pack,
schema or ABI was changed.
