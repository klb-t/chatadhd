# PASS4 AGEDS / LEM probes

Runs immutable product sources from `git show SHA:path`; a checkout and explicit commit are required. No product files are edited. Use a **new** output directory. `--phase acceptance` fails for B/contract/control failures; reproduction PASS is never acceptance PASS. Baseline LEM exits 1; AGEDS exits 0.

```sh
python tools/ecosystem-audit-2026-10-09/pass4/ageds-lem/run.py --suite ageds-js --repo AGEDS_CHECKOUT --sha 9c1d513bc19d177bd324d7506a21fbab98c2e268 --out NEW_JS_OUTPUT
python tools/ecosystem-audit-2026-10-09/pass4/ageds-lem/run.py --suite ageds-kotlin --repo AGEDS_CHECKOUT --sha 9c1d513bc19d177bd324d7506a21fbab98c2e268 --compiler-dir KOTLIN_COMPILER_DIR --deps-dir KOTLINX_SERIALIZATION_DIR --out NEW_KOTLIN_OUTPUT
python tools/ecosystem-audit-2026-10-09/pass4/ageds-lem/run.py --suite lem --repo LEM_CHECKOUT --sha 1755e1dfaa69fcaac6fbdd12aa95ea1a593ec456 --compiler-dir KOTLIN_COMPILER_DIR --deps-dir LEM_RUNTIME_DEPS_DIR --phase acceptance --out NEW_LEM_OUTPUT
```

Use JDK17 and the pass2 LEM compiler/dependencies (`pass2/lem/bootstrap.py`, manifest and README). This suite reuses its published infrastructure fixtures in `pass2/lem/fixtures`; it does not copy product algorithms. AGEDS Kotlin requires `kotlinx-serialization-core-jvm` and `kotlinx-serialization-json-jvm` 1.8.0 in its dependency directory; only those, stdlib and annotations enter this suite's classpath. No serialization compiler plugin is needed for the tested constructors/JsonElement API. Exact consumed jar/source hashes are written to receipt.

Observed local dependency locations were `continuation/scratch/lem/{compiler,deps}` and `continuation/scratch/ageds/serialization`; these are conveniences, not portable requirements. The test inputs/outputs are synthetic. Do not replace labels with credentials or load private archives. LEM application interceptor never calls `proceed`, and JVM SecurityManager rejects all sockets. Android preferences/lifecycle/DAO, audio backend/EventTarget are explicit test seams. Actual UI rendering, device storage, MediaPlayer and external endpoints are not exercised.

`A4-LEM-001` oracle allows the original authority for both subrequests, or explicit interruption after the first on authority change. It separately preserves revocation: clearing the key must prevent the second request. `A4-LEM-002` rejects old registry data as current after clear/rekey. AGEDS transform oracles preserve object meaning, pinned IDs/version, exact quote and ordered selectors; they explicitly do not treat list order or every numeric type as interchangeable.

An unknown operative field in the closed create-response selector is rejected explicitly. This is not the external-resource requirement to preserve unknown source fields, and the tests do not claim universal format or graph support.
