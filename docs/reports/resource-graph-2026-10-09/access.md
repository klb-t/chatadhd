# Resource access and container composition

Base: `9e20f99a`, branch `gpt/resource-graph-2026-10-09`.

Implemented as independent byte transport and container stages in
`loom/tools/resource_graph/access.py`; defaults are data in
`loom/data/resource_graph/access.json`. No graph database, configuration
resolution engine, source write, extraction directory or persistent cache is
introduced. Main task owns source identity and cache/snapshot policy.

Public entry points:

| API | Actual execution |
|---|---|
| `Access(policy=None, transport=None, environment_limits=None)` | User settings and deployment bounds remain separate; only the trusted keyword changes deployment bounds. |
| `read_transport(locator)` | Bounded local-file or explicitly allowed HTTP(S) read; no format assumptions. |
| `authorize_local(locator)` | Root/type authorization and canonical path, without source-byte reads; caller must secure subsequent descriptor access. |
| `open_container(data, members=())` | Bounded selected ZIP member chain, including ZIP inside ZIP, with hashes and container provenance. |
| `read(locator, members=())` | Composition of byte transport and selected container chain. |
| `inspect_container(data)` / `list_members(locator, members=())` | Central-directory metadata, with no expansion of member contents. |
| `validate_archive(data, recursive=True)` | Explicitly eager native-import preflight: CRC verification, recursive ZIP inventory and aggregate expansion/count/depth limits. |

Successful byte results are `available` or `empty`; inability to retrieve is
`unavailable`, invalid archives are `corrupt`, unsupported transports/members
are `unsupported`, exceeded budgets/incomplete inventories are `partial`.
`unloaded` is the pre-read state. Failures contain a reason and no usable data;
no unavailable archive becomes an empty success.

Local versions use stable fstat metadata. HTTP versions use ETag/Last-Modified
when available, and explicitly labelled content observation otherwise. Fetch
timestamps belong to provenance, not source versions. A URL is never a source
version; neither content hash nor version is asserted to be logical identity.

HTTP is denied by default. Each requested origin and every redirect target
require explicit authorization; redirects also require a separate switch.
Built-in HTTP does not inherit environment proxies, authentication or cookies,
requests identity content encoding, and never automatically follows redirects.
Credential-bearing URLs are rejected and URL query/fragment/userinfo are
removed from public evidence. Injected transport callbacks are **trusted
infrastructure**, required to honor byte/time limits and not follow redirects;
the returned redirect/size metadata is validated too. Their exception text is
never copied into results.

ZIP reads never extract to the filesystem. Unsafe paths, symbolic-link members,
encrypted members and ambiguous duplicate member names remain visible in an
inventory but are not readable. Central-directory counts are checked before
allocating ZipInfo objects, including falsely low EOCD count declarations.
Per-member, cumulative decoded-byte, depth, compression-ratio and entry-count
limits are explicit. Exceeding a user preset or deployment limit reports which
layer limited the operation. ZIP source bytes remain unchanged.

Validation: `python -m unittest loom.tests.resource_graph.test_access -v` —
21 tests pass on Python 3.12.14, including a real loopback HTTP server, local vs
remote content equivalence, ZIP vs nested ZIP, source changes/unavailability,
stable versions for unchanged sources, corruption/CRC errors, bad member names,
symlinks, duplicate names, forged directory counts, decompression budgets,
redirect authorization and credential-free error reporting. Generated fixtures
are parser/transport tests; no user corpus or LLM quality measurement is claimed.

This byte transport performs a bounded **whole-source read** for local/HTTP ZIP
sources. ZIP inventory does not decode all payloads, and selected nested access
decodes only the ancestor chain. This is distinct from the separate JSON Lines
demand adapter, which proves bounded partial file reads before the rest of the
source is materialized. HTTP Range ZIP I/O and streamed JSON/XML are not claimed.

Reuse review: the existing `loom/tools/structure/live_pilot.py` archive guard is
research-artifact-specific (fixed required paths and replay manifests), and
`research_programme_transport.py` concerns provider requests and credential
handling. They are not suitable generic source-access public APIs. This module
reuses Python `zipfile`, `urllib.request`, `pathlib`, hashing and stat facilities;
it does not copy either specialized runner.

Official documentation checked 2026-10-09:

- [Python zipfile documentation](https://docs.python.org/3/library/zipfile.html),
  especially resource limitations, decompression pitfalls and member APIs.
- [Python urllib.request documentation](https://docs.python.org/3.13/library/urllib.request.html),
  especially handlers, redirect behavior and proxy-handler defaults.

Implemented against APIs available in project Python 3.12; no external runtime
dependency installation is required for the access layer.
