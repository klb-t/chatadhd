# Final native regression evidence

The preserved full native first run passed **73/74 CTest cases** at HEAD
`a439e6e5116615aa5f569f9f489a8810603c69af` in 177.530 seconds. The only failure
was `research.structure`: 851 discovered unit tests with one cwd-dependent
agentic test error. CTest runs in the native build directory; the test had
assumed a repository-root relative configuration path. The first log, JUnit,
environment and summary were retained without modification.

The owner made a test-only portable relative-path correction; production
experiment code, prompts, manifests and compilers stayed unchanged. A minimal
package initializer also makes all 30 shared lease tests part of standard
discovery. No quality gate, threshold or native timeout was weakened.

After ROOT authorized the narrow follow-up, **research.structure passed 1/1
CTest case, containing 851/851 unit tests**, at HEAD
`065673e35aa401c808dbb7390ca35fca4783227e` in 37.889 seconds. No full 74-case
repeat was run. The other 73 native first results are reused because all 250
preserved native build input digests and all recorded native binary digests
still match. This is a preserved first run plus a corrected narrow follow-up,
not a claim of one clean 74-case run at the final working tree.

The two exact Python deltas are recorded in
`structure_followup_environment.json`: test SHA-256
`44d84a5ff01e8810b78a43b4895c8f8333564706e69cf619e9a500ea7157f23b`
and package initializer SHA-256
`8f433d2d5f37d9ec330c6f0c6a2e99b5d1d092706b0c7bf66c0198415bf602c0`.
All pinned Python bytes stayed unchanged during the follow-up. The CLI binary
remains `8e784b882b9a509f4e1f55bae50748d738a896e5a198a02781cb0c4168c23c2c`;
native input aggregate remains
`7549cbd40f54c9f894792016bc9208af030a0ad00dc466511607434b82d14882`.
The existing dev build retained Werror, shared C ABI, OpenSSL, vendored SQLite
and the HTTP server. No rebuild was needed.

Captured first-run subsets passed: `research.independent_protocol` 10 tests,
`research.graph_native_protocol` 12, `research.candidate_graph_protocol` 15,
and `compat.test_abi_compat` 3. `server.smoke` passed in 2.122 seconds. Native
CTest does not include the whole contracts or evaluation discovery suites;
ROOT owns their separate final verification. No additional full structure
unittest invocation was performed outside CTest.

CTest truncates successful JUnit stdout after 1024 bytes. The first follow-up
summary consequently has an empty extracted unit-count list; it is preserved.
`structure_followup_test_output.log` saves CTest's complete LastTest output,
and `structure_followup_output_receipt.json` proves `Ran 851 tests` / `OK`
without another test execution.

Environment selection and commands were saved before each outcome:
`TMPDIR=/var/tmp`, repository plus declared contract dependencies on
`PYTHONPATH`, isolated native requests user base, and bytecode disabled. No
process environment dump or credential lookup occurred. This lane made zero
actual model/tool API calls, paid no external charges, and did not open sealed
validation or blind holdout data. Timing is a run record under concurrent work,
not a performance comparison with the earlier baseline.

Next step: ROOT integrates the exact verification staging manifest alongside
the preserved test/freeze amendment, runs the missing full contracts/evaluation
suites once, and continues separately frozen DEV model experiments. The
composition and validation wrappers still require an explicit release before
actual sealed cases can be replayed or scored.
