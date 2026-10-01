# W1 — deterministic compiler/reference agreement and numeric range fix

Date: 2026-10-01 Europe/Amsterdam. Scope: W1 queue item 4 only.

## Result

The native deterministic ActiveTaskSpec renderer agrees exactly with the
public T2 reference renderer for every frozen public gold specification and
for independent multilingual synthetic cases. The comparison covers supplied
order, all nine statement kinds, active/contested inclusion, rejected and
superseded omission, conditions, executor-only labelling, UTF-8 byte spans and
the complete rebuilt `compiled_instruction`.

The audit also found and fixed one native validation defect. Locator time
ranges were compared after converting both endpoints to `double`. Adjacent
integer values above binary64's exact integer range could therefore collapse:

```text
time_start = 9007199254740993
time_end   = 9007199254740992
```

The Python relational validator rejected that reversed range, while the
pre-fix native compiler accepted it. The same defect reproduced at
`UINT64_MAX` versus `UINT64_MAX - 1`. Native comparison now preserves exact
integer ordering and safely handles mixed non-negative integer/float pairs;
the existing finite/non-negative gate remains in front of it.

## Changed files

- `loom/src/chat/active_task_spec.cpp`: lossless non-negative JSON-number
  ordering for locator time ranges.
- `loom/tests/test_chat_active_task_reference.cpp`: independent hand-authored
  renderer, Unicode/source-map, status, supersession, malformed type and
  numeric-boundary cases, including both reproduced precision failures.
- `loom/tests/compat/tool_active_task.cpp`: a narrow native compiler command
  for differential tests.
- `loom/tests/compat/test_active_task_compiler_compat.py`: exact native versus
  public-reference comparison for 44 gold specs plus new synthetic and invalid
  cases. The materializer and expanded-gold hashes are checked before use.

No schema, public ABI, frozen fixture, manifest, sealed/holdout data, shared
`docs/STATE.md`, coordination index, integration branch, main branch or PR6 was
changed. No file, source, branch, worktree, history, log or prior checkpoint
was deleted.

## Agreement evidence

The public corpus comparison covers 44 specs and 137 statements: 106 active,
4 contested, 22 rejected and 5 superseded; all nine statement kinds; 8
conditioned statements; 5 superseding statements; and 64 statements with
non-ASCII text. Native output matched `render_statements` exactly for text,
UTF-8 byte spans, statement IDs and the full copied specification.

An independent read-only differential probe added 500 deterministic random
valid specs (seed `0xA17C`) and found 500 accepted with zero renderer or
source-map mismatches. A separate 828-mutation malformed-type sweep raised no
native exception. An adversarial review exercised 400 numeric endpoint pairs
across signed/unsigned integers and doubles, including fractions, subnormals,
the `2^53` boundary, `UINT64_MAX`, the predecessor of `2^64`, `2^64` and a
large finite double; it found no mismatch after the fix.

Frozen oracle anchors verified by the committed compatibility test:

| Item | SHA-256 |
|---|---|
| public T2 materializer | `361cbdfc0d23fb3df5b7f93ee12d57a1af705499657defa62194c792b64a754a` |
| expanded dev gold | `37e5d799dc2db1e4ef632f0a324611402a9020a2f12aa7e98c813e9c7cb04eb3` |
| expanded validation gold | `fa27253ce0f5555e1e4f21c7d48eed0d048d24eb2f2aaafc35567e97a59e6ac1` |

## Executed verification

A fresh Release build was made in `/dev/shm/w1-build` because the preserved
Debug build and prior checkpoints had filled the overlay filesystem. Nothing
was removed to make room. Both changed C++ sources compiled successfully; the
Release configuration disabled `LOOM_WERROR` only because GCC 13 emits an
unrelated pre-existing optimized standard-library `-Wnull-dereference`
diagnostic in `src/import/export_common.cpp`. The existing Debug/Werror build
had already compiled the changed production object before its archive step ran
out of space.

```text
unit.test_chat_active_task_reference       Passed
compat.test_active_task_compiler_compat    Passed
2/2 targeted CTest entries passed in 1.66 s
```

```text
10/10 ActiveTask/refinement CTest entries passed in 2.56 s
```

```text
87/87 full configured CTest entries passed
0 failed, 0 skipped, 0 disabled
63.32 s
```

Fresh JUnit evidence at `/dev/shm/w1-compiler-reference-full.xml` reports
`tests=87`, `failures=0`, `skipped=0`, `disabled=0`; its SHA-256 is
`c4593b4a7a84df2232e93225eb5a08b4c3fae44d6d556972f8012d12a29550a9`.
The existing public T2 evaluator and T1 contract suites also passed 155/155 in
31.462 s. All provider-facing tests used scripted transport or local loopback;
there were no model calls, paid calls or remote provider requests.

The first broad ActiveTask run in the fresh build is not counted as a pass: it
correctly reported that the separately requested `libloom.so` target had not
yet been built. After building that target, the identical 10-test gate passed.

## Independent review

Separate agents independently compared both renderers, generated the
counterexample, fuzzed valid and malformed inputs, reviewed the numeric helper
and verified the fresh JUnit artifact. Final source review found no blocker.
Reviewed pre-commit file hashes were:

| File | SHA-256 |
|---|---|
| `loom/src/chat/active_task_spec.cpp` | `8c3ebe32cd04a9d63a609b5da0fe2beac6539543abc31631000137991e9876e6` |
| `loom/tests/test_chat_active_task_reference.cpp` | `b5f5030e6c7c8f5deb2b40a665ea78225f256dca111e1d8d2818dfbb36264211` |
| `loom/tests/compat/tool_active_task.cpp` | `d4724a415fc01e162ecac875a434d03b4e33b5971794843175a601c1e445984b` |
| `loom/tests/compat/test_active_task_compiler_compat.py` | `7780d01c844fbf5d63f6bcc0a4b3007b3cdb75933fcac43ffcf49a3c9a1fdd0d` |

## Intentional differences and remaining risks

- JSON Schema defines `integer` mathematically, so Python accepts `1.0` and
  arbitrary-size integers in integer fields. Native JSON intentionally
  requires integer storage and its exact integer domain ends at `UINT64_MAX`.
- The existing Python relational validator truncates RFC3339 fractional
  seconds after six digits and can miss future knowledge that differs only
  later in the fraction. Native comparison retains all supplied digits and
  rejects the counterexample.
- The Python validator accepts integral floats in source-map spans at schema
  level and then raises `TypeError` during byte slicing. Native fails closed.
- Raw duplicate JSON keys are rejected by the Python file loader, while the
  native JSON parser currently applies last-key-wins before the compiler sees
  the object.
- T2 fixture authoring requires every `exception` to have a non-empty
  condition; the general T1 schema and native compiler both allow an unscoped
  exception. Aligning this requires an explicit contract decision, not a
  silent renderer change.

The Python/reference limitations and fixture-grammar boundary were documented,
not changed, because this increment must not modify frozen instruments.

## Publication

Publication mapping is appended after the tested local checkpoint is frozen
and the current remote W1 head is re-read.
