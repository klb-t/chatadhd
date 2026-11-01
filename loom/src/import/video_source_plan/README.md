# Native video source plan (in memory)

`make_source_plan` accepts borrowed host-owned original bytes, an ordered vector
of host-owned PNG buffers with display names, exact helper receipt bytes and a
separately pinned receipt SHA-256. It returns an owning `Result<SourcePlan>`.
There are no paths, file readers, subprocesses, database handles, provider calls
or mutation callbacks in this API. An error returns no partial plan.

This module supplements the already received seven-file `video_basic` helper.
It does not replace or modify that module or repeat its decoder/CLI tests.
The caller must pin the receipt from its own trusted helper invocation; a digest
supplied alongside arbitrary untrusted JSON is not an authenticity guarantee.
The plan verifies all declared bindings against the provided bytes; it does not
decode the original again to prove that the PNG pixels came from that video.

## Calling contract

The host loads trusted `policy.json` through its existing configuration boundary,
captures the helper result and supplies byte buffers without resolving any paths
from the receipt:

```cpp
std::vector<loom::video::FrameBytes> frames = {
    {first_png_bytes, "Klatka żółta.png"},
    {second_png_bytes, "Klatka zielona.png"},
};
loom::video::SourcePlanInput input{
    original_bytes, "Film zażółć.mkv", "video/x-matroska",
    frames, captured_receipt_bytes, separately_pinned_receipt_sha256,
};
auto planned = loom::video::make_source_plan(input, trusted_policy);
if (!planned) return planned.error();
// Only the designated host integration may commit this complete owning plan.
```

Buffers must stay alive and unchanged for the duration of the call. After return,
the plan owns the exact original, receipt and PNG snapshots and image carriers.
Names are validated host display labels; source URIs are content-addressed
`sha256:` references. The receipt's `file` value must match the helper's ordinal
tag, but it is never opened, converted to a source URI or used as an image name.
Unknown root/frame fields, unsafe ordinal tags and path-shaped host names fail.

`policy.json` contains admission limits, compatible method ID/version catalogue,
kind labels and ID prefixes. The module has no file I/O or hidden defaults.
The preset is 8 MiB original, 256 KiB receipt, 8 MiB aggregate PNGs, four frames,
1920x1080/2073600 pixels and decoded ordinal at most 1000000. These are data,
not new product ceilings. Actual count must also fit the existing native
`Observation::ordinal` integer contract. Receipt limits can reduce admission;
they cannot raise trusted host limits. Changing trusted policy changes derived
plan identities; raw-video identity remains tied to the original bytes.

The following structural checks precede creation of any `SourcePlan`:

- Exact receipt pin, byte budgets, original SHA-256/size and frame count.
- Bounded JSON depth, duplicate-key refusal, required schema and compatible
  method; `derived` evidence, RGB24, no autorotation, prefix sampling without
  a whole-video coverage claim. An unknown method/version fails by default.
- Canonical ordinal tags, dense selected ordinals, exact decoded ordinal
  `selected * step` with overflow checks, original hash and stream binding.
- Frame SHA-256/size and aggregate bytes, PNG signature/chunk bounds/CRCs,
  8-bit RGB noninterlaced IHDR dimensions, matching declared stream/frame
  dimensions, host and receipt dimension/pixel admission, and terminal IEND.
- Integer PTS and positive time base, agreement with the selected stream time
  base, constant frame time base, exact reduced rational seconds (checked
  multiplication), and nondecreasing PTS. Equal PTS are valid. Available
  `start_pts` and `duration_ts` constrain the actual stream interval. No duration
  or total decoded frame count is invented when the receipt does not supply it.

PNG container validation does not inflate IDAT or establish visual semantics.
The existing image decoder/admission boundary must still validate pixels before
using them. A metadata Observation quotes a hash in the captured receipt, never
pixel text, OCR output or an inferred scene description.

## Existing native contracts in the result

`original` is the verified raw-video `SourceRecord` plus its exact bytes.
`receipt` is a distinct derived JSON `SourceRecord` plus the original captured
bytes (not a reserialization). Whole-source Units carry exact byte extents.
Every frame contains:

- A derived PNG `SourceRecord`/byte snapshot. Blob identity is its PNG hash;
  occurrence source identity includes receipt+policy and decoded ordinal.
- A `model::Unit` addressed by `/frames/N` in the derived receipt, with its
  native `Unit::make_id` identity. Original temporal coordinates, stream,
  decoded ordinal, rational PTS, hashes and method are separate attributes.
- A native Field `Observation` quoting exactly `/frames/N/sha256`, with
  `Observation::make_id`. No original-video or PNG byte coordinates are
  assigned to this text. Two frames with identical PTS or even identical PNG
  bytes remain separate occurrence Units.
- Existing `ProvenanceRecord`s for the derived PNG from the raw-video temporal
  locator, the Unit in receipt JSON, and its exact field Observation. The
  receipt also has raw-video provenance. A provenance confidence of 1 means
  exact checked byte/field binding; it makes no OCR/semantic accuracy claim.
- A `genesis::VersionText`/`SourceRef` for that metadata Observation. Its group
  is the occurrence Unit and ordinal is 1, with no invented date/person. Each
  is an independent one-version metadata input. Do not concatenate sampled
  frames as consecutive revisions of one text or invent replacement events.
- An existing `chat::PreparedImage` containing the verified PNG snapshot,
  host display name, PNG MIME and bound provenance note. `has_ocr=false`, and
  parts/preset remain empty for the host's normal admission/rendering. There
  is no `screenshot_body`, `prepare_image_attachment` or fabricated OCR result.

Method metadata includes the supported captured helper method, receipt hash,
trusted policy hash and derived identity hash. IDs use native builders and the
existing SHA/JSON utilities. No model/ABI/pack/CMake contract is extended.

## Focused native harness

From repository root, with global/target free space >=1 GiB:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 loom/tests/video_source_plan/run_focused.py
```

The harness compiles serially the new module/test TU and the actual small pure
SHA/JSON/UTF-8/ID implementation dependencies. It uses the repository's existing
nlohmann/doctest headers, section garbage collection and no foreign archive,
runtime/database implementation, CMake or core-library build. Native identity
methods are real, not test stubs. Compilation/test output stays in an ephemeral
directory, capped at 8 MiB with a global free-space guard, CPU/address-space/file
resource bounds and per-command timeouts. No helper tests or ffmpeg extraction
run here. The synthetic native fixture is one opaque original buffer and two
stdlib/zlib-generated 1x1 RGB PNGs with equal PTS and different decoded ordinals;
it is a mechanism fixture, not evidence of original-video decoding.

## Sole integration next

`fleet-chat-wave-tail-integrate-20261101`, UUID
`01a192f5-2814-7e52-bf0c-32f3428151ed`, receives this delta together with the
unchanged seven prerequisite helper files after the Python work. It must verify
the stable allowlist, prerequisite hashes and `git apply --check` against the
next base, then wire one existing safe host boundary for BlobStore/Provenance
and transaction handling. Use the exact returned bytes for the existing store;
recheck returned blob hashes before Source/provenance/graph mutations. Retain
the original and all derived bytes. Reuse host policy/admission and image
serialization; derived PNG Sources must not become original screenshots.

Only that integration may add native/HTTP wiring and run its scoped native/HTTP
checks through the authorized runner. This author delta claims no DB write,
DB idempotence, graph/UI/HTTP reachability, egress admission or deployment.
There is no shared-file `integration.patch`: no shared hook was changed. The
author stops after delivery rather than starting another work loop.
