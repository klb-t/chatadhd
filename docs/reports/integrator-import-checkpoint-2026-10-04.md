# W5: known-member checkpoint replay can hide failed interpretation

Reviewed receipt: `c7972252a23cb13c54797f6b57df24de903c5296`.
Executed importer source receipt: `d77867517bb128b7e43236c436a2dc3a31e1ae46`.
The complete `loom/src` and `loom/include` trees are byte-identical between those
receipts. The newer receipt changes the Python audit fee names and evidence only.
All input archives and message/project content in these probes are synthetic.
No network, model calls, owner archives, or sealed evaluation data were used.

The probes compile every W5 importer implementation plus W5 `db.cpp` and
`annotations.cpp`, then link unchanged dependencies from an existing baseline
native static library. Exact source, input, result and baseline-library hashes
are recorded in `manifest.json`.

## Observed results

| Scenario | First import | Second import | Third import | Final project nodes |
|---|---|---|---|---:|
| Known `projects.json` has invalid JSON | `partial=true` with two errors | `resumed=true`, `partial=false`, no errors | completed-source cache hit | 0 |
| Valid project/document, transient failure creating their link | `partial=true`; provider transaction rolls back | after removing failure trigger: `resumed=true`, `partial=false`, no errors | completed-source cache hit | 0 |

Both scenarios leave one `export:member` node and a journal row for
`projects.json` with `kind=member`, no errors, `partial=false`. The final source
metadata says `import_status=complete`. The valid project/document are never
recreated after the transient database failure is removed.

## Cause and request to W5

`loom/src/import/export_archive.cpp`:

- `Run::checkpoint_member()` rolls back provider writes when interpretation
  fails, but returns an error that leaves `handled=false` in the auxiliary loop.
- The same loop falls through to `unknown_member()` and runs a second
  `checkpoint_member()` with the same source/member/archive-index/source-index.
- That successful byte-retention fallback persists a journal delta excluding
  the errors already added by the failed provider attempt.
- On retry, provider `checkpoint_member()` sees the fallback journal first,
  replays its clean delta and returns success before calling the interpreter.
  `set_source_outcome()` then records the whole archive as complete.

Please keep unsupported-member detection separate from failed interpretation
of a recognized member. A retention checkpoint must not stand in for a completed
provider interpretation. Retention may be recorded independently, while a failed
recognized interpretation remains pending and its errors remain visible.

Required regressions: retry the valid-project/link-failure probe after removing
the trigger and expect one project, one document and their link; preserve IDs
and avoid duplicate records. Repeating the malformed known-member import must
continue reporting partial/errors and must not return a completed-source hit.

## Reproduce

Copy `reproduce.py`, both probe `.cpp` files, and this README into a fresh
directory without existing `run` or `run-link-failure` subdirectories. Supply a
clone containing the pinned receipt and an existing baseline native build:

```bash
python3 reproduce.py /path/to/chatadhd /path/to/chatadhd/loom/build/dev
```

The runner extracts the pinned public source through read-only `git show`,
compiles the overlay with C++20, and generates both synthetic scratch databases
and synthetic ZIP archives. It refuses to overwrite existing replay runs.
For comparison with the original observed receipts, retain these saved hashes
and result JSON files; newly generated ZIP metadata may have different hashes.

## Preserved archive

Full original evidence bundle: [review-evidence.zip](integrator-import-checkpoint-2026-10-04/review-evidence.zip). It includes both portable C++ probes, runner, fabricated input ZIPs, literal results and source/library/input/result hashes. Unzip into an empty directory before running the commands above. The original W5 branch is unchanged. This negative archive is not admitted to main.

## Do wątku 5

Repair recognized-member retry semantics and add both regressions above, then publish a final report and run full gates with the actual W2 policy. Keep failed interpretation visible independently from successful source-byte retention.
