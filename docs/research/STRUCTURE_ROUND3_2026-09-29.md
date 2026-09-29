# T6 — explicit source envelopes, round 3

Measured on 2026-09-30; base `5816b557`. [P] Optional research channel;
[H] wider source-envelope coverage can supply useful grounded candidates.
**Keep experimental, investigate scope and transfer.** This is not a native
semantic parser or proof of broad meaning extraction.

## Protocol and first results

An isolated agent authored/froze 96 new bilingual cases before seeing parser,
source-document benchmarks or outputs. Development has 32 positive/32 abstain
cases; sealed validation has 16/16. Eight relation families are balanced by
language and label. Exact directed operands, Unicode/UTF-8 spans, operation,
cue and qualifiers are independently labelled. See fixture PLAN/manifest.

The optional `explicit_relations` channel loads patterns/guards as data. It
keeps all source bytes, reversible Markdown/linewrap detection maps, opaque
operands, provenance and supplied `known_at`. Every formula abstains, confidence
and calibration are unavailable, and zero canonical claims/mutations occur.
`bounded` stays the default and its outputs matched HEAD on **66/66** checked
records. Other channels are not vetoed by this channel's assertion-scope policy.

| Run | Strict TP / predictions | Strict TP / gold | FP / FN | Family-only TP / predictions; TP / gold |
|---|---:|---:|---:|---:|
| Development, bounded | 0/4 | 0/32 | 4 / 32 | 3/4; 3/32 |
| Development, first new parser | 31/31 | 31/32 | 0 / 1 | 31/31; 31/32 |
| Development, final new parser | 32/32 | 32/32 | 0 / 0 | 32/32; 32/32 |
| Validation, bounded | 0/0 (precision unavailable) | 0/16 | 0 / 16 | 0/0; 0/16 |
| Validation, frozen new parser | 5/5 | 5/16 | 0 / 11 | 5/5; 5/16 |

First output bytes and exact first parser/policy/tests are preserved in
`structure_round3_v1/first_parser_snapshot/`. Its manifest binds original
implementation hashes; first-dev scoring was replayed with the later scorer,
so report-level scorer hashes are not a claim that the first parser used that
later implementation. Subsequent mechanism fixes addressed quotation leakage,
Markdown identifiers, competing purpose patterns and empty-input CLI version.

The scorer was independently audited before any validation access. Reproductions
exposed unchecked cue quotes, bool/float offsets and same-invocation freeze/release;
these were fixed **before** FREEZE2 and validation. FREEZE is preserved as
superseded. FREEZE2 binds parser, policy, scorer and fixture manifest. Baseline
and final validation were each run once after release; neither labels nor
parser were subsequently edited. Independent direct recount agrees exactly.
Scorer tests are mechanism tests, not 33 extra measured extraction cases.

The validation gap is material: cause transfers 1/2, generalization and
conjunction/alternative 2/2 each; five other families 0/2. Perfect development
coverage did not transfer. No tuning on this validation result is claimed.

## Current repository-source diagnostic (different snapshots from 2026-09-28)

Three current documents contain **1,186** raw nonempty physical units, not the
historical 877. Current source hashes and exact candidate spans are saved in
`structure_round3_v1/source_first.json`. The two channels segment differently;
their unit/character denominators must not be directly equated.

| Source | Bounded envelopes / raw physical units | Optional envelopes / proposed units |
|---|---:|---:|
| Owner requirements | 0/377 | 1/211 |
| GPT note | 0/321 | 3/335 |
| Conceptual model | 0/488 | 2/248 |
| Total | 0/1,186 | 6/794 |

Independent source review: **6/6 exact raw provenance spans**, but only **2/6
valid envelope scopes**, **3/6 invalid operand scopes**, **1/6 needs review**.
Nominal/prepositional/verb-phrase coordination can be split at the wrong
hierarchy; exact spans do not establish correct thought structure. These are
candidate audits, not natural-source recall or semantic truth labels. The
historical reports remain untouched; the default research pipeline still finds
zero on the current sources, because the optional channel is not wired into it.

## Reproduce and next decision

From repo root (choose fresh output filenames; runners refuse overwrite):

```sh
python -m unittest discover -s loom/tools/structure -p test_relation_envelopes.py -v
python -m unittest discover -s loom/tools/structure -p test_round3_eval_independent.py -v
python loom/tools/structure/round3_eval.py --policy explicit_relations --output /tmp/structure-dev-new.json
python loom/tools/structure/round3_sources.py --output /tmp/structure-sources-new.json
```

Validation release/reuse commands are in the independent report. Re-running
after inspecting its outputs is reuse, not a new blind evaluation. Keep this
channel optional; next work needs richer operand-scope representation and new
independent data, or separately budgeted source-to-structure models. Do not
promote the six source matches or the development 32/32 to model reliability.
No model/API/Actions/device calls were made; native integration is unverified.
