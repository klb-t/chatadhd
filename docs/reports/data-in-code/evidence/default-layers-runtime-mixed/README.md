# Independent W12 layers / W11 runtime-profile proof

This is a standalone evidence harness, separate from full repository tests.
`capture.py` compiles two isolated executables from immutable input copies:
the selected baseline W11 implementation and the current W11 implementation,
both using the same pinned **actual W12** `DefaultLayers` and
`runtime_profile_values` sources. It does not replace their code, compile a
second production resolver, create a database, invoke a provider or change
root CMake. A result is positive only when its retained `receipt.json` says
`valid:true`; preparing this runner alone does not establish integration.

Layer identities and bindings in the fixture are explicitly synthetic and
evidence-only. Values come directly from actual native builtin descriptors.
Each top-level setting group is bound atomically, so this probe does not claim
that per-item identities inside arbitrary arrays are production bindings.
The proposal policy is copied from W12's actual `profiles/user.pack`, with its
source SHA recorded. No method execution, model-quality score or graph run is
fabricated.

Per domain the runner verifies:

- Exact direct-builtin versus W12-adapted defaults, version hash and builtin flag.
- A changed value accepted by the actual native schema, explicit user-layer
  precedence and preserved caller provenance, then clearing the override.
- Immutable original profile/layer snapshots throughout all operations.
- Disable and permanent exclusion remove effective values. Suppression of a
  required group must return the native schema error; it cannot restore a preset.
- Forward update, disappearance and reappearance retain permanent exclusions.
  A new default in the excluded area remains a proposal; explicit direct-area
  mode applies the new default without clearing the old exclusion.
- Explicit reenable applies the latest pack value; identity recycling is rejected.
- Unknown state metadata and serialized layer state survive reload.
- Current exact inspection definition/defaults and authoritative raw-source hashes.

The same baseline domains must retain identical complete descriptors, defaults,
hashes and fixture bindings. Additional current domains are reported separately.
The domain count is taken from the actual embedded table, not a manually
advertised constant. Current source files must still match their captured bytes
after both executions. The receipt retains complete source inputs, commands,
compiler version, positive/negative outputs and binary hashes; compiled binaries
are temporary and are not included in the repository.

Run from any checkout containing the chosen baseline and W12 git objects:

```sh
python3 docs/reports/data-in-code/evidence/default-layers-runtime-mixed/capture.py \
  --root "$PWD" \
  --baseline fffa9fd557db97cae549d555959fedf3910d346c \
  --w12 3c0bc36552ef9851f1174946cfb108549aae3228 \
  --output /tmp/w11-runtime-layers-evidence
```

Existing output directories are refused. Keep any negative directory intact and
choose a new destination for a corrected run. Full `OnboardingStore` CAS,
durability, actual consumers and native method/result graph edges are outside
this proof and remain separate integration gates.

The actual retained run is [actual-2026-10-05/receipt.json](actual-2026-10-05/receipt.json):
19 domains / 162 groups / 1,828 checks before; 22 domains / 179 groups / 2,148
checks after. All 19 shared complete descriptors/defaults/hashes/bindings match;
23 source provenance records were rehashed. Both compiler and native stderr
streams are empty. A separate read-only reviewer rehashed all 56 input records,
the fixture/policy/output streams and provenance files and found no mismatch.
The two originally identified harness-review issues (runner-byte drift and
vacuous empty provenance) were fixed before this actual execution.

All source/header graphs and provenance bytes are retained, so original git
objects are unnecessary for offline reproduction:

```sh
python3 docs/reports/data-in-code/evidence/default-layers-runtime-mixed/replay.py \
  --captured docs/reports/data-in-code/evidence/default-layers-runtime-mixed/actual-2026-10-05 \
  --output /tmp/w11-runtime-layers-replay
```

`replay.py` verifies the captured source hashes and requires byte-identical
native stdout. It is supplied for reproduction; it was not run as an additional
build in this session. The actual positive result above is from `capture.py`.
