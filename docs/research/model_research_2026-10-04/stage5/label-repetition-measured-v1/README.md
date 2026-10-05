# Selected label repetitions — public measured snapshot, 2026-10-05

All 192 planned first attempts are represented: two selected recipes, the same 48 authored DEV queries over the same 12 source families/cases, repeated twice. Supplied source-relative label availability is 192/192. This reuses the original population; it is not a new independent sample, holdout, native grounding, semantic-adequacy judgment or content/world-truth measurement. No winner or automatic promotion is declared.

| Recipe | Repetition 1, correct / planned | Repetition 2, correct / planned | Pooled correct / planned | Label availability | First-attempt USD |
|---|---:|---:|---:|---:|---:|
| `j_active` | 40/48 | 40/48 | 80/96 (83.33%) | 96/96 | 0.003399816 |
| `j_directed` | 44/48 | 45/48 | 89/96 (92.71%) | 96/96 | 0.003798984 |

The unique 192-attempt cost is **USD 0.007198800**, with zero unknown-cost rows in the supplied normalized attestation. `j_active` incorrectly refutes 16/48 pooled unknown-source judgments, and `j_directed` 7/48. Full confusion tables, failed IDs and every supplied first-attempt resource field remain replayable. Costs and first-response latency describe recorded attempt cohorts, not an independent account audit or whole-workflow inference speed.

Original [`SCORE.json`](SCORE.json) is copied byte-for-byte, SHA `4d64e9798be4f54b8659819e8c933dc0ca117bd9a704b80c467f8a946c7ffcfa`. `SCORING_INPUT.json` is preserved byte-for-byte inside [`replay-inputs.zip`](replay-inputs.zip), SHA `842feeee072fb6aced135795a451031ef532f35d411c05b0cc91a4324770b2a3`. Scorer SHA is `9aec8378f5e51b78cf7a9bfcb02fbcb1a3d027e3471a0ebdef6ea27d5e8ea560` at published source snapshot `9812ef64e8f6940a7d5210dc073d4c8aa2cc978f`, with explicit runtime selector `/cases`, full gold source SHA `417e03f56e10608ab9daeec3bbdedef177b41c134ca363ca0200c070853617f7` and selected canonical array SHA `8b51b82ee9a8cb07856d5e176b2dbceb655a37baeef639542e298385819f1bdc`. No scoring input, label, criterion, threshold, preparation or source code was edited. The export derives metrics from the existing SCORE; it does not rerun models, test suites or semantic scoring.

The existing `programme_results_v1.export_results` and shared method/ModelProfile metric/native DTO graph formats produce [`packet.json.gz`](packet.json.gz). Caller DATA adds explicit repetition subcohorts through the unchanged generic graph exporter. Each arm has one pooled 96-operation run and two 48-operation runs, all dated 2026-10-05. The graph has 1,966 entities, 5,484 claims and 8 source observations, with six cohort runs and 12 source-commitment accuracy/availability metrics. It creates zero ModelProfile entities and performs no CABI/native process or canonical-store write. Existing Python DTO/schema validation is recorded in [`VERIFICATION.json`](VERIFICATION.json); native grounding and semantic adequacy remain null.

The graph's 384 event occurrence records are overlapping pooled/repetition views of **192 unique physical first-attempt IDs**, not 384 calls. Resource values across those views are alternative aggregates; the unique-attempt total above counts each ID once. Method/prompt/parameter versions and requested versus observed identities are preserved. Supplied observed identity is `typesafe/jev-1.13-20260917` / `TypeSafe`; all measurements remain specific to the historical recipe, population and date.

The compact directory contains five files. Replay ZIP contains 105 complete plain payloads (5,963,017 bytes) plus `REPLAY_MANIFEST.json`: original SCORING_INPUT, all supplied normalized rows/request bodies/response projections, group reports, METHODS, caller configuration, derivation metadata and 96 distinct original request byte sequences. All 192 request bodies reconstruct through the existing canonical encoder and match their original SHA; duplicate scientific repetitions intentionally share the same request bytes. Every ZIP payload's SHA/size and CRC is verified. No public projection is substituted for an original response hash.

**Projection boundary:** responses preserve all supplied public projections and the original private HTTP/generation hashes, byte counts and loss declarations. Full original HTTP/GEN envelope bytes are absent from this public input and cannot be regenerated here. The execution owner retains a separate durable private checkpoint; this exporter never reads it, the private ledger, credentials or account data. Public normalized billing/identity attestations are retained without claiming independent private replay or proving absence of unpublished attempts.

Restore evidence paths from the repository root:

```sh
python -m zipfile -e docs/research/model_research_2026-10-04/stage5/label-repetition-measured-v1/replay-inputs.zip docs/research/model_research_2026-10-04/stage5/label-repetition-measured-v1
python -m loom.tools.structure.method_graph_export_v1 docs/research/model_research_2026-10-04/stage5/label-repetition-measured-v1/configuration.json --output /tmp/label-repetition-packet.json
```

Use a new output path; the CLI preserves existing files. Extract at this exact directory because configuration binds repository-relative evidence paths. Existing `graph.export` + `codec.encode_packet` reproduces the raw packet byte-for-byte; raw SHA is `ff948762ae99c6e1765a093713885677d60ede7d7bb252aa7a59543f044880a9`, packet ID `4f094534dc81e5aa396933ea0f9c33f299a31d772bfd9a74f9b9896147efe4ee`. Raw packet is 30,146,965 bytes; deterministic gzip is 1,975,553 bytes with SHA `52088dac761b60c497380e6a3a65db0c5253e4d32b5a46dee66d15a06569666e`. Replay ZIP is 616,669 bytes with SHA `e6eca028717841f4f727a9d8bb0a81989d72b7d106177819f69bf134bb5d526a`. This is full supplied public evidence compression, not a data-removal or file-size policy.
