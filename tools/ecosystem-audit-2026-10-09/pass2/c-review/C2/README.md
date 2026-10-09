# Independent C2 boundary acceptance probes

`run.py` takes `--repo`, exact `--sha`, `--native-lib`, `--native-sha` and `--output`. It imports the target checkout's actual queue, payer, transport, graph producer, Python native wrapper and shared native engine. `jsonschema` is needed. The payer author's synthetic setup/mock supplies fixtures only; no author test methods run. Socket connect/create_connection are denied, native workers disabled, real provider calls zero.

Example from the audit workspace:

```bash
PYTHONPATH=audit-work/continuation/scratch/c-review/deps:audit-work/deps python3 audit-work/continuation/artifacts/tools/ecosystem-audit-2026-10-09/pass2/c-review/C2/run.py --repo audit-work/continuation/checkouts/C2 --sha b9b503f62bb8e8e00c94ab7401e1cd9579129af3 --native-lib /workspace/scratch/1112e9e8a3c6/audit-work/continuation/scratch/native/main-build/libloom.so.0.1.0 --native-sha 9e20f99ab27e7cd45e1892f83bf60fbe60db3de9 --output /tmp/c2-receipt.json
```

Exit 1 means a product acceptance failure, including a successfully reproduced bug. Exit 0 requires all acceptance gates. Source SHA, library SHA and canary hashes are recorded; canary content and raw exceptions are never serialized. A caught public-CLI error is rendered by Python's actual `sys.excepthook` into a private memory buffer to test its uncaught CLI diagnostic boundary. Public input files are copied into temporary directories; the real CLI's `__file__` is patched only to select those input fixtures, preserving its actual imports and producer.

The two existing probes in `../run.py` remain unchanged. Re-run phases `projection` and `native` against the target SHA. Their old `C-PUBLIC-00` compares an intentionally historical embedded program to the latest source. Do not treat that mismatch as a product regression: `C2-HISTORY-01` independently checks unchanged historical packet bytes and the prior SHA. Original receipts remain available.

New fixture costs are synthetic and do not modify or represent the owner's campaign. Actual PrivateLedger reserves, validates and settles only the temporary fixture ledger. The mismatch probe restores an offline pending queue via the supported public API, never by database editing. It tests settlement projection association, not a bypass of the new verified dispatch admission.
