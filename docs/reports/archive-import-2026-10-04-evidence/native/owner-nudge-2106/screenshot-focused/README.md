# Screenshot OCR — supplementary strict offline red/green proof

The historical core archive, plus the new acceptance test, passes **2/6 cases**.
Adding the corrected `importer_core.cpp` and `importer_text.cpp` objects passes
**6/6 cases, 872/872 assertions**. This is a derivative focused build; it is not
a full build or CTest gate of the mixed main/W2 kernel.

| Executed variant | Cases passed | Assertions passed | Exit | Run time |
|---|---:|---:|---:|---:|
| Historical core + new test | 2/6 | 216/224 | 1 | 0.172 s |
| Same archives + corrected importer objects | 6/6 | 872/872 | 0 | 0.417 s |

[receipt.json](receipt.json) is the unchanged measured receipt. It records all
four compiled sources and three linked archives before/after, exact commands,
`-O0 -DNDEBUG`/WERROR flags, exit codes, timings and binary hashes.
`source_stable=true` covers those seven pinned inputs; it is not an expanded
header/toolchain manifest. [SOURCE_BINDINGS.json](SOURCE_BINDINGS.json) separately
confirms that the four source hashes match published commits `bf8b620` and
`4e8c3de`; this Git lookup was performed during packaging, after measurement.

The historical core SHA-256 is
`f2002cbe0a527039bf9b1c03753c4707f5a85d0667da70b2cee805233f83a9c0`, matching
the [original integrator screenshot negative at 6e4bf03](https://github.com/klb-t/chatadhd/blob/6e4bf03d6294719de8df4e9477e417d232c7b87b/docs/reports/integrator-import-screenshot-2026-10-04.md).
That earlier diagnostic measured MIME preservation on three routes. Its
denominator differs from these six acceptance cases. The new strict transport
rejects an incorrect full OCR form body instead of returning successful OCR for
malformed input. The red run stops failed format loops at their first failure;
it does not exercise every format against the historical implementation.

The green cases exercise `import_file`, direct `import_screenshot`, both
no-provenance controls, png/jpg/jpeg/webp with uppercase variants, exact NUL/high
bytes and base64, blob/source MIME/hash/size and conversation/message provenance.
The tiny PNG replays all three original routes. A test-only SQLite source-insert
hook overwrites or removes the original after snapshot/admission and before OCR;
both routes still send the captured bytes. Preflight mutation separately rejects
stale admission before provider/source/conversation writes. Image decoding or
live-provider acceptance is not measured. All data/keys are synthetic; no network
or paid calls occurred.

Full [red stdout](run-red.stdout), [green stdout](run-green.stdout), their stderr
and every compile/link stdout/stderr are retained, including empty logs.
All files are small and remain uncompressed.

[executed_runner.py](executed_runner.py) preserves the exact Python source passed
on stdin in the measured tool invocation. It was retained as a file during
packaging; no runner-file hash was recorded before execution. Its original host
paths and result-overwrite behavior remain unchanged. It is not a portable runner
or evidence of a later execution. Reproduction requires the exact source/archive
hashes and build dependencies from the receipt; keep this copy unchanged and
adapt a separate copy for a different host/output directory.

[SHA256.json](SHA256.json) binds the retained files, their byte sizes and original
hashes; no binaries, objects, databases or private keys are included.
