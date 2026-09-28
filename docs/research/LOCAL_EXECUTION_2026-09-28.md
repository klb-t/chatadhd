# Local execution after the Actions quota alert

The owner asked to leave the older workflow alone and move subsequent work into
the execution environment. Do not spend more attention on cancelling that run.
Keep repository checkpoints with `[skip ci]`; GitHub remains the durable record.

## Verified now

All four research suites registered in CMake can run here without CMake, a
native library, a model key, a network request or GitHub Actions:

| Suite | Passed | unittest elapsed |
| --- | ---: | ---: |
| `loom/tools/structure/test_*.py` | 350/350 | 2.151 s |
| `test_independent_cases.py` | 10/10 | 0.171 s |
| `test_graph_native_eval.py` | 12/12 | 0.501 s |
| `test_candidate_graph_eval.py` | 15/15 | 1.070 s |
| Total | **387/387** | Separate sequential invocations |

The initial attempt to run the last three suites was blocked by missing files
in the partial local checkout. Fourteen evaluator and fixture/report files were
restored byte-for-byte from remote commit
`6859998da9530eb829acf87ed8b47e719a7d589d`; every Git blob SHA was verified.
No implementation or label was altered to make these suites pass. Those files
already existed remotely and need no duplicate remote rewrite.

Reproduction from repository root:

```bash
python -B -m unittest discover -s loom/tools/structure -p 'test_*.py'
python -B -m unittest discover -s loom/tools/eval -p test_independent_cases.py
python -B -m unittest discover -s loom/tools/eval -p test_graph_native_eval.py
python -B -m unittest discover -s loom/tools/eval -p test_candidate_graph_eval.py
```

These are mechanism/protocol checks, not measured natural-language model quality
and not a substitute for the native ABI/compatibility/build gates. Offline work
can include graph projections, motif search, scorer development, synthetic
experiments and analysis/replay of retained first responses. Preserve the original
live scores and label post hoc analyses separately.

## Native build status

The available compiler is GCC/G++ 13.3; GNU Make, Python 3.12.14, Node 24.19.0,
Java and OpenSSL development headers/libraries are present. CMake is absent,
and a bounded pip installation attempt could not obtain its distribution.
Ninja and ccache are also absent, but neither is required for a Makefiles build.

The checkout still lacks most native sources, public headers and vendored
dependencies. GitHub has the missing files, accessible through read-only blob
retrieval. Before claiming a native build, restore those files and obtain
CMake >=3.24. Use the repository's vendored SQLite: the environment has its
runtime library but lacks the system SQLite development header/linker library.
No CMakeLists changes or gate removals are required for that route.

No full native build, sanitizer run, JNI test or web end-to-end test was performed
in this local transition. Prior native results retain their historical status.

## Live model requests

The dedicated `LOOM_OPENROUTER_PILOT_KEY` is not present in this execution
environment. Its existing GitHub Actions secret is not a locally readable
credential store. Do not expose or attempt to extract that secret.
A single unauthenticated public endpoint GET to OpenRouter timed out after
eight seconds; direct network access has not been established here.

Thus a new local live-model experiment is not currently ready. This is not a
BYOK configuration problem and not an exhausted OpenRouter model budget.
No paid call, retry or Actions job was started during this transition. Continue
the available offline work; the frozen 48-pair Jev experiment remains unrun.
