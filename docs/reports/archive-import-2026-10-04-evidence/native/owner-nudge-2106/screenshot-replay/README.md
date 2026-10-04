# Independent positive screenshot replay — owner nudge 21:06

Measured source/header revision: **4e8c3dec29bb3aa1e343e09bee00b7768f0fc0f2**,
rebased on main **30ad7d37337d6641cb7714b03e9feff0e6e25d25**, including real W2.
Actual completed static core: **327052552 bytes**, SHA-256
`d4093c76c886d1b2b30b8bac4f9dc6eeeee5e994dce316dfc7ecc81ff56b2028`.
224 actual source/header files match the measured revision; source, CMake cache
and all three static libraries remained unchanged throughout this replay.
Only the tiny independent probe was compiled; no production translation unit,
core object, source overlay or replacement importer was compiled here.

**MIME 3/3; acceptance 22/22; zero live provider calls.** The three original
routes all send `data:image/png;base64,...`: no-provenance import_file control,
provenance import_file and provenance direct import_screenshot. The exact decoded
outbound PNG matches the captured fixture. Both provenance routes retain the
extensionless blob bytes, SourceRecord `mime=image/png`, declared format `png`
and matching source hash. The ScriptedTransport response is intentionally
nonvalidating; these independent acceptance assertions inspect actual requests.
The separate native regression suite additionally uses a strict mock.

The original `probe.cpp` is byte-identical, SHA-256
`2fa6ed9b1c01d5719281563f464315e4190e996ed4d4408b0288975b73f3e9f3`.
All original eleven diagnostic conditions are retained, with the old negative
MIME expectation changed to valid PNG on every route. Eleven additional checks
validate full request bytes and stored source bindings. No assertion was weakened.
This probe tests PNG only; it does not certify all image types or a real OCR
service. Format is declared by filename, not inferred from content signatures.

The exact independent negative archive from
[6e4bf03](https://github.com/klb-t/chatadhd/blob/6e4bf03d6294719de8df4e9477e417d232c7b87b/docs/reports/integrator-import-screenshot-2026-10-04/evidence.zip)
is retained unchanged as `original-negative-evidence.zip` (79707 bytes,
SHA-256 `c840cc9ca0948253460478918ced73bc13155cab65d281c686964e81129fcfae`).
Its historical result was MIME 1/3, with two malformed routes; eleven diagnostic
checks established that negative, rather than acceptance. Its receipt remains
pinned to old source `03cd52e`; it is not a failure of this new revision.

Run from any fresh completed checkout/build, selecting the exact source revision:

```sh
python3 replay_acceptance.py --repo /path/to/chatadhd \
  --build-dir /path/to/completed-build --out-dir /path/to/fresh-output \
  --source-commit SOURCE_COMMIT
```

Optionally bind `--expected-core-sha256` to the completed build's recorded hash.
The runner checks actual CMake source files against that revision and retains
before/after source/library snapshots. It requires fresh output and records
failure with a nonzero exit; it adds no provider permission. The completed
production build evidence remains necessary to bind objects to these sources.

`receipt.json`, all logs, unmodified probe, PNG and plain result JSON preserve
this measured execution. Source snapshots are losslessly gzip-compressed;
`compressed-SHA256.json` records compressed and original hashes. Decode to their
original names to compare the unchanged receipt inputs. No databases, executables
or static libraries are packaged. The obvious in-memory dummy key is synthetic.
Compile/link: 9.495 s, peak 342828 KiB; run: 0.076 s, peak 14100 KiB. These
are probe resources, not an import performance benchmark or a full CTest gate.
