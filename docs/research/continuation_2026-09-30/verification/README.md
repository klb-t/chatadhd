# Verification — continuation 2026-09-30

Source: `a79862837c6726f534471f8dbcdccfe749dfb942`; tree `e24142cdf897a48ba1f51b08dc3edd216d423c29`.
Native source hashes were checked again after the run; no source changed.

| Check | Observed result |
|---|---|
| Current all-target native build | Pass; Debug dev, WERROR, shared C ABI, CLI and server |
| Current full CTest | **77/77**, 0 failed, 0 skipped; 94.60 s |
| Focused CTest subset | **10/10**, 14.36 s; already included in 77 |
| New chat/context cases | **15/15** chat compilation + **9/9** depth/detail controls |
| Current research.structure | **821/821** unittest cases within CTest; 39.81 s CTest time |
| Catalog synthetic_dev gate | **31/45 recall**, **31/31 precision**, traps **0/5**, generic noise **0/15** |
| Current source projection, separate check | **56/56**, source/hash receipt retained |
| Web scenario, recover_history agent | Pass; **5 local fake-provider calls**, **0 remote calls** |

Catalog recall threshold remains 0.55 and precision threshold 0.75. Ranking AUC
was 0.965556; hits@20 20/20 and hits@45 42/45. This is a fresh development-fixture
measurement, not blind/real-archive accuracy or a claim that this patch improved
the catalog. No threshold was weakened.

The build used bundled SQLite 3.47.2 because system SQLite headers were absent,
GNU 13.3.0, CMake 4.4.3 and Python 3.12.14. Test dependencies were installed only
in the verification workspace; `TMPDIR=/var/tmp` is required for the credential
handoff tests to remain outside the ancestor Git workspace. Environment,
binary/source hashes and exact commands are in `current-ctest-environment.json`.

## Source-projection provenance correction

`source-projection-review.log` is the earlier independent **52/52** run, before
four adversarial regression tests and the corresponding fixes. Its original
log mtime is 2026-09-30 19:15:21.786959 UTC; the test-start timestamp, execution
revision and source hashes were not captured. It is **not** validation of final
candidate `a798628` and is retained under `earlier_independent_checks`.

The final **56/56** suite was previously reported only in tool output. To close
that concrete evidence gap, one authorized rerun at `a798628` started at
2026-09-30 19:27:31.054929 UTC and passed in 0.156 s of unittest time.
`source-projection-current.log` preserves its output;
`source-projection-current-receipt.json` records the command, environment and
unchanged source hashes captured before and after that run. The 56 cases are
27 inherited contract, 26 boundary/batch, two receipt parity and one CLI test.
No native/full-suite tests were repeated for this correction.

## Baseline and first failures

The pinned `fafc77f` Python baseline passed eval **213/213**, contracts **185/185**
and seeding **36/36**. Native baseline compilation was paused to prioritize the
current candidate; **native baseline tests were not run**. There is no fresh
native baseline pass count. Suite denominators must remain separate.

Original setup/build failures are retained rather than replaced:

- `baseline-eval.log`: two imports failed before jsonschema was installed;
  the complete dependency-equipped eval run then passed 213/213.
- `baseline-structure-direct.log`: 12 of 821 tests errored because their temporary
  credential paths were inside an ancestor Git workspace. All 12 passed with
  `/var/tmp`; the current full 821-case suite also passed in that environment.
- `current-server-build.log`: the initial mixed build retained the old
  `list_runs(int)` signature while headers changed. After freezing sources and
  touching changed public headers, the stable server and full build succeeded.
  A follow-up server build was a no-op and symbol inspection found only the new
  `list_runs(int, string_view)` signature. The mixed binary was never tested.

`current-ctest-details.log` is the full CTest LastTest log. JUnit truncates some
successful stdout, so use the details log for catalog metrics and research
unittest counts. `results.json` is the compact machine-readable summary;
`manifest.json` hashes every delivered file. All files are UTF-8 text, including
logs and XML. No binaries, real credentials, live model responses or real user archive
content are in this package.
