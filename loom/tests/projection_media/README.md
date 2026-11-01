# Projection/media admission (synthetic only)

`verify_native.py STAGE ROOT FIXTURES EVIDENCE MODE` loads only `STAGE/lib/libloom.so`.
The helper/process path is the installed runtime, not Python reader imports from this checkout.
Use a filesystem sandbox, an environment whitelist, a private TMPDIR, and new synthetic roots.
Do not point this harness at operator data: it intentionally corrupts/restores its own blobs and
uses temporary SQLite failure triggers. No credentials, provider or real image model are needed.

The finite phases are `old-create`, `candidate`, `old-postcopy`, `old-restored`.
Stop each process before copying. Use `deploy/cold_snapshot.py` for the pre-upgrade snapshot,
a separate candidate root, a separate post-upgrade copy for the old runtime, and a new root
restored from the pre-upgrade snapshot. Never reopen the original source fixture for writing.
The retained original PDF is `fixtures/document.pdf`; the XLS factory is
`loom/tests/unknown_structure/fixture.py`, video is `loom/tests/video_basic/synthetic.mkv`,
and the shared transport sentinel is `loom/tests/fixtures/docx_parts/base.docx`.
Conversation JSON contains two synthetic user/assistant messages only.

`candidate-resume` and `candidate-bundle` are explicit continuations of the recorded interrupted
acceptance run, not generic retry modes: inspect progress, database and completed phases first.
The latter uses a distinct `conversation-reuse.json` fixture after the bundle-reuse fix.
`final-sentinels` checks empty-projection status and explicit incomplete bundle admission in a
separate root. `bundle-parent-check` checks the already recorded bundle/video bindings without
rerunning the earlier matrix. Historical phase logs retain their own library hashes.
`test_server_projection_media.py` runs one combined HTTP/selected-context/restart case with the
existing local recording provider. This is not browser or real vision acceptance.

## Native API contract

All operations use the existing authorized `client.blob_text` action / HTTP blob-text route.

* `projection_check`: `projection_node`, optional `selected_nodes`; verifies retained snapshot
  integrity and current replay. Empty projections report `not_exercised`, not compatibility.
* `projection_reproject`: additionally `expected_receipt_sha256` returned by the check. The host
  reparses retained original bytes with the current pinned installation/default recipe. It keeps
  old Units/receipts, writes a separate upgrade record and coordinate mapping proposals. It does
  **not** alter the account-global selector. Caller must explicitly read/select new IDs. Changed
  ranges/kinds require a decision; no old IDs are relabeled. Old custom settings are not a command
  to execute an obsolete tool. ZIP tree migration / `previous_source_id` is explicitly unsupported.
* `format: xls`: installed catdoc, packaged charset tables and generated private configuration;
  only PARTIAL output-matrix coordinates. No asserted sheet names/formulas/BIFF addresses.
* `format: video`: installed bounded ffprobe/ffmpeg, immutable original + receipt + derived PNG
  Sources. Frame ordinal and exact rational PTS remain distinct; `has_ocr=false`. Read via existing
  `client.blob_text_read`. No automatic screenshot routing, OCR or visual interpretation.
* `bundle_export`: `conversation_id`, optional `allow_incomplete`. Returns managed ZIP hash.
* `bundle_import`: `hash`, `expected_archive_sha256`, optional `allow_incomplete`. Only
  `conversation.json` is interpreted; assets/other JSON remain opaque. Source JSON pointers map to
  actual target message IDs before normalization. Unsupported/ambiguous normalization refuses.
  Missing assets default to refusal; explicit partial admission returns `INCOMPLETE`. Hash pins
  prove integrity against that caller-supplied hash, not authenticity of an arbitrary archive.

SQL rollback does not remove orphan content-addressed blob bytes. Bundle assets verified is not
lossless database restore: status/version groups and full source metadata remain in the archive,
not recreated as an entire database. Runtime profile/tool changes still refuse stale replay.
A cold pre-upgrade restore loses all changes after that copy (RPO); it is not lossless in-place
rollback. Neither API manages services, packages, operator backups or production migrations.

The trusted helper uses a private workspace and a bounded process group. catdoc configuration,
charset files, interpreter, scripts and tool executables are pinned. Codec/converter subprocesses
have filesystem Landlock admission and fixed argv; this is not a kernel network sandbox or proof
that every system shared library/OS component is immutable. Watchdog bounds are safety limits,
not the owner's engineering-time guidance. New GUI/extent consumer and real vision are outside
this backend admission. Existing installer remains the package mechanism.
