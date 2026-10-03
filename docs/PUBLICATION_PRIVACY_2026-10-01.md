# Publication privacy triage — 2026-10-01

**All 37 flagged paths were resolved: no private-content or credential finding
was confirmed in this flagged set.** The initial scanner used an unbounded
case-insensitive substring for an insurer name; this matched ordinary English
words. It also searched compressed bytes as if they were UTF-8 text.

This report addresses the publication-review rejection caused by those unresolved
flags. It reports categories and paths only; it contains no credentials, private
passages, contact-address values or source-conversation excerpts. Audited source
checkout at report time: `d7912498ce79feb2b61326cb7cb343df15cf33df`.

| Classification | Flagged paths |
|---|---:|
| Generic-word false positive (`fracture`, `manufacture`, `manufactured`) | 27 |
| ZIP compressed-byte coincidence, absent after recursive decoding | 7 |
| MHTML base64 coincidence, absent in decoded HTML | 1 |
| PNG compressed pixel-data coincidence in IDAT | 1 |
| Public upstream maintainer contact in bundled license header | 1 |
| Confirmed private-content finding in these flags | **0** |

The initial credential-signature scan reported zero matches. That is a scoped
scan result, not a mathematical proof that arbitrary credentials cannot exist.
This triage does not open the sealed holdout or authorize publishing private
original exports. Existing public source refs are preserved; private source files
outside Git are not inputs to this handoff.

## Fixture and archive provenance

- `loom/tools/gen_synthetic_eval_corpus.py` constructs fixture content from
  committed literals using standard-library `calendar`, `datetime`, `json`,
  `pathlib` and `zipfile`. AST inspection found no source-data read or network
  acquisition operation. Its eight initial flags are the generic word
  `fracture`; none is an insurer name, person identifier or private medical
  marker. The file explicitly describes its synthetic/no-real-data provenance.
- `loom/tests/fixtures/eval/synthetic_dev/chatgpt_export.zip`,
  `claude_export.zip`, and `ground_truth.json` have respectively 3, 1 and 7
  occurrences of that same generic synthetic word. They have no whole-token
  institution match or scanned owner/medical identifier. Their file names
  describe test import formats, not private account-export provenance.
- `docs/research/inputs/openrouter-native-dev-2026-09-28.zip` has 15 decoded
  members with zero original personal-marker matches. Its `inputs.json` is
  structurally **exactly equal** to the 16-record committed fixture
  `loom/tests/fixtures/eval/live_structure_pilot_v1/inputs.dev.json`. It is saved
  synthetic DEV request/response evidence, not a private source-chat archive.
  The original hit was compressed-byte coincidence.
- Decompressed seeding result files contain repeated copies of the synthetic
  generic word; no different decoded marker was found. They remain historical
  result artifacts and can be archived for curation reasons independent of
  this privacy classification.
- `loom/tests/fixtures/import/chat.mht` contains one base64 MIME text/html
  part. The raw encoding matches a three-letter marker; the decoded payload
  does not. `loom/web/e2e/screenshots/desktop-05-graph.png` matches inside the
  compressed IDAT chunk and has no tEXt, zTXt or iTXt metadata chunks.
- `loom/third_party/miniz/miniz.h` carries a public upstream maintainer email
  in the license/copyright material. Preserve upstream attribution; it is not
  a user secret. The sqlite source match is the word `manufactured`.

## Path-by-path disposition

| Flagged path | Category | Evidence |
|---|---|---|
| `docs/coordination/receipts/W1-2026-09-30-verification/full-details.log` | Generic word substring | `fracture` |
| `docs/coordination/receipts/W1-2026-10-01-http-verification/full-details.log` | Generic word substring | `fracture` |
| `docs/research/GRAPH_NATIVE_RESULTS_2026-09-28.md` | Generic word substring | `manufacture` |
| `docs/research/STRUCTURE_ROUND3_INDEPENDENT_2026-09-30.md` | Generic word substring | `manufactured` |
| `docs/research/agentic_graph_cache_v1/raw_measurements_v1.zip` | Compressed-byte coincidence | 7 recursively decoded members; zero decoded matches |
| `docs/research/analysis_plan_independent_audit_v1/MECHANISM_EVIDENCE.zip` | Compressed-byte coincidence | 471 recursively decoded members; zero decoded matches |
| `docs/research/continuation_2026-09-30/verification/current-ctest-details.log` | Generic word substring | `fracture` |
| `docs/research/graph_method_panel_v1/first_evidence.zip` | Compressed-byte coincidence | 301 recursively decoded members; zero decoded matches |
| `docs/research/inputs/catalog-recall-diagnostic-2026-09-28.log` | Generic word substring | `fracture` |
| `docs/research/inputs/local-native-verification-2026-09-28.zip!native-local-full-LastTest.log` | Generic word substring | `fracture` |
| `docs/research/inputs/openrouter-native-dev-2026-09-28.zip` | Compressed-byte coincidence | 15 recursively decoded members; zero decoded matches |
| `docs/research/integration_2026-10-01/verification/ctest-full-output.log` | Generic word substring | `fracture` |
| `docs/research/philosophy_watch_2026-09-30/PROTOCOL_01.md` | Generic word substring | `manufacture` |
| `docs/research/philosophy_watch_2026-09-30/RESEARCH_DIRECTIONS_01.md` | Generic word substring | `manufacture` |
| `docs/research/w2_retrieval_2026-09-30/verification/targeted-corrected.log` | Generic word substring | `fracture` |
| `docs/research/w2_retrieval_2026-09-30/verification/targeted-first.log` | Generic word substring | `fracture` |
| `loom/tests/fixtures/eval/synthetic_dev/chatgpt_export.zip!conversations.json` | Generic word substring | `fracture` |
| `loom/tests/fixtures/eval/synthetic_dev/claude_export.zip!conversations.json` | Generic word substring | `fracture` |
| `loom/tests/fixtures/eval/synthetic_dev/ground_truth.json` | Generic word substring | `fracture` |
| `loom/tests/fixtures/exports/anthropic_2026_full.zip` | Compressed-byte coincidence | 4 recursively decoded members; zero decoded matches |
| `loom/tests/fixtures/import/chat.mht` | Base64 coincidence | Decoded text/html MIME part: zero matches |
| `loom/third_party/miniz/miniz.h` | Public upstream contact | Maintainer email in bundled upstream license/copyright header; not an owner credential |
| `loom/third_party/sqlite/sqlite3.c` | Generic word substring | `manufactured` |
| `loom/tools/gen_synthetic_eval_corpus.py` | Generic word substring | `fracture` |
| `loom/tools/seeding/results/synthetic_lopo_v1_first/predictions.json.gz` | Generic word substring | `fracture` |
| `loom/tools/seeding/results/synthetic_lopo_v2_first/predictions.json.gz` | Generic word substring | `fracture` |
| `loom/tools/seeding/results/synthetic_lopo_v2_first/results.json.gz` | Generic word substring | `fracture` |
| `loom/tools/seeding/results/synthetic_lopo_v2_metadata_corrected/predictions.json.gz` | Generic word substring | `fracture` |
| `loom/tools/seeding/results/synthetic_lopo_v2_metadata_corrected/results.json.gz` | Generic word substring | `fracture` |
| `loom/tools/seeding/results/synthetic_lopo_v3_ranking_control_first/predictions.json.gz` | Generic word substring | `fracture` |
| `loom/tools/seeding/results/synthetic_lopo_v4_application_alternatives_first/predictions.json.gz` | Generic word substring | `fracture` |
| `loom/tools/structure/GRAPH_PATTERNS.md` | Generic word substring | `manufacture` |
| `loom/tools/structure/graph_composed_validation_v1/native_baseline/catalog_metric_first.log` | Generic word substring | `fracture` |
| `loom/tools/structure/method_panel_v1/PROTOCOL.md` | Generic word substring | `manufactured` |
| `loom/tools/structure/retrieval_exploration_v1/cross_encoder_v1/first_evidence.zip` | Compressed-byte coincidence | 4 recursively decoded members; zero decoded matches |
| `loom/tools/structure/retrieval_exploration_v1/first_evidence.zip` | Compressed-byte coincidence | 7 recursively decoded members; zero decoded matches |
| `loom/web/e2e/screenshots/desktop-05-graph.png` | Compressed pixel-data coincidence | Match is inside PNG IDAT; no text metadata chunks |

## Secret, database and export filename check

Tracked sensitive-looking filename review found exactly three template/fixture
paths, not live credential stores:

| Path | Category and evidence |
|---|---|
| `loom/deploy/.env.example` | Two-entry deployment configuration template; no populated suspicious secret value. |
| `loom/tests/fixtures/import/claude.db` | Synthetic SQLite import fixture named by `gen_import_fixtures.py`; two tables, five rows; zero scanned marker cells. |
| `loom/tests/fixtures/import/generic.db` | Synthetic SQLite import fixture named by `gen_import_fixtures.py`; one table, two rows; zero scanned marker cells. |

No tracked `secrets.json`, live `.env`, private-key file, or non-fixture SQLite
store was found by that filename check. Tracked export-like files under test
fixtures are deliberate parser/evaluation inputs; source generators and the
exact saved-DEV equality above explain the flagged archives. A path containing
“export” or “key” alone does not establish private contents.

The root `.gitignore` excludes `secrets.json`, `.env`, `.env.local`, `*.pem`,
`*.key`, local build directories and generated large-eval output. Ignore rules
are preventive defaults, not evidence about already tracked files; the checks
above inspect tracked files directly. No secret file, private original export,
archive content or history was modified during this audit.

## Method and limits

The complete flagged-path list came from the publication scan. For each ZIP/GZIP
path, decode recursively before matching; for MHTML, decode MIME transfer
encoding; for PNG, identify the containing chunk. For textual matches, classify
the enclosing word instead of treating a substring as a named entity. Scan SQLite
cells read-only, inspect fixture generator AST and compare saved DEV inputs with
the committed fixture. Retain old negatives and first responses in archived
source history; none of these false positives requires deleting or rewriting
that scientific record.

This resolves the specific 37 review flags and obvious tracked secret/database
filenames. It does not claim a new exhaustive semantic review of every historical
commit or OCR review of every screenshot. No additional private data should be
introduced in the promotion tree or newly created archives.
