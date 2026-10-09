# Independent discovery/native probes

Compile the audit-only `probe.cpp` against the pinned actual native static archive. No replacement registry, resolver, parser or adapter is implemented. `run.py` verifies repository SHAs and records source/archive hashes.

```bash
python3 tools/ecosystem-audit-2026-10-09/pass3-resources/discovery/run.py --repo /path/to/main --sha 9e20f99ab27e7cd45e1892f83bf60fbe60db3de9 --build-dir /path/to/main-build --research-repo /path/to/C2 --research-sha b9b503f62bb8e8e00c94ab7401e1cd9579129af3 --output /tmp/discovery-receipt.json
```

Dependencies: g++ C++20, compiled `libloom_core.a`, `libloom_miniz.a`, `libloom_sqlite3_amalgamation.a`, OpenSSL development libraries; no Python packages beyond stdlib. Native Runtime uses actual ScriptedTransport exclusively, with workers disabled. Synthetic ZIPs and profile data are generated in a temporary directory. Research input is the committed public C2 handoff v2, never a private archive.

Exit 1 means an acceptance FAIL. The unbound new discovery-contract integration has separate BLOCKED status. Successful reproduction never makes the product PASS. `DISC-C2-02` uses actual model serializers diagnostically; it does not normalize/repair product inputs. Native GraphPacketStore applies the actual lossless gate. A standard JSON semantic equality check in the fixture diagnostic is supplementary: the receipt enumerates every scalar representation difference, and unknown DTO fields still must be rejected.

`DISC-PROVIDER-04` records BLOCKED until an integrated discovery consumer is bound. It requires four semantic distinctions, never invented field names. Equivalent explainable facets are acceptable. This does not claim that old `can()` promised authorization. Other gates exercise existing contracts unchanged.

Historical harness receipts distinguish an initially omitted required fusion fixture value and the intermediate overly strict canonical-byte diagnostic from product failures; the final receipt retains the raw C2/registry failure and all required rejection gates.
