# CTest execution evidence

`verify_ctest.py` compares CTest's discovered manifest with its complete JUnit
receipt. A successful outer entry is insufficient: native entries must report
positive executed cases and assertions, and unittest entries must report actual
execution. Missing entries, duplicates, failures, unexpected skips, missing
summaries and truncated outputs fail the check. No product assertion or quality
threshold is changed.

The existing opt-in `unit.test_catalog_scale` is reported as **unexecuted** when
its output contains zero cases/assertions; it does not count as 1 GB coverage.
The existing non-shared `asan` preset has explicitly named unavailable ctypes
cases; those are reported separately and subtracted from executed coverage.
`dev` and `vendored` CI builds enable the shared library and require those cases
to execute. Lineage tests require checkout of the complete preserved Git history.

From the repository root, after building the selected preset:

```bash
ctest --test-dir loom/build/dev --show-only=json-v1 > ctest-manifest.json
ctest --test-dir loom/build/dev --output-on-failure --no-tests=error \
  --test-output-size-passed 10485760 --test-output-size-failed 10485760 \
  --output-junit ctest.xml
python3 .github/scripts/verify_ctest.py --preset dev \
  --manifest ctest-manifest.json --junit ctest.xml --output executed-cases.json
python3 -m unittest discover -s .github/scripts -p 'test_*.py' -v
```

Retain the JSON evidence even after a rejected run: the command writes its
observations and errors before returning nonzero. The output capture size in
the workflow is an instrumentation setting; raise it if a complete summary
would otherwise be truncated. This check does not establish model quality or
execution of script-internal scenarios that do not expose a case count.
