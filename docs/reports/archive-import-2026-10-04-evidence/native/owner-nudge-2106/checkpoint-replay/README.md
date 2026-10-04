# Independent checkpoint replay — owner nudge 21:06

Measured source/header revision: **4e8c3dec29bb3aa1e343e09bee00b7768f0fc0f2**,
rebased on main **30ad7d37337d6641cb7714b03e9feff0e6e25d25**, with real W2.
Actual static core: **327052552 bytes**, SHA-256
`d4093c76c886d1b2b30b8bac4f9dc6eeeee5e994dce316dfc7ecc81ff56b2028`.
Observed source/header files match the revision; libraries and source inputs
remain unchanged throughout this separate replay. Only two small probes were
compiled and linked directly to that completed core plus SQLite/miniz. No
production translation unit, overlay or replacement importer was compiled.

**2/2 scenarios; 31/31 checks; zero provider calls.** Malformed known member
passes 17/17 checks, remains visibly partial with errors over all three attempts,
and never becomes a completed-source cache hit. Valid-project transient link
failure passes 14/14: after removal of the failing SQL trigger, replay restores
exactly one project, one document and one `part_of` link; the third import caches.
Source and conversation identities remain stable. Both databases pass integrity
and foreign-key checks with one conversation and one message, without duplication.

Original negative probes remain pinned in
[the historical archive](https://github.com/klb-t/chatadhd/tree/archive/2026-10-04/integrator-import-checkpoint-replay).
The only C++ adaptation is explicit `options.resume=true`, preserving the
library's compatibility default. `adaptation.json` and the runner enforce exact
original/adapted bytes; every original scenario condition remains checked.

```sh
python3 verify_checkpoint_replay.py --repo /path/to/chatadhd \
  --build-dir /path/to/completed-build --out-dir /path/to/fresh-output \
  --source-commit SOURCE_COMMIT
```

The runner checks actual CMake source/header bytes against that revision and
records complete source/library manifests, commands, binary hashes, synthetic
ZIPs and result JSON. This source observation does not replace the completed
production build's own evidence binding objects to these sources.

The unchanged receipt, all logs, plain results, tiny source ZIPs, adapted probes
and runner are packaged. Source snapshots are losslessly gzip-compressed with
both original and compressed hashes in `compressed-SHA256.json`. No executable,
database or static archive is copied. This independent replay is separate from
full CTest, native focused counts and screenshot MIME acceptance.
