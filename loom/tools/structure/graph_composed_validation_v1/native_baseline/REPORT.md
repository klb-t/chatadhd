# Fresh native baseline, 2026-09-30

Build succeeds and full **CTest passes 74/74** in 42.44 seconds. No C++ source,
header, test or gate was changed by this auditor. The source snapshot recorded
before compilation remains byte-identical afterward. The current configured
development catalog gate passes; an older red-gate status is stale for these
exact sources. This is a local baseline, not a merge or a production-quality
claim on the owner's archive.

Build environment:

- GNU C/C++13.3.0, CMake4.4.3, Ninja1.13.2;
- `dev` preset, warning-as-error and shared C ABI retained;
- `LOOM_USE_SYSTEM_SQLITE=OFF` because system SQLite development headers are
  absent; the repository's unchanged SQLite3.47.2 amalgamation is used;
- `LOOM_BUILD_SERVER=ON`, OpenSSL detected and enabled;
- four compile workers in the 8GB/8CPU workspace; compilation243.28 seconds;
- workspace-only build tools and declared Python dependencies; no paid models.

The source snapshot covers kernel `.cpp`/`.h`, public headers, native tests and
root CMake files. Exact configuration and reproduction commands below identify
the server/CLI build as well. The vendored-SQLite variant is reported separately
from historical system-SQLite runs; no quality threshold was relaxed.

First full CTest result **70/74** is preserved. Its four failures were identified
before correcting the environment:

1. chat compatibility, semantic recovery and utility compatibility lacked the
   declared `requests>=2.28.0` dependency;
2. structure discovery from the build working directory lacked the repository
   import root, and a concurrently added agentic module needed a normal
   package/discovery import fallback.

The module owner repaired only those imports and retained the first failure.
`requests2.34.2`, urllib3 and certifi were installed in an isolated Python
userbase, which remains visible when CTest compatibility tests override
`PYTHONPATH`. The second run adds repository root plus the existing declared
contract dependencies to `PYTHONPATH`. It changes neither native code nor
primary semantic policy. Full74 then pass with no JUnit-disabled/skipped tests.

The first server smoke returned success after internally skipping for missing
requests; that first exit code does **not** establish server coverage. The second
smoke genuinely runs and prints successful knowledge/catalog/context, general
server and authorization checks. Shared ABI checks3/3 and native candidate-graph
compatibility checks9/9 execute and pass. The successful CTest research structure
output records **762/762** after concurrent new Python methods were added. The
nested composition20/20 suite is separate from this discovery count.

## Frozen development catalog measurement

Original selector and original `synthetic_dev` fixture,65 labeled conversations
(45 relevant,5 traps,15 generic noise), plus auxiliary documents kept separate:

| Measurement | Result |
|---|---:|
| Selection recall | 31/45 = .688889 |
| Selection precision | 31/31 = 1 |
| Trap false positives | 0/5 |
| Generic noise selected | 0/15 |
| Final ranking AUC, printed | .965556 |
| Final hit@20 | 20/20 |
| Final hit@45 | 42/45 |
| Lexical hit@45 | 36/45 |
| Semantic-feature hit@45 | 43/45 |
| Lexical-only diagnostic rescue | 5 relevant,0 noise,2 auxiliary |
| Semantic-only diagnostic rescue | 2 relevant,0 noise |

The unchanged gates remain recall≥.55, precision≥.75 and trap FPR≤.05. These
results reproduce the development selection baseline; no new tuning or
validation measurement occurred. The feature named `semantic_score` is the
configured native selector's channel, not a live LLM quality measurement. Its
diagnostic disagreement with lexical evidence remains visible. The owner
archive, new sealed graph-validation split and older blind catalog holdout were
not opened by this audit.

## Reproduction

From repository root, install workspace-only public build tools and declared
requests. CMake/Ninja locations can be replaced by equivalent installed tools.
Then run from `loom/`:

```sh
cmake --preset dev -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_BUILD_SERVER=ON \
  -DCMAKE_MAKE_PROGRAM=/absolute/workspace/native-build-tools/bin/ninja
cmake --build --preset dev --parallel 4
TMPDIR=/var/tmp \
PYTHONPATH=/absolute/chatadhd:/absolute/contract-deps \
PYTHONUSERBASE=/absolute/native-python-userbase \
ctest --preset dev --output-junit /absolute/result.xml
```

`TMPDIR=/var/tmp` avoids the unrelated Git ancestor under `/tmp` in credential
handoff mechanism fixtures. All first and corrected logs/JUnit artifacts are
preserved. Binaries, build trees, tool wheels and dependency packages remain
outside tracked artifacts. `artifact_manifest.json` pins the report, exact first
measurements, environment/source snapshot and both regression outcomes.
