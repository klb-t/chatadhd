# Independent PASS4 C boundary tests

`run.py` imports the pinned checkout's actual credential handoff, preparation
connector, final public builder, workflow Queue, payer manifest validator and
native GraphPacket FFI. Inputs are synthetic fixtures or public C artifacts.
No model/payer dispatch runs. Python socket connections are denied; native store
workers are disabled. Credential tests execute the actual generated browser
script inside a Node VM with WebCrypto and no network APIs.

Required: Python 3, jsonschema, cryptography, Node; for native probes, the compiled
Loom library and a C++20 compiler. Dependencies are ordinary host dependencies.

```sh
python3 tools/ecosystem-audit-2026-10-09/pass4/C-boundary/run.py \
  --repo /path/to/C --sha EXACT_C_SHA \
  --prior-repo /path/to/C2 --prior-sha b9b503f62bb8e8e00c94ab7401e1cd9579129af3 \
  --native-lib /path/to/libloom.so \
  --native-source /path/to/B --native-sha EXACT_B_SHA \
  --output /var/tmp/pass4-c-receipt.json
```

`run_registry.py` compiles only the independent `registry_probe.cpp`, linked to
actual product objects/archive. Supply either a PASS4 selective build attestation
or an ordinary build directory. The selective path validates every provided
object and source hash; the full build path must be attested by the caller.

```sh
python3 tools/ecosystem-audit-2026-10-09/pass4/C-boundary/run_registry.py \
  --repo /path/to/B --sha EXACT_B_SHA \
  --research-repo /path/to/C --research-sha EXACT_C_SHA \
  --build-dir /path/to/native/build \
  --output /var/tmp/pass4-c-registry.json
```

The ordinary build directory contains `libloom_core.a`, `libloom_miniz.a` and
`libloom_sqlite3_amalgamation.a`. Alternatively replace `--build-dir` with
`--object-manifest /path/to/pass4/B-native/object-manifest.json`.

Exit 1 is expected on the audited commits because acceptance criteria fail.
Reproduction PASS is recorded separately and never turns the product green.
The native input tests intentionally distinguish raw DTO input, accepted-receipt
input, and diagnostic DTO normalization. Only the first currently fails;
normalizing inside a test is not a repair. No historical receipt is overwritten.
