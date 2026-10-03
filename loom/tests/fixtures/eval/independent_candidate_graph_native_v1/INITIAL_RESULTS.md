# First independent native validator parity result

The first recorded native run passed all nine test methods. It reproduced all
32 independently expected validity decisions and the saved Python decisions:
26 nonempty accepted draft sets, four valid empty abstentions, and two rejected
references. This is native **validator parity**, not native graph-projection
equivalence or live-model extraction quality.

## Measured outcome

| Check | Development | Fresh validation |
| --- | ---: | ---: |
| Expected validity and Python validity agree | 16/16 | 16/16 |
| Source packet and original bundle retained exactly | 16/16 | 16/16 |
| Partial drafts match expected originals or rejection-empty result | 16/16 | 16/16 |
| Coverage and located unknowns match gold and Python | 16/16 | 16/16 |
| Packet/vocabulary hash and report contracts hold | 16/16 | 16/16 |
| Inference and persistence remain disabled | 16/16 | 16/16 |
| Accepted nonempty draft sets | 13 | 13 |

Each split includes two accepted abstentions and one rejection. Empty drafts
are checked against their expected state; they are not counted as represented
success. Rejected reports preserve the original inputs, return no draft entries,
and leave coverage and packet hash unknown/unset under the declared contract.
No default evidence class, confidence or native status was introduced into the
partial Entity/Claim drafts. Existing packet records remain untouched.

Four independently frozen mechanical probes also passed: a mismatched quote,
Boolean byte offset, duplicate argument ordinal and unbound variable were all
rejected, with source inputs retained, empty drafts and closed gates. These are
additional contract checks, not four new semantic-quality examples.

The stdin wrapper rejected malformed JSON/envelopes and input over 4 MiB. Its
preparse depth guard rejected depth 130 while permitting quoted brackets and
escaped quote/backslash content to reach ordinary validation. This does not
claim exhaustive parser or native hard-preflight coverage.

All 32 source cases independently passed raw-source and exact UTF-8 support
checks before invoking C++. Retention comparisons are type-exact JSON, so
Boolean values cannot silently substitute for numeric offsets/ordinals. The
Python reference report is pinned and checked for the complete unique case set;
it is a parity reference, while the frozen manual labels remain the validity
oracle. Error records were checked for shape and presence, not identical text
or validation ordering across languages.

## Recorded implementation

| Artifact | SHA256 |
| --- | --- |
| `loom/src/extract/candidate_graph.cpp` | `5beca05fef288acbdd24afa8897a8408fe07288a47d622e6772180b167626f2e` |
| `loom/include/loom/knowledge_candidate_graph.h` | `fca23892a95893e02b30fb40b4973f587280b8001ea946e7d02bb53f48b7f579` |
| Frozen vocabulary file | `0d02838638e6f82c4e5f1cc90e3842f152f4db0ac44d593721cddc8e6af8650d` |
| Python reference report | `82ca75d3d3cd8679c084d452a5c253b82d499e3729d066cfe9ba701af786abeb` |

The report also records the actual executable, runner and evaluator hashes.
The coordinator built the executable; the independent evaluator did not build
or change the validator. No method was tuned against these outcomes, and the
completed nine-file Python pilot remained unchanged.

## Limits

The C++ validator emits original local-handle partial drafts and no comparison
graph. This run therefore establishes neither graph-incidence parity nor
matching quality. Inputs already contain manually authored occurrence, scope,
binding and support annotations. Prior Claim arrays are empty, and the source
spans are whole sentences. This is not evidence of spontaneous model
decomposition, context retrieval, logical proof, native promotion, production
model recall or cost. No provider call occurred.

## Reproduction

After the coordinator has built the runner at the recorded implementation:

```sh
LOOM_CANDIDATE_GRAPH_NATIVE_TOOL=loom/build/dev/loom_candidate_graph_native_tool python loom/tests/compat/test_candidate_graph_native.py -v
LOOM_CANDIDATE_GRAPH_NATIVE_TOOL=loom/build/dev/loom_candidate_graph_native_tool python loom/tests/compat/test_candidate_graph_native.py --write-report /tmp/native-candidate-parity.json -v
```

CTest discovers `test_candidate_graph_native.py` through the existing compat
glob. Normal execution does not rewrite saved reports. The recorded first
result is `initial_report.json`; later validator versions require new result
files and hashes rather than overwriting it.
