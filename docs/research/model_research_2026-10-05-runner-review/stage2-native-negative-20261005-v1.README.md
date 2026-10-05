# Stage2 full public native negative — 2026-10-05 v1

This archive preserves the complete failed first-response batch and its public reproduction closure. It is prepared for `archive/gpt/model-research-runner-review-2026-10-05`; publication and commits are owned by the root agent. Nothing here promotes a candidate to production.

The 12 original Stage2 public files are retained byte-for-byte at their original repository paths, including REQUESTS, RESPONSES, NORMALIZED, NATIVE_RECEIPTS, METHODS, configuration, four group reports, the generic graph packet and producer receipt. All 60 first-content strings and choices remain, including all 32 truncated responses, eight other malformed JSON bodies, one duplicate-key body and all 19 decoded/native-rejected alternatives. No content repair, retry selection or alternative merging was performed. Complete private HTTP captures are outside this public projection; their raw hashes remain references.

Public cost representations agree at **USD 0.1639080** over all 60 requests, with 120,590 input and 82,663 output tokens. Native validation has **0/60 accepted first responses**: 41 strict response failures and 19 native rejections (16 quote mismatches, two shape errors, one span bounds error). All 32 `length` responses hit the frozen 1,600-token limit; nine `stop` responses also fail the strict parser. These observations do not establish that a higher cap would solve the contract.

The frozen source-only reference contains 15 inspected synthetic DEV cases and 48 criteria. The four methods retain **192 planned criterion slots, all unresolved**, with semantic accuracy null. The final 60-record manual diagnostic review, unchanged-scorer output, three mechanical audit files and all exact support/pointer bindings are archived. No best native recipe, semantic-zero result, heldout quality, world truth or Stage5 winner is established.

Two graph projections are separate. The original generic packet retains the actual method/prompt/parameter identities and public resource claims. The added `stage2/native-diagnostic-graph-v1` packet uses the same four observed method/version/configuration identities, 60 events and 12 evaluations in the existing shared ModelProfile metric format: native validity **0/15 on the mechanism axis**, **48 unresolved source slots on the diagnostic axis**, and semantic agreement **unavailable/null**, per method. It creates no ModelProfile entities or canonical store write. Both configurations, packets and exact source closures are included.

`MANIFEST.json` gives the exact size and SHA-256 of every other ZIP member; `SOURCE_MANIFEST.json` records source roles and Git bindings. Current Python sources/tests include native adapter `f92da5c4…`, its test `6eb24bde…`, and unchanged semantic scorer `86e46aa1…`, bound to research commit `1259d47596455e041662409c69ec96fdc79f3210`. The archive copies the native CMake/core/include/vendor/test build tree at main commit `30ad7d37337d6641cb7714b03e9feff0e6e25d25`; its 330 copied native source files match the current checkout exactly. The two exact public synthetic DEV archive fixtures used by source projection are included; no holdout or private account/key data is included.

The original reference freeze pins a pre-fix test import. Both exact sources are retained: corrected test `ea971ae8…` at its ordinary path, and old test `31544304…` under `historical-pre-flat-import/`, recovered from the existing immutable native-full-negative archive. Only the import changed for CTest flat discovery; source inputs, scorer criteria and thresholds did not change. Its recovery provenance and the independently recorded full-gate metadata remain available. An initial archive closure check lacked the two raw synthetic fixtures; its negative log is retained, and the final copied tree passes all 32 focused tests after those unchanged fixtures were added.

No binary bytes are copied. `NATIVE_BINARY_PROVENANCE.json` and the diagnostic platform record bind the observed candidate-tool SHA-256 `d3e8411f099698bca28d6f7f089eb8f5ba5920637826eb447796db6d86c79def`, all six recorded platform binary hashes, C++ inputs, Git objects and selected build options. Rebuilding on another platform can yield different binary bytes. Recorded full 108-entry gate results are evidence from the execution owner, not a fresh full-gate run inside this archive.

Extract into an empty directory and use that extracted repository-shaped root for replay. The original evidence paths must remain intact:

```bash
python -m zipfile -e stage2-native-negative-20261005-v1.zip extracted-stage2-negative
cd extracted-stage2-negative
```

Verify every extracted payload against the manifest:

```python
import hashlib, json
from pathlib import Path
for name, binding in json.loads(Path("MANIFEST.json").read_text())["members"].items():
    raw = Path(name).read_bytes()
    assert len(raw) == binding["bytes"]
    assert hashlib.sha256(raw).hexdigest() == binding["sha256"]
```

The recorded Python environment is 3.12.14 with jsonschema 4.26.0 and requests 2.34.2. Replay the unchanged source scorer and the original generic graph without any provider call:

```bash
python -m loom.tools.structure.stage2_semantic_review_v1 --reference docs/research/model_research_2026-10-04/stage2/semantic_reference.source_only.v1.json --inputs docs/research/model_research_2026-10-04/stage2/inputs.synthetic_dev.json --prepared docs/research/model_research_2026-10-04/stage2/prepared/prepared.json --reviews docs/research/model_research_2026-10-04/stage2/SOURCE_INTERPRETATION_REVIEWS.v1.json > score-replay.json
python -m loom.tools.structure.method_graph_export_v1 docs/research/model_research_2026-10-04/programme-actual/stage2-final-20261005-v1/configuration.json --root . --output graph-replay.json
```

Compare parsed scorer data with the archived SCORE file, and raw graph bytes with the decompressed original packet. Substitute the diagnostic configuration path to reproduce its separate packet. Clean copied-tree replay reproduced the complete scorer data, original raw graph and all 19 native receipts; only fresh wrapper latency measurements differ.

For native replay, rebuild the existing `loom_candidate_graph_native_tool` target from the copied `loom/CMakeLists.txt` using the toolchain/options recorded in provenance. The stdin object has exactly three keys, `{"packet": ..., "bundle": ..., "vocabulary": ...}`; it calls `validate_candidate_graph_bundle(bundle, packet, vocabulary)` and writes a report to stdout. Process exit 0 does not mean candidate validity. Use the unchanged `programme_native_results_v1` CLI with the archived NORMALIZED/prepared/source-inputs/vocabulary/policy/reference files and your binary path/hash, writing to a fresh output file. Compare exact stdin/stdout/stderr capture hashes and report data. A rebuilt binary's hash/provenance describes that new replay, not authentication of the old platform executable.

All reproductions are public selected-field, source-binding and mechanical checks. They do not independently authenticate private raw captures, generation-to-request proof bindings, account usage or absence of unreported operations. The owner retains the original append-only generation proof chain separately. No paid call, CABI/store execution, adoption or new corpus occurred in this packaging task. All original files in the working repository remain untouched.
