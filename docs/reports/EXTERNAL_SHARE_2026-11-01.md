# External conversation share adapter — executor handoff (historical)

Date: 2026-11-01. Base: `97a5562bed5294602161eb0e7d2f2c4bc73e2a60`.
Worktree: `chatadhd-external-share-97a5562-20261101`, UID/EUID 993.
Integrator: `01a192f5-2814-7e52-bf0c-32f3428151ed`.

Admission confirmed the requested gap: original `parse_content` accepts only
`conversation` + `messages` in ChatADHD JSON. An export archive's
`shared_conversations.json` is not a public-share HTML adapter. No scope switch
to another P0 was necessary. Philosophy §0, revision 2, was read from
`/var/lib/codex-team/context/philosophy-section-0.md`.

## Delta

- Existing URL worker parses two evidenced ChatGPT share HTML structures:
  Next.js `__NEXT_DATA__` and React Router turbo-stream v2 JSON data. Recognition
  requires a fetched HTTPS URL at `chatgpt.com/share/<id>` or legacy
  `chat.openai.com/share/<id>` plus the expected embedded structure and MIME.
  It executes no HTML/JS, queries no provider API, and does not use a model.
- Exact roles, Unicode, whitespace, empty message text, per-branch order and
  source title survive. Unsupported content yields `partial`; malformed or
  ambiguous trees yield no conversation and an explicit unsupported result.
  A title or visible page text never becomes a conversation in share mode.
- Mapping parents are projected into native `parent_id`; off-path messages use
  the existing `version` status. `current_node`, or the explicitly supplied
  linear path when no current node exists, selects the active branch. There is
  no inferred newest-leaf fallback. All supplied message branches remain stored.
  Structural null-message nodes remain in the immutable original and branch
  metadata; nearest message parents connect through them.
- Attachments and provider asset pointers remain original references in message
  metadata and the result, including exact locators. They are not downloaded.
  Text links remain unmodified. Neither private links nor files are opened.
- Native persistence retains HTML bytes and hash in the existing Blob/Source
  stores. Provenance scopes decoded JSON pointers with script index/indices
  and format (`embedded_data`), instead of claiming a JSON pointer into HTML.
  Existing snapshot-based identity and transaction idempotency are reused.
- UI labels identify ChatGPT share, active supplied branch, reference-only
  attachments, partial results and the existing conversation file-import path.
  Labels live in PL/EN data. HTTP/UI import transport and public ABI are unchanged.

No second HTTP stack: PublicTransport, Fetcher, URL normalization, public-address
checks, redirects/TLS downgrade, proxy refusal, original limits and credentials
boundary remain in place. Worker embedding regenerated with its existing tool.
Expansion uses the existing configurable worker output budget; cyclic or invalid
pool references cannot fabricate data. No schema migration or ABI addition.

The detailed public evidence and honest synthetic-fixture provenance are in
[`PROVENANCE.md`](../../loom/tests/fixtures/url_share/PROVENANCE.md). Official
sharing documentation establishes semantics; it does not promise this private
HTML wire format remains stable.

## Verification and commands

Final results and exact commands are recorded in root `CHECKPOINT.md` and the
adjacent `external-share-2026-11-01/` logs. Parser tests run on fictional data,
using an explicit in-memory test transport. Existing M2 tests use only their
own synthetic loopback fixture. No paid API, model, credentials, owner archive,
holdout, production data, installation, commit, push, deploy or added agent.

Two native test cases were added (not executed): provider tree/reference/source
provenance + duplicate import + database reopen, and unresolved/duplicate parent
rollback. The native suite now contains 10 cases. Syntax checks on implementation
and tests passed independently of linking. The web dependency tree and `tsc`
are absent; no install, TypeScript build or browser result is claimed.

## Limits and receiving queue

The original executor handoff was not integrated. The sole integrator subsequently
merged the source in a light phase; [integration status](EXTERNAL_SHARE_INTEGRATION_2026-11-01.md).
**Native/runtime/browser acceptance and deployment remain unperformed.** No public
provider conversation was fetched. Parser/artifact evidence is not native
store/reopen, HTTP, real request-context, UI or live TLS evidence. Native tests
and UI verification are the integrator's next dependent gate on the accepted
base, after conflict checking with administrative backup work.

The adapter supports the evidenced JSON subset; deferred non-JSON stream frames,
future formats and unsupported content stay unsupported/partial. Share URLs
requiring login or returning 401/403 retain explicit refusal and file fallback;
no attempt is made to bypass access restrictions. Claude share remains open.

Next receiving step: audit/apply this delta once; run `url_share_import` native
suite, then verify the existing import HTTP/UI flow, actual selected-branch
context with a synthetic provider, and reopen. The independent next adapter
candidate is Claude share, contingent on public format/access evidence; it is
not implemented or claimed in this increment. No independent follow-on was
started before receiving this bounded scope.

Reversal: restore this delta's files from the pinned base and remove its new
fixtures/tests/docs. No data migration, installation or deployment to reverse.
