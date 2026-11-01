# Conversation bundle transport, version 1

Executable Python 3.10+ stdlib module for POSIX systems. It packages **existing
ConversationExporter output bytes**, preserving JSON, Markdown, text and HTML
verbatim. It reads no database, configuration, keys, HOME directory or provider.
Only explicit hash-addressed original blobs are read from a configured BlobStore
root (`hash[:2]/hash[2:4]/hash`); no directory walker and no arbitrary attachment
path opener exist. Tests create only synthetic data.

This is an author delta for the sole integrator. It does not register a UI format,
run a native backend, or implement a second conversation importer. Native hooks
are supplied separately in `integration.patch`, never in `product.patch`.

## Running it

From the repository root:

```sh
python3 -B -m unittest discover -s loom/tests/conversation_bundle -p 'test_*.py' -v
python3 -I -B loom/src/import/conversation_bundle/bundle.py pack /own/stage/request.json /own/stage/export.zip
python3 -I -B loom/src/import/conversation_bundle/bundle.py verify /own/stage/export.zip --staging-parent /own/stage
```

`request.json` explicitly names files already produced by ConversationExporter:

```json
{
  "exports": {"json": "conversation.export", "markdown": "readable.export"},
  "blob_root": "blobs",
  "attachments": [
    {
      "pointer": "/messages/0/attachments/0",
      "availability": "included",
      "sha256": "<64 lowercase hex of the ORIGINAL>",
      "name": "żółć.txt",
      "provenance": {"source_id": "src_existing", "unit_id": "un_existing"}
    }
  ]
}
```

Paths are explicit caller configuration; relative request paths resolve against
the request directory. `json` is mandatory. Every attachment slot in its
`messages` array must appear exactly once in the inventory, including repeated
uses of an original. Callers resolve existing attachment representations to
original hashes; the transport never guesses a hash from a display name/path,
uses OCR output as the original, or opens references from the exported JSON.
Existing JSON metadata and old device references remain unchanged as source
bytes. They are not used as restore filesystem paths.

Missing or unmanaged attachments fail the default complete export. An explicit
`--allow-incomplete` (or API `allow_incomplete=True`) permits inventory entries
with `availability: "missing"` or `"unmanaged"`, without copied bytes. A known
expected SHA may remain in such an entry; it does not certify byte inclusion.
The index then says `complete: false`; default restore rejects it. Partial
restore requires its own explicit opt-in. Omitted inventory entries always fail.

The destination must not exist. Publication is exclusive, through a same-parent
temporary and hard link, after validation; failures clean up partial output.
Repeated exports with identical input/policy produce identical bytes: stored
ZIP members, fixed timestamps, sorted inventory, no generated date or ID.

## Archive/index and provenance

The ZIP contains `conversation.json`, optional renderings in `rendered/`, one
`originals/<sha256>` per unique included original, and `bundle.index.json`.
Names in v1 are relative; original labels never become filesystem paths.
The versioned index records SHA-256, byte length and original/derived role for
each content member, plus source attachment JSON pointers and supplied existing
Source/Unit/Observation references. Original genesis is `identity` from its
`sha256:<hash>` source; renderings identify ConversationExporter and the source
conversation ID. No semantic observation, confidence, graph ID or source URI
is invented. Supplied graph references are historical references, not assertions
that those rows exist on the restore target. The existing importer remains the
owner of Source/Unit/Observation/genesis materialization and target IDs.

Pack returns an archive SHA/size and index SHA receipt. The index does not hash
itself. Its entries detect accidental loss and inconsistent tampering. **Internal
hashes do not authenticate an archive whose index was also replaced.** Preserve
the receipt separately and use `--expected-sha256` / API `expected_sha256` to bind
restore to trusted exported bytes. The tests explicitly demonstrate this boundary.

`preset.json` holds admission presets and format/member mappings. Library callers
pass `Limits` and `formats` explicitly; CLI `--policy /trusted/preset.json` overrides
them. Any admission limit may be raised or disabled with JSON `null`; positive
`chunk_bytes` remains an algorithm buffer parameter. Conventional single-disk
ZIP, stored/deflated members, no ZIP64/encryption/comments and regular files only
are v1 format capabilities. ZIP64/other encodings are rejected as unsupported,
not silently truncated. The fleet's capacity/output gates are execution policy
for the author, not hardcoded product limits.

## Safe staging and the existing ZIP path

`stage_bundle` snapshots an input regular file into a fresh private staging
directory, optionally pins its SHA, validates the entire index/ZIP, then restores
verified member bytes there. It never extracts through an input member path until
the full allowlist, sizes, hashes, completeness and pointer inventory pass.
It rejects traversal, Windows path separators/drives, duplicate names including
case aliases, symlinks, directories, extra/missing members, CRC/hash mismatch and
unsupported layouts. It bounds archive size, actual central-directory entry count
**before ZipFile allocation**, individual/total expanded bytes, manifest size and
compression ratio. Blob path components use `openat`/`O_NOFOLLOW`; all input handles
must be regular files. No extraction into an existing user directory occurs.

`restore_bundle(path, import_zip_callback, ...)` calls that callback exactly once
with a `RestorePlan`, after validation/extraction. The plan has the private ZIP,
staged bytes, checked manifest and `interpret_members=("conversation.json",)`.
Both disappear when the callback exits or throws. Errors are propagated; there
is no retry or invented database rollback. The callback must use the existing
ZIP importer; the transport contains no conversation persistence code.

`restore_verified_bundle(path, import_zip_callback, inspect_target, ...)` additionally
checks the actual target after import. `inspect_target(result)` supplies a
`RestoredTarget`: the target BlobStore root, an explicit source-message-pointer
to target ID map, a persisted-row attachment reader and the managed reference
hash resolver. It streams and rehashes every restored original, checks sizes and
each persisted attachment slot, rejects missing/aliased mappings and reports
verified/total denominators. Partial restores need explicit opt-in and preserve
`None` slots for unavailable attachments; they remain `complete: false`.
The target inspector must read the target DB/registry, not echo the input index.
The supplied mapping must come from native import receipts, not guessed order.
Run this on an isolated target or inside the caller's transaction; a verification
failure propagates and never claims rollback or successful full restore.

The CLI `verify` performs the same isolated staging and prints an admission
receipt including `native_options`. It cleans its private staging on exit, so
the caller must bind the subsequently imported input to the returned SHA/size.
Prefer verifying an outer private copy owned by the native caller. A successful
CLI receipt is not itself a native import. Package an absolute trusted script
and interpreter, use isolated mode and a clean environment, and validate the
helper's status/JSON before invoking the native importer. No helper discovery
through uploaded filenames, PATH, HOME or provider configuration is needed.

## Exact integration points and remaining native acceptance

1. Export: call existing `ConversationExporter::export_conversation` for JSON and
   any requested renderings, with the same conversation/include_all scope and a
   consistent snapshot. Resolve **all** snapshot attachment slots through the
   managed original/blob registry. Pass those exact bytes and inventory to
   `create_bundle` / CLI `pack`. Explicit missing/unmanaged status must reach the
   user. Include only those originals, never a database or a data-root backup.
2. Restore admission: validate before calling any DB-writing native import. For
   a process bridge, consume `verify`'s receipt and set the typed ImportOptions:
   `export_mode=Off`, `record_provenance=true`, `expected_source_hash`,
   `expected_source_bytes`, `zip_interpret_members={"conversation.json"}`. Use
   native `preflight` for the gate if the existing caller needs it. This native
   hash/size guard is essential when an input pathname can change after verify.
3. ZIP materialization: `integration.patch` adds an optional **exact member
   interpretation list** to the existing ZIP path. Every original and rendering
   still uses `materialize_zip_member` and opaque retention; only the snapshot
   enters existing conversation decoding. It also makes selection part of
   preset/projection cache identity, preserving historical identity when unset.
   Without this hook an original JSON/text/ZIP or a second rendering could be
   misread as another conversation. Never pass the complete bundle to unmodified
   auto/provider interpretation and call it a successful bundle restore.
4. Native binding acceptance: the legacy JSON importer normalizes/drops messages
   and currently does not restore attachment arrays/status/IDs losslessly. The
   bridge must use source-message pointers/receipts to map restored message IDs,
   then bind included originals via verified BlobStore hashes, recording genesis
   to the bundle member and supplied historical references. Do not assume output
   ordinal equals source ordinal when blank/system/other skipped rows exist.
   Missing/unmanaged slots remain explicit gaps. This bridge/atomic binding work
   is **not claimed complete by the standalone transport**; do not expose full
   conversation restore as accepted before that native gate passes.
5. Apply `product.patch`, inspect `integration.patch` against its five explicit
   manifest entries, and use the sole integrator's authorized runner. The native
   wrapper adds `tests/test_conversation_bundle_handoff.cpp` to the existing test
   glob, without editing CMake. Run the suite `conversation_bundle_handoff` and
   relevant import/preset identity sentinels. Its fixture exercises actual ZIP
   materialization, original retention, no extra attachment-conversations,
   cache separation and invalid option selection; it does **not** test the
   native full binding adapter in point 4. Then test the synthetic 2-message,
   shared-original/Polish case through the native/API bridge, reopen an isolated
   target and verify blob hashes and attachment mapping. Invalid/tampered input
   must be refused before calling native import. HTTP is needed only if wired
   into that caller. No full guest matrix or production/private-data gate.

No shared files are changed in this author worktree. `integration.patch` remains
unapplied and native compilation/HTTP remain unrun. Receipt, checkpoint, logs and
patch manifests are handoff artifacts outside the product patch. The sole Chat
integrator checks stable manifest + apply-check, completes the scoped native
binding gate, and accepts a next base; the author starts no further loop.
