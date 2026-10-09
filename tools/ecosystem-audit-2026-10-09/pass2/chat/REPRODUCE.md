# Independent chat consumers

Use a clean checkout at the exact supplied SHA. All fixtures are synthetic. No paid call is allowed; Python sockets are blocked and provider requests intercepted. Native Runtime is constructed with the existing ScriptedTransport and workers off.

Python dependency: `requests==2.34.2` (Python3.12 used). Install in a temporary venv, never product source.

```sh
python python_consumers.py --repo CHECKOUT --sha SHA --phase reproduction --output NEW_A.json
python python_consumers.py --repo CHECKOUT --sha SHA --phase acceptance --output NEW_B.json
python native_run.py --repo CHECKOUT --sha SHA --build-dir CMAKE_BUILD --phase both --output NEW_NATIVE.json
```

Build the checkout's actual `loom_core` with CMake/C++20, OpenSSL and its vendored sqlite/miniz; native_run checks the build source and runs the freshness build before linking. It records archive hash; do not substitute an unrelated archive. `--phase both` records A and B separately. Exit1 means FAIL or missing acceptance contract, not successful product behavior. CH004/005/006 full acceptance has explicit missing contracts; never treat their captured reproduction as authorization/admission success. The CH006 metadata check is necessary only, not a proof of valid references. Changing an interface requires adapting the explicit contract adapter, not reimplementing policy in the test.

Main and B use the same scripts. Current receipts: docs/reports/ecosystem-audit-2026-10-09/pass2/chat/{python-main,python-B,native-main,native-B}.json. All four runs returned1. GUI, device lifecycle and real external providers are not asserted.

Peer review removed invalid assumptions about budget=0, a hand-invented memory binding, fixed fallback as forbidden default and transport-count guards. No existing product test was weakened.
