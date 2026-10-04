# ChatADHD / Loom documentation

The current reports below are the starting point. Detailed historical runs are
retained through the archive rather than presented as current product claims.

| Read this | For |
|---|---|
| [Synthetic demo](DEMO.md) | A short introduction and repeatable walkthrough for visitors |
| [Porządki — raport po polsku](RAPORT_2026-10-02.md) | Przyczyna rozjazdu, odzyskana praca i granice odtworzenia |
| [Privacy review](PUBLICATION_PRIVACY_2026-10-02.md) | Source audit, historical Git contact metadata and the separate local correction |
| [Current state](STATE.md) | Accepted implementation, verification and open engineering work |
| [Claude handoff](HANDOFF_2026-10-02_TO_CLAUDE.md) | Development ownership, recovered source and next integration boundaries |
| [Verification report](verification/current-2026-10-03/RESULTS.md) | Current native integration gates, source hashes and retained first failures |
| [Previous web/demo verification](verification/current-2026-10-02/RESULTS.md) | Recorded production web build and actual native/browser demonstrations |
| [GraphPacket native store](NATIVE_GRAPH_PACKET_STORE.md) | Explicit acceptance, immutable receipts, CAS, readback/replay and migration |
| [Application profiles](APPLICATION_PROFILES.md) | Versioned interface/workflow data, simultaneous views, actual Loom adapters and evidence gaps |
| [Limits and wiring](LIMITS_AND_WIRING_2026-10-01.md) | Missing connections and remaining resource/policy work |
| [Research and costs](RESEARCH_AND_COSTS_2026-10-01.md) | Conclusions with task-specific denominators and spending reconciliation |

## Technical references

- [Loom build, C ABI and pipelines](../loom/README.md)
- [Web workbench](../loom/web/README.md)
- [Conceptual model](architecture/LOOM_CONCEPTUAL_MODEL.md)
- [Contract index](contracts/README.md)
- [Self-hosting demonstration](selfhost/README.md)

## Reconstruct earlier work

- [Archive and exact active-tree selection](archive/README.md)
- [Historical branch map](BRANCHES_2026-10-01.md)
- [Bounded conversation recovery](CONVERSATION_RECOVERY_2026-10-01.md)

Historical measurements refer to their own source snapshots. Retained research
fixtures are development data and regression inputs, not independent holdout.
