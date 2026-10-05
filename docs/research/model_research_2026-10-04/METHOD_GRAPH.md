# Accepted research method graph

[`method-graph/final/`](method-graph/final/) is the accepted offline projection of this lane's public saved receipts into the existing `loom.graph_packet/1` format. It preserves exact method configurations, actual scripted producers, intended model configurations, runs, dated evaluations and source-byte bindings. It does not execute a model or write the canonical native store.

| Artifact population | Accepted count |
|---|---:|
| Declared configured-method descriptors | 55 |
| Declared runs / native run Entities | 34 / 36 |
| Entities / Claims / source Observations | 310 / 487 / 23 |
| Research evaluation Entities / legacy profile metric Claims | 44 / 16 |
| Scripted events | 60: 36 old frontier + 12 Stage 3 + 12 Stage 4 |
| Existing ModelProfiles | 2 unchanged Jev profiles |

The extra two native runs describe the exact offline projection of the two existing ModelProfiles; they do not reconstruct their original model invocation settings. Their sixteen metric Claims retain the original dates/metrics and explicitly rebind evidence to the saved profile report. No scripted method is promoted to a fictitious ModelProfile. All 60 scripted events retain request/result/response identities and link to their actual scripted producer. Separate `intended_configuration` Claims retain prepared model/provider/recipe settings without attributing the scripted output to a real model.

The accepted raw packet is 9,313,697 bytes, SHA-256 `62fe32c0ac26fa13df69d79545b83a7555d58d50c889e8b8722cf3426964956a`, packet ID `4ffa721e2526617503d6e3f45109a1bdddf16e529a8b42a6468b904233fe7a50`. Repository storage uses deterministic [`packet.json.gz`](method-graph/final/packet.json.gz): 541,399 bytes, SHA-256 `ec8cd76e703639bc5f8d7e880227c73c500decdbd3143c383b6b30e776b9ff38`, gzip mtime 0 and empty filename. Decompression reproduces the exact accepted bytes. [`VERIFICATION.json`](method-graph/final/VERIFICATION.json) records both hashes; its recovery change concerns storage only. `configuration.json`, `METHODS.json`, `EVENT_MAPPING.json` and the eight-check proof preserve their accepted bytes.

The generic exporter is the corrected accepted source SHA-256 `537589147b8548a86c68de1a9726b6fb423e52aee02ce2a185232ad83a9943b6`. It checks saved-byte hashes, recipe material, declarations, exact JSON pointers/support spans, event bindings and complete counted populations. A planned `one_of` selector must select `identity_pointer`; an arm-label list cannot masquerade as a planned identity denominator. Literal missing IDs remain in that denominator, failures are retained, and `equals` group filters count all selected records. Scripted/planning metrics cannot become measured model quality. The focused suite has 14 passing tests.

## Relation to the production contract

The current canonical production [packet-side METHOD_GRAPH contract at `b302df25`](https://github.com/klb-t/chatadhd/blob/b302df25e1a65f20c395eadc5ad5ef065d26e33d/loom/src/packet/METHOD_GRAPH.md) defines `loom.method_graph/1` as a client graph-construction manifest. It is not a `loom_packet` request operation. This lane's accepted GraphPacket vocabulary remains a research consumer proposal; it is not a W3 execution export or native persistence receipt. No production contract, registry, dispatcher, default or store is changed here.

| Research projection | Canonical production role and remaining mapping work |
|---|---|
| Method/version/configuration Entities | Stable method identity and immutable effective method-version definitions; preserve exact consumed parameters and hashes |
| Parameter preset/configuration attrs | Separate parameter-set, preset, recipe and prompt versions with exact definitions and native relationship Claims |
| Component lists | Ordered method/nested-combination occurrences and active membership Claims; retain repeated paths, weights and effective overlays |
| Saved run/event producer edges | Concrete run and result Claims to exact method version and applicable compiler transform, with independent producer instrumentation |
| Dated `method_evaluation` Claims | Separate literal-valued evaluations, evaluator/instrument Observations and source locators; never rewrite provenance confidence |
| Existing saved ModelProfiles | Preserve version/date/population limits and distinguish report projection from original inference |

W1 owns versioned prompts/recipes; W3 owns registry execution/results; W4 owns native records/history/persistence; W7 owns measured experiments and dated profiles. Consumer agreement for this research vocabulary remains open. Native shape validation establishes neither execution provenance nor content truth. Requested and observed model identities, exact transport/compiler/rendered/sent-byte hash scopes and unknown instrumentation must remain distinct. Unknown values remain null.

The [latest W4 report at `b302df25`](https://github.com/klb-t/chatadhd/blob/b302df25e1a65f20c395eadc5ad5ef065d26e33d/docs/reports/native-graph-packet-2026-10-04.md) confirms the canonical W3 execution export plus 3/3 alias/nested supplemental variants through the actual registry and native store, with 29/29 GraphPacketStore and 110/110 full CTest. W4 reports no remaining format dependency blocking W3. Those supplemental variants use saved synthetic replies and zero transport/provider calls. Older INDEX/integrator snapshots still list format reconciliation as open; joint producer/native review and combined-source integration gates remain separate from this lane's recovery. W3's [report3 at `03c670c`](https://github.com/klb-t/chatadhd/blob/03c670c/docs/reports/chat-selector-2026-10-04.md) describes the source-private execution/selector API and its distinct producer proof. This research projection does not claim those production proofs for itself.

## Recovery and reproduction

The accepted source/data closure comes from the immutable [v3 review archive at `f14a4035`](https://github.com/klb-t/chatadhd/blob/f14a4035eaad896a9905439fc3a60e46c6b0f1ba/docs/research/model_research_2026-10-04/negative-research-review-20261004-v3.zip). Its nested v2 and original archives preserve all seven negative controls, their exact predecessor sources, failed inputs, before/after observations and captured pinned Git blobs. Manifest checks verified 40, 61 and 89 retained files respectively. Rejected drafts and failed proofs stay in that archive branch. The [pre-freeze archive](https://github.com/klb-t/chatadhd/blob/f14a4035eaad896a9905439fc3a60e46c6b0f1ba/docs/research/model_research_2026-10-04/method-graph-pre-freeze-20261004.zip) preserves the older graph drafts.

From the repository root, use new output paths:

```bash
PYTHONPATH=loom/tools/structure:. python -m unittest \
  test_frontier_reply_followup_v1 test_method_graph_export_v1 -v
python -m loom.tools.structure.method_graph_export_v1 \
  docs/research/model_research_2026-10-04/method-graph/final/configuration.json \
  --output /tmp/accepted-research-packet-new.json
python -m loom.tools.structure.method_graph_build_v1 \
  --output docs/research/model_research_2026-10-04/method-graph/new-build \
  --exported-on 2026-10-04 \
  --followup-directory docs/research/model_research_2026-10-04/followup-frontier-reply/final-verified-lineage
```

The builder stores gzip and verifies its decompression. A different output path becomes part of the declaration-source identity and therefore changes packet bytes; exact-byte builder reproduction uses the accepted relative path in an isolated scratch root. Recovery reran 27/27 focused tests and all seven archived negative controls, reproduced all eleven followup outputs exactly, replayed the exporter to the accepted packet hash, and reproduced the builder's compressed packet/configuration/method/event outputs. Current evidence is in [`verification/followup-method-graph-recovery.json`](verification/followup-method-graph-recovery.json).
