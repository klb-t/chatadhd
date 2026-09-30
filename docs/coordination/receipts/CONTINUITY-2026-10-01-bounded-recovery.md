# Bounded source-recovery follow-up — 2026-10-01

ROOT followed up the remaining `semantic_sketch` source gap while the published
W1–W6 lanes were being integrated. This is a recovery receipt, not a new
implementation or a claim that the historical prototype was tested again.

## Material examined

Two saved MHTML copies of the earlier handoff conversation were retrieved and
decoded locally with Python's MIME parser. The scan covered every decoded MIME
part, in addition to extracting visible HTML text.

| Saved copy | Bytes | SHA-256 |
|---|---:|---|
| `Przejęcie pracy i synchronizacja.mht` | 3,516,989 | `4c4cdd7898872746c9f0e8e1ae048020efefa5a2c627f9b090a2a1468130df4e` |
| `Przejęcie pracy i synchronizacja (1).mht` | 3,506,793 | `8236bc65981b260c1708a9017f50f9c8bb7cae12a91f0b4eeef3ce83a5dc6e91` |

Both contained two HTML parts. Each had zero literal occurrences of
`semantic_sketch`, `compile_sketch`, and `SEMANTIC_SKETCH_PROTOCOL` across all
decoded parts. The first copy's visible conversation contained no `<pre>` code
blocks. Saved-file name searches for the missing module/protocol and a bounded
workspace filename search did not locate the original source.

## Result and boundary

No original source, test module, final commit, or final source hash was recovered.
These particular conversation captures do not resolve the gap. This does not
establish absence from every historical conversation, archive, or private copy.
The earlier recovery specification remains in
[the continuity audit](CONTINUITY-2026-09-30-old-thread.md); its reported 20 tests
and 48-mutation exercise remain historical, non-reproducible claims here.

Raw conversation captures and extracted private text are not included in the
public repository. No paid model calls or replacement implementation were made
for this check. Existing GPT/Jev results remain preserved and were not rerun.
