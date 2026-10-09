# AGEDS pass 2 — independent conformance probes

All runners take an immutable checkout/SHA and write a new receipt. They do not edit product code. Do not overwrite earlier receipts. Use synthetic input only.

```bash
python3 -m venv /tmp/ageds-audit-venv
/tmp/ageds-audit-venv/bin/pip install -r /path/to/AGEDS/server/requirements-test.txt
/tmp/ageds-audit-venv/bin/python run_python.py --checkout /path/to/AGEDS --sha <exact-SHA> --output /tmp/python.receipt.json
python3 run_kotlin.py --checkout /path/to/AGEDS --sha <exact-SHA> --compiler-dir /path/to/compiler-jars --output /tmp/kotlin.receipt.json
python3 run_store.py --checkout /path/to/AGEDS --sha <exact-SHA> --compiler-dir /path/to/compiler-jars --serialization-dir /path/to/serialization-jars --output /tmp/store.receipt.json
python3 run_scan.py --checkout /path/to/AGEDS --sha <exact-SHA> --compiler-dir /path/to/compiler-jars --output /tmp/scan.receipt.json
```

Baseline all four commands exit **1** because product acceptance fails or no profile contract exists. Do not add xfail/ignore-exit to turn this into a product PASS. `run_python.py --mode reproduction` separately requires the observed baseline signatures; its PASS does not mean acceptance. Good-mechanism observations are also in its receipt and clearly named.

Python consumer imports are real, HTTP uses in-process ASGI, SQLite writes/reads and worker state transitions are real. Network socket connect/connect_ex/create_connection are blocked. `faster_whisper.WhisperModel` alone is a capture-only test transport/model boundary; production `FasterWhisperAdapter`, `run_transcribe`, `VerifiedReader`, jobs and evidence store execute unchanged. Nothing performs actual ASR or sends external data.

Host Kotlin compiler dependencies: Maven Central `org.jetbrains.kotlin:kotlin-compiler-embeddable:2.1.20` and its runtime dependency jars (stdlib/script-runtime/daemon 2.1.20, reflect1.6.10, trove4j1.0.20200330, annotations13.0, coroutines1.8.0). The CKP pass2 dependency receipt provides shared compiler hashes. Store runner additionally uses exact Maven URLs/hashes in `serialization-dependencies.json`: serialization compiler plugin2.1.20 and core/json JVM1.8.0. This is a standalone fallback and does not satisfy project pins Kotlin2.4.20/serialization1.11.0/Gradle9.7.0/JDK21/SDK37.

`run_kotlin.py` extracts exact declarations/method bodies from the given checkout and records range hashes. Infrastructure state, resolver, coroutine scheduling and HTTP are stubs; the code under test is not reimplemented. Cache classes are compiled whole and use actual disk. `run_store.py` compiles whole store/model/cache with real serialization and local-file Context/ContentResolver stubs. `run_scan.py` compiles whole engine and parser consumers with a synthetic provider and annotation stub. None is Android device acceptance.

Unknown-field recipe/limit tests require explicit rejection or consumption. Proposed `case_id` and `date_order` test inputs are reviewable contracts for B, not claims those parameters already exist. Strategy/global-budget acceptance is explicitly `BLOCKED_MISSING_CONTRACT`: the baseline offers no configuration entrypoint to vary. The retained reproductions identify the real consumer; when B implements a contract, only supply input glue and keep the behavioral assertion.

Existing environment gates were also rerun without changing their expectations:

```bash
python -m pytest server/tests -q -p no:cacheprovider --junitxml=/new/path/existing-backend.junit.xml
node --test server/tests/js/*.test.mjs
GRADLE_USER_HOME=/tmp/ageds-audit-gradle bash ./gradlew --version
```

The browser script `server/tests/browser/acceptance.cjs` was attempted with installed Playwright1.56.1 and Chromium; see `browser-attempt.log`. No paid services or CI are used. Never substitute host compilation for the blocked pinned/device/browser gates.

Executable index: `python run_all.py --checkout <AGEDS> --sha <SHA> --compiler-dir <jars> --serialization-dir <codec-jars> --output-dir <new-empty-directory>`. It continues across failing modules and preserves separate receipts. Python default exit status follows acceptance; `--mode reproduction` independently checks the old failure signature. Fixing a bug is not a harness failure merely because reproduction ceases.
