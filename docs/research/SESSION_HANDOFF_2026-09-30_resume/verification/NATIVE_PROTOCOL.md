# Final integration verification

Prepared before the final integration run. Run once after ROOT confirms the
shared lease and cache source freezes. The existing native build is reused only
if every file in the preserved native build input snapshot and the CLI binary
still matches its recorded digest. No native rebuild or new model/API call is
part of this verification.

The command is `ctest --test-dir loom/build/dev --output-on-failure
--output-junit <absolute verification path>/final_native_ctest.xml`, using the
workspace CTest 4.4.3 binary. Tests run sequentially with their existing timeout
and quality settings. Native CTest already includes `research.structure`; do
not repeat that suite separately when it runs here. ROOT separately owns the
full contracts and evaluation suites.

Use the declared repository/dependency paths on `PYTHONPATH`, the isolated
native Python user base for the declared requests dependency, and
`TMPDIR=/var/tmp`. `run_final_native_ctest.py` records only these selected public
environment values, code/binary digests, current HEAD, exact command, first log
and JUnit result. It does not dump the process environment or load credentials.

The script refuses existing first-outcome files. A failure is preserved and
investigated before any separately named corrected run. No gates or thresholds
are lowered. Native measurements remain separate from model-quality results.
