# Independent native candidate-validator parity gate

This gate reuses the already frozen 32-case source-gold candidate-contract pilot.
It introduces no new semantic labels and does not change the completed Python
pilot. It evaluates the pure C++ validator, not natural-language extraction,
provider behavior, canonical promotion or a C++ comparison-graph projection.
The native validator does not emit that graph.

## Fixed inputs

| Input | Frozen hash |
| --- | --- |
| Development canonical payload | `cb14ad33834be118def4a2305f503c58e5dac3a91cb20dcbf5223bac3b99501b` |
| Validation canonical payload | `4dcb59baaccd6deb914c5504004649a5e2f926183b69b0797e43ec654a273787` |
| Vocabulary file SHA256 | `0d02838638e6f82c4e5f1cc90e3842f152f4db0ac44d593721cddc8e6af8650d` |

The inputs remain under `independent_candidate_graph_v1/`. Their earlier
source/graph labels were manually authored, with 16 cases per split and eight
English/eight Polish per split. Each split has 13 represented intentions, two
explicit valid abstentions and one malformed reference to reject. This native
gate preserves those denominators. Exact source bytes and snapshot references
are independently checked before invoking the native tool.

Method authors receive only the public API contract. They receive neither
validation examples nor outcomes for tuning. If the native first run differs,
its result is retained and reported to the coordinator before any method
revision. Later versions require separate reports and hashes.

## Transport and scope

`candidate_graph_native_tool.cpp` is a standalone, pure validator runner. It
accepts one stdin JSON envelope containing `packet`, `bundle` and `vocabulary`,
bounded to 4 MiB, invokes the library validator, and emits one JSON report.
Before JSON parsing, a quote/escape-aware scan bounds raw container depth to
128; quoted brackets do not contribute to that depth. A depth-130 transport
probe and an escaped-string control exercise this guard separately.
Malformed transport/envelope input exits nonzero. A well-formed request whose
candidate fails validation emits the native rejection report and exits zero.
The runner does not construct a Runtime, open a database or call a provider.

The CMake owner builds the tool and sets `LOOM_CANDIDATE_GRAPH_NATIVE_TOOL` for
`test_candidate_graph_native.py`, which is discovered through the existing
compat-test glob. The independent evaluator does not build native code.

## Measurements

- Expected validity decisions against the independently authored labels, and
  separately parity with the saved frozen Python validator outcomes.
- Exact retained packet/bundle content, raw source/hash coherence, source-byte
  coverage and located unknowns. Accepted empty abstention is distinct from a
  represented graph intention; rejection coverage may remain unknown.
- Normalized partial-draft fields against immutable pre-call source gold,
  including ports, polarity, assertion context, scope/binding references,
  support identity and any copied locator/text hash.
- Absence of invented native Assessment/entity evidence, confidence or active
  defaults; explicit inference and persistence gates remain closed.
- Type-exact JSON retention (Boolean values cannot replace zero/one), independent
  valid-packet digest checks and null rejected-packet hashes. The saved Python
  reference report is pinned by SHA256 and checked for the exact fixture/case
  set. Error records are checked for shape and presence, not identical wording.
- Wrapper bound, malformed JSON/envelope handling and clean machine-readable
  output. Normal test execution does not overwrite the recorded report.

Four separate mechanical rejection probes were frozen before native execution
in `negative_probes.json`: mismatched quotation, Boolean byte offset, duplicated
argument ordinal and an unbound variable. Their canonical self-excluding payload
hash is `6389d020ca10911ede4ed31b36e0a416407808147fcfc794d5b7f4a8187537e7`.
They test enforcement that the 32-case source corpus cannot fully establish;
they do not enlarge its semantic quality denominator. Rejected probe input must
remain available, drafts empty, and inference/persistence disabled.

The native hard-preflight contract has a separate retention exception for
over-depth/resource/invalid-UTF8 inputs: it may decline recursive copying and
report `retained_input: null` plus an explicit `retention` reason. Ordinary
bounded semantic/schema rejection retains all input. The frozen source cases
and four probes remain within those hard bounds; the runner's transport-byte
guard runs before the validator. No claim of full preflight coverage is made.

Native success means validation/retention parity for these supplied contracts.
It does not establish graph-projection equivalence, logical truth, model recall,
live response size, token cost or broad semantic coverage. Timings, if recorded,
include the process wrapper and are diagnostic rather than model latency.
