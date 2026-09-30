# W6 — independent evidence package

Evaluated native production baseline:
`b118c80e981c08ec6d7f9ab6aacc177979186cf2`.
This lane adds independent instruments, fixtures, reviews and receipts only.
No production fixes, integration merges, STATE edits or paid model calls.

| Arm | Fresh evidence | Interpretation |
|---|---|---|
| Real OpenAI shard | 100/100 exact conversations; 730/730 raw messages; 9,020/9,020 typed atoms; source bytes equal | Availability-selected 1.3 MB slice only; no asset bytes, semantic quality or whole-export claim |
| Synthetic import V1 | 47/59 named diagnostics, preserved unchanged | Includes two overly strict standalone member-blob checks; all 7 ZIP members recover from preserved source |
| Amended synthetic import V2 | 12/12 oracle controls; 9/9 raw-message objects; 2/3 exact reconstructed conversations | Anthropic original array order still fails; 23/24 total is not a quality percentage |
| Native transport | 72/72 checks, 8 chat attempts, 6 observed loopback requests | Source→selected context→captured request; no model comprehension claim |
| Trace persistence | 6/6 traces after reopen and 6/6 after reindex | Includes one compiled-but-unsent missing-key trace, proving dispatch distinction |
| Real Anthropic | Not evaluated | Raw Drive fetch timed out HTTP 504; existing 206 MB source located |
| Semantic fidelity | Six frozen adversarial dialogues with expected outcomes | Ready for W1/W3 evaluation; no model/ActiveTaskSpec result yet |

## Findings for integrator / production file owners

1. Anthropic importer reorders `chat_messages` for traversal without recording
   original array index in structured metadata. Source bytes remain intact;
   reconstructing the exact original array needs reopening that source.
2. `record_provenance` assigns traversal ordinals; these can identify a different
   Anthropic message and lack an exact OpenAI mapping key. OpenAI export metadata
   can supply the key in a join, so do not claim all provenance is unrecoverable.
3. Unknown JSON wrapper fields survive only in raw source, not structured
   conversation metadata. Conversation leaf counters do not validate wrappers.

Detailed reproductions, file owners and exact measured limitations:
[import_README.md](import_README.md), [import_v2_report.md](import_v2_report.md),
[real-export-README.md](real-export-README.md),
[transport_report.md](transport_report.md).

The evaluators were reviewed independently; two synthetic oracle holes and a
real-runner pointer strictness/output-overwrite weakness were found and addressed
in separate versions. All first results/instruments remain available. Reviews:
[review.md](review.md), [import_transport_review.md](import_transport_review.md),
[transport_review_real_export.md](transport_review_real_export.md).

[threat-supplement.md](threat-supplement.md) adds code-grounded T12 boundaries.
[transport_semantic_cases.json](transport_semantic_cases.json) supplies exception,
rejection, reversal, quoted-other-author, contradiction and missing-premise cases.
These are now exposed validation examples, not protected holdouts.

## Reproduction

Only standard-library Python and the recorded native build are needed. Exact
commands/hashes are in each arm's protocol/report. For text-only distribution,
restore the two immutable synthetic ZIPs before evaluating either import runner:

```bash
python3 docs/research/w6_evidence_2026-09-30/import_decode_fixtures.py
```

The decoder verifies the original frozen manifest and refuses to replace changed
bytes. Then run the arm-specific commands into new directories. The source and
runtime of the real-data arm are private; the public files contain aggregate
hashes/counts only. Synthetic payloads and model dummy replies are clearly named.

Integrate only this lane's new paths. Preserve this evidence when assigning
production fixes. Retest each fix against the frozen cases and independent
additional cases; do not replace the first failure or weaken its expectation.
