# Readiness orphan fix — independent offline replay

Source: `5753d2cf93dcba4edd5c35534b667f23b915c9b7`,
`loom/tools/structure/model_study_readiness_v1.py` SHA256
`86b362b980b1efe3067ad86646da69539c1e43c42844c4484b874d9a32ac8f98`.

All three original synthetic conditions now match the expected result:
valid-ledger-plus-orphan blocks; no-ledger-plus-orphan blocks;
valid-ledger-without-orphan remains unblocked. Unbound responses are not
adopted or scored. This confirms the specific fix, not completion of lane7.
The original failed result remains on
[its archive branch](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-readiness-orphan-response/docs/reports/integrator-readiness-orphan-2026-10-04.md).

[evidence.zip](evidence.zip) retains the exact original runner, pinned source,
synthetic manifest/ledger/response bytes, literal outputs and `REPRODUCTION.json`.
`SHA256.json` hashes every payload. All prices, responses and names are fixtures;
transport/key loading throw, zero provider calls and zero credential reads.

To replay, extract outside the repository and run with its public checkout:

```sh
python3 replay.py /absolute/path/to/chatadhd /absolute/path/to/new-output
```

The portable runner only changes input/output path selection; `reproduce.py`
is the original unchanged runner. Fetch the public model-research branch first
so the pinned object is present. Readiness dependencies in the reviewed checkout
remain the base versions; their actual hashes are in `REPRODUCTION.json`.
Do not infer a live campaign/billing audit from this mechanism replay.
