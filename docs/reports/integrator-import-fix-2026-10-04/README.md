# Independent W5 checkpoint-fix replay

This positive checkpoint regression does not accept the whole W5 branch.
The separate screenshot regression is retained in archive and blocks admission.

[evidence.zip](evidence.zip) SHA256:
`b8a203c53e54cfd9ea4d73f13375238665c9ea1a2fe9e264adb1e50c03ba1607`
(58449 bytes).

Reviewed branch receipt: `03cd52e33e5902ffb90d33ec0084cea2bf60feaa`.
All 211 actual `loom/src` / `loom/include` files in the completed build source
tree match that Git revision. Actual core archive SHA-256:
`f2002cbe0a527039bf9b1c03753c4707f5a85d0667da70b2cee805233f83a9c0`.
All three linked archives and all checked source/header files remained identical
before and after execution. The independent runner compiled no production object.

The exact original two integrator probes are preserved in `original/`. The sole
adaptation in the executed probes is one `options.resume = true;` line per file,
because the C++ compatibility default changed since the original negative run.
Original/adapted/input/output/library/binary hashes and all commands/resources
are recorded by `receipt.json`, `source-before.json`, and `source-after.json`.

## Result

**2/2 scenarios and 29/29 independently asserted controls passed**:
15 controls for the malformed known member and 14 for transient valid-project
failure. This denominator belongs to this runner, independently of the branch's
published 31-control acceptance runner.

- Malformed known `projects.json`: all three attempts remain partial with errors;
  none is a completed-source cache hit. One raw node is retained without typed
  project records. Source/conversation IDs remain stable; final source status
  is partial.
- Valid project/document with first-attempt link failure: first attempt is
  partial; after dropping the synthetic failure trigger, the second restores
  exactly one project, one document and their `part_of` link; the third uses the
  completed-source cache. Source/conversation IDs remain stable.
- Both databases pass quick/integrity and foreign-key checks.

Two serial compile/link invocations took 7.77 and 5.74 seconds; peak measured
child RSS was approximately 351,116 KiB. Probe runs took 0.05 and 0.03 seconds.
The wait4 RSS readings can include startup memory inherited from the Python
hashing runner and are resource receipts, not import-memory benchmarks.

No paid/model/network calls, owner archives, sealed corpus reads, repository
source edits, checkouts, fetches or publication were performed in this replay.
The known P1 checkpoint-laundering failure is independently closed for these
two original scenarios. This does not replace fresh mixed-W2 integration gates,
the scoped branch's full CTest gate, or the coordinator's acceptance decision.

## Retained reproduction

`replay-executed.py` is the exact executed runner, retaining its original host
paths and exact core-archive hash requirement. `replay.py` is a portable variant:
its only changes add CLI path/source/hash inputs and a fresh output directory.
The scenario logic, original probe adaptation, and 29 asserted controls are
unchanged. Build the pinned public W5 source in a fresh external build directory
with C++20, CLI/tests/static core, vendored SQLite and OpenSSL, then run:

```bash
python3 replay.py --repo /path/to/chatadhd --build-dir /path/to/w5-build \
  --out-dir /tmp/w5-independent-replay \
  --source-commit 03cd52e33e5902ffb90d33ec0084cea2bf60feaa
```

The default original-probe directory is the packed `original/` beside the script.
The runner obtains the actual source root from `CMakeCache.txt`, compares its
source/header bytes with the public Git object, and records archive hashes before
and after execution. An optional `--expected-core-sha256` additionally requires
an exact measured library hash. A freshly built library may differ bytewise
because of compiler/build environment while still using the same checked source;
fresh clean-build evidence remains necessary to bind objects to that source.
`--out-dir` must not exist; the runner saves its own used source and all commands,
resources, input ZIPs, outputs and hashes in that new directory.

The portable variant was syntax and CLI-help checked; it was not rerun as another
acceptance result. The measured 2/29 execution belongs to the exact retained
`replay-executed.py` and its original receipt.

Both generated ZIPs, full JSON receipts, compile/run logs and original probe
sources are retained. Synthetic ZIP timestamp metadata may differ on another
generation, so new receipts must not overwrite these measured originals.
