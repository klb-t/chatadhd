# Local basic video adapter

`video_basic.py` is an executable stdlib Python adapter for Linux with installed
ffmpeg/ffprobe. It produces metadata and a bounded prefix sample of PNGs. It
does not run OCR, models, providers, network requests, or storage/graph writes.

From the repository root, with at least 1 GiB available on `/` and the output
filesystem:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 loom/tools/video_basic/video_basic.py \
  loom/tests/video_basic/synthetic.mkv /tmp/my-new-video-bundle
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s loom/tests/video_basic -v
```

The destination must not exist. CLI success returns the original SHA-256, frame
count and SHA-256 of `receipt.json`. Failure returns a stable error code and
exit status 2; codec diagnostics and media metadata are not printed. Python
callers use `extract(path, destination, expected_sha256=hash, overrides={...})`.

All numeric limits and the demuxer catalogue are presets in `defaults.json`.
`--settings settings.json` accepts an object of overrides. There is no invented
product maximum on the owner's requested settings. A caller is responsible for
admitting a resource increase before execution. The default input cap is 8 MiB,
output cap 8 MiB, 4 frames, every second decoded frame, at most 1920x1080 and
2073600 pixels, 10 seconds total wall time, 8 seconds CPU per subprocess,
768 MiB address space, 64 MiB single allocation, and one decoder/encoder/filter
thread. Metadata/log pipes are independently capped. The default demuxers are
Matroska/WebM, MOV/MP4, AVI and MPEG-TS. Unsupported media fail explicitly.

The input must be a regular local file, with no URI or final symlink. It is
copied while hashing into a private bounded snapshot. A source that changes
during capture is rejected. Both tools receive that same snapshot, never the
original path. Input bytes are left untouched. A caller-supplied original hash
is checked before launching tools. The snapshot is ephemeral; the caller must
keep the original in the existing immutable BlobStore before native integration.

Every subprocess uses argv with `shell=False`, no stdin, a scrubbed environment
and POSIX CPU/address-space/file-size limits through `limited_exec.py`. The
protocol whitelist is always `file`, including when a caller enables another
demuxer. There is no URL fallback. The tools must be trusted local executables;
the executable overrides in the Python API support deterministic test doubles.
A wall deadline covers capture, probe and extraction. Timeouts/errors kill and
reap the subprocess group and remove staging. `RLIMIT_FSIZE` assigns each PNG a
share of the output budget, reserving space for the receipt, so output is bounded
during decoding. PNG dimensions are checked again after decoding. The adapter
never scales/autorotates pixels silently; rotation metadata remains in the probe.

Publication uses Linux `renameat2(RENAME_NOREPLACE)`: an existing destination,
including a concurrently created empty directory, is preserved. Unsupported
platform/filesystem publication fails closed. There is no resume/append cache
or implicit replacement of an earlier extraction.

## Coordinates and evidence

`receipt.json` (`loom.video_basic/1`) contains the original hash/size, selected
ffprobe metadata, actual tool versions, effective limits/method, sampling rule
and per-frame file/hash/size/dimensions. Each frame carries original hash, stream
index, zero-based decoded frame ordinal, integer PTS and rational time base.
`timestamp.seconds` is the exact rational PTS conversion `[numerator, denominator]`.
The tool copies timestamps with `-copyts`; it does not derive them from nominal
FPS or rebase them to zero. The fixture deliberately starts at two seconds.

Sampling selects decoded ordinals `0, step, 2*step, ...` and stops after the
requested count or EOF. This is a prefix sample. The receipt explicitly makes
no whole-video coverage claim. Decoder read-ahead may inspect a few additional
frames; CPU/wall/input/memory limits still apply. Timestamp absence, changing
dimensions/time base, and decreasing selected PTS fail explicitly. Equal PTS
are allowed; frame ordinals distinguish those occurrences.

This is a deterministic derived representation, with no semantic interpretation,
confidence estimate, transcript or frame-duration claim. Same input, settings
and local tool build produce identical PNGs and receipt bytes. PNG/container
bytes across different tool builds are not promised to match.

## Existing image and native Source/Unit/Observation handoff

The Python `image_inputs` function is the executable image handoff:

```python
result = extract(original_blob_path, new_bundle, expected_sha256=original_hash)
images = image_inputs(new_bundle, original_sha256=original_hash,
                      receipt_sha256=result["receipt_sha256"])
# All PNGs and the receipt are validated before any image is returned.
for image in images:
    existing_image_sink(image.bytes, image.mime, image.name, image.note)
```

It returns immutable byte snapshots with the carrier fields of native
`loom::chat::PreparedImage`: bytes, MIME, name, note, `has_ocr=false`. The helper
does not call the sink. Hashes are caller-pinned; frame names, dimensions, sizes,
stream and temporal provenance are validated, with no traversal or final
symlink. `note` preserves the raw-video temporal locator separately from receipt
JSON locators. The Unit locator points to `/frames/N` in the immutable receipt;
the field Observation quotes its exact SHA-256 string at `/frames/N/sha256`.
Two frames with equal PTS therefore remain separate Units. No text observation
is asserted to be a byte span or OCR text from the original video/PNG.

Only the designated integrator wires these existing native points:

1. `BlobStore`/`ProvenanceStore` (`loom/include/loom/provenance.h`): verify and
   retain the original video Source first. Store the receipt JSON and each PNG
   as **derived** Sources, with original Source/hash, method and frame provenance
   in metadata. Do not change the original's kind or reuse its byte coordinates.
2. `model::Unit`/`model::Observation` (`loom/include/loom/model.h`): replace
   `sha256:` references in the handoff with the corresponding registered Source
   IDs. Use the native `make_id` builders. Keep original temporal locator,
   integer PTS/time base, stream and decoded ordinal in attrs; quote only the
   receipt field as `ObservationKind::Field`, evidence `derived`. Attach the
   normal provenance/genesis path to these derived metadata observations, not
   to an invented video transcript. Repeated equal extraction must reuse the
   same immutable receipt/PNG blobs and occurrence identity.
3. `loom/include/loom/image_preparation.h`: copy validated snapshots into the
   existing image carrier. Image serialization/selection must remain behind
   existing admission/egress gates. This author delta makes no model calls.
   `prepare_image_attachment` and `screenshot_body` can run OCR; do not invoke
   them for this handoff. `store_image_representation` expects OCR output and
   is not a raw-frame writer. `persist_image_attachment` currently labels an
   image as an original screenshot; its existing behavior must be accounted
   for by the integrator so a derived PNG retains its relationship to video.

No registry, source_parts, unified_context, pack, CMake, ABI or UI hook is changed
by this module. There is no shared-file integration patch to apply. One native
integration must supply registration and shared storage wiring; this is not a
claim that video import is already reachable through the UI/HTTP.

Integrator acceptance: validate the stable allowlist/manifest and patch hashes;
`git apply --check` on the next accepted tree; run this focused suite; then run
scoped native/HTTP via the authorized runner only if those hooks are added.
Check native original/derived Source kinds, Unit/Observation locators, genesis
and idempotent reuse, no OCR dispatch, unchanged original bytes, and corrupted
receipt/frame rejection before graph mutations. Accept into the next base as
one integration; the author does not start another work loop.

## Fixture

`loom/tests/video_basic/synthetic.mkv` is a 64x48 FFV1/Matroska clip containing
7 `testsrc` frames at 4 FPS with first PTS 2 seconds. It has no private media.
To regenerate into a **new** path with local tools:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 loom/tests/video_basic/make_fixture.py /tmp/new-synthetic.mkv
```

The focused suite also generates a tiny variable-rate clip and uses local fake
executables to exercise timeouts, pipe caps and OS resource limits. All generated
test outputs live in private temporary directories and are removed.
