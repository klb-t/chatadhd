# LEM independent host conformance tests, pass 2

The runner reads production sources using `git show <sha>:<path>`. It does not change the checkout or substitute copies of ResearchAgent, ExperimentRunner, ResearchViewModel, ResearchRepository or AppDatabase behavior. Use Java 17; the host Kotlin compiler is pinned to the product's declared version 2.2.10. Gradle/Android are a separate gate.

```bash
python3 tools/ecosystem-audit-2026-10-09/pass2/lem/bootstrap.py --dest /tmp/lem-audit-deps
python3 tools/ecosystem-audit-2026-10-09/pass2/lem/run.py \
  --repo /path/to/LEM-Workbench \
  --sha 1755e1dfaa69fcaac6fbdd12aa95ea1a593ec456 \
  --compiler-dir /tmp/lem-audit-deps/compiler \
  --deps-dir /tmp/lem-audit-deps/deps \
  --out /tmp/lem-audit-run --phase all
```

Exit 0 means requested checks passed. Exit 1 means a test failed or an acceptance gate remains blocked. Exit 2 means compilation/execution was blocked. `--phase reproduction` evaluates only A; **A PASS means the bug was reproduced**, not a product pass. `--phase acceptance` evaluates B and the network guard. Reports always include both A and B. A fixed product should fail the corresponding old-behavior A assertion and pass B; do not use `--phase all` as a release gate for the fixed product.

The transport is a real Retrofit/Moshi/OkHttp pipeline with an application interceptor that records the serialized request and returns synthetic test-only envelopes without calling `chain.proceed`. A Java 17 SecurityManager independently denies all socket connections and its control test checks denial. No real keys are read; only an explicit synthetic noncredential is used in private in-memory preferences. Fixture responses are software test inputs, not LEM scientific measurements.

Android Context/SharedPreferences, lifecycle scope and Room annotations/builder are explicit infrastructure seams. Coroutines/Flow are real. DAO implementations are in-memory test doubles. This tests call wiring, request serialization, responses, propagation to the repository/DAO boundary and the database builder decision. **It does not test Android storage, Room generated SQL/migrations, process restart, Compose or an APK.** The Room spy observes the real `AppDatabase.getDatabase` call; it never implements/describes data deletion itself.

The baseline has no entrypoint selecting an ExperimentConfig as active. LEM-001 saves a different sole config in each repository instance, reconstructs the actual ViewModel, then invokes its existing model-name entrypoint. It proves those saved configs do not affect that consumer; it does not pretend to exercise a currently nonexistent profile-selection control. A fix that introduces a new selection API needs an explicit thin input adapter in this independent harness.

LEM-003 raw-artifact tests check necessary conditions. Nonempty references alone cannot prove raw bytes were preserved: `LEM-003-ARTIFACT-ROUNDTRIP` remains BLOCKED until a real artifact resolver/store can be exercised. LEM-005's builder decision similarly does not stand in for the separate blocked v1→v2 data-preservation gate. Do not convert either blocked gate into PASS from a schema/spy test.

Changing product signatures should cause an explicit compile/adaptation failure rather than silently dropping tests. New adapters must call the actual consumer; do not copy its logic into a fixture. Full acceptance additionally requires record reload and artifact resolution from the actual Room/runtime store once available.

For the original Gradle gate, use `gradle_probe.py --repo <repo> --sha <sha> --out <scratch> --gradle <gradle-9.3.1/bin/gradle> --jdk <jdk17> --sdk <android-sdk>`. The script extracts an immutable source archive, uses one worker and the environment's current authorized proxy. `--audit-room --phase acceptance` attaches external Room tests through the audit init script, without editing product/build files. Run the original gate separately before the focused audit gate. Room fixture v1 comes from commit `0c6ea26abecd7c401dd9cb452309741216533c30`; both entity source files are byte-identical at baseline. It is a generated real Room schema fixture, not an old APK, and does not substitute a migration implementation.

The Room wrapper returns 1 when assertions actually executed and failed, and 2 when compilation/runtime blocked without tests. `--phase reproduction` filters A; `--phase acceptance` filters B. The historical host receipt keeps its earlier blocked Room status; the newer actual Room receipt supersedes it in the test index.
