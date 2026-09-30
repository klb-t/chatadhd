# Direct lookup research handoff — 2026-09-30

All owned implementation/policy/protocol/test files and first outputs below are
finished. No files listed here remain under active edits. Parent owns index,
STATE, global log and commits; this agent made no commits and no API calls.
All three method freezes precede their first predictions and any sealed
validation release. Do not tune these methods after validation results.

## Completed increments and measured state

| Increment | Correct / planned | Available | New gains / losses | First result |
|---|---:|---:|---:|---|
| Full-conversation GPT extraction → original direct lookup | 88/96 | 96/96 | Initial arm | direct_lookup_v1/first_score.json |
| Same responses → turn-ID source binding → same lookup | 92/96 | 96/96 | 4 / 0 versus 88 | direct_binding_ablation_v1/first_score.json |
| Same bound assertions → individual-source event view → same lookup | 95/96 | 96/96 | 3 / 0 versus 92 | source_commitment_projection_v1/first_score.json |

All arms are DEV and **retrospective**: extraction saw the full conversation,
raw judges physically saw per-query prefixes. These results are not a causal
model ranking. Oracle graph lookup 96/96 is a separate mechanism control, not
model quality. The 95/96 arm retains all nine raw status events, withholds three
only in the source-commitment view, and leaves strict extraction event 0 TP /
9 FP / 6 FN unchanged. Remaining 006_q2 is nonendorsement misread as negation.
No world content truth, prefix-only extraction or temporal/cross-source
independent generalization is verified.

Primary frozen method files:

- loom/tools/structure/new_graph_direct_lookup.py
- loom/tools/structure/graph_direct_lookup_v1/policy.json
- loom/tools/structure/test_new_graph_direct_lookup_driver.py
- loom/tools/structure/test_new_graph_direct_lookup.py (independent reviewer-owned)
- docs/research/graph_method_panel_v1/DIRECT_LOOKUP_PROTOCOL.md
- docs/research/graph_method_panel_v1/DIRECT_LOOKUP_FIRST_MECHANISM_RESULTS.json (independent reviewer-owned first failures and superseding success)
- docs/research/graph_method_panel_v1/DIRECT_LOOKUP_DRIVER_FIRST_MECHANISM_RESULTS.json
- docs/research/graph_method_panel_v1/DIRECT_LOOKUP_RESULTS.md
- docs/research/graph_method_panel_v1/direct_lookup_v1/ (five immutable JSON outputs, including paired raw-judge recount and actual model response availability)

Citation-only ablation files:

- loom/tools/structure/graph_direct_binding_ablation.py
- loom/tools/structure/test_graph_direct_binding_ablation.py
- docs/research/graph_method_panel_v1/DIRECT_BINDING_ABLATION_PROTOCOL.md
- docs/research/graph_method_panel_v1/DIRECT_BINDING_ABLATION_RESULTS.md
- docs/research/graph_method_panel_v1/direct_binding_ablation_v1/ (first freeze, predictions, score, integrity)

Individual-source commitment projection files:

- loom/tools/structure/graph_source_commitment_projection.py
- loom/tools/structure/test_graph_source_commitment_projection.py
- loom/tools/structure/graph_source_commitment_projection_v1/policy.json
- docs/research/graph_method_panel_v1/SOURCE_COMMITMENT_PROJECTION_PROTOCOL.md
- docs/research/graph_method_panel_v1/SOURCE_COMMITMENT_PROJECTION_RESULTS.md
- docs/research/graph_method_panel_v1/source_commitment_projection_v1/ (first freeze, predictions, score, integrity, full regression log and regression.json)
- docs/research/graph_method_panel_v1/DIRECT_REPLAY_INTEGRITY.json
- docs/research/graph_method_panel_v1/DIRECT_LOOKUP_HANDOFF.md

Dependencies include the existing raw extraction first graph, fixed DEV inputs
and scorer, plus independently frozen turn_reference_binding_v1 artifacts/code.
Do not add Python cache directories. Frozen nested manifests check exact hashes
of original direct code/policy/protocol/tests, binder receipts and raw responses.

## Verification and limitations

Before first predictions: original relevant suite 67/67, citation ablation
73/73, source projection 86/86. Final full structure regression **664/664**,
13.852 s, using writable /var/tmp. Full log and its SHA are preserved in source
projection directory. First broad sandbox run 600/612 with twelve credential
fixture setup errors was preserved in citation integrity; writable /var/tmp
superseding run 612/612 passed without code changes. These tests use fake keys;
no real private file was read. Existing contract suite was not rerun by this
agent; parent previously reported 153/153 after declared dependency install.

Independent counting algorithms recounted precision/recall counts and exact
source witness paths: original 57, binding 58, source projection 61. All three
96-row runs replayed exactly in decisions and provenance after excluding only
new computation timestamps. All first output files remain untouched. First
mechanism failures are retained with superseding passing evidence. Independent
artifact review could not be triggered after thread limit; reviewer had already
passed all 31 direct mechanism tests. Post-result source recount is independent
of the primary scorer, but authored by the implementation agent.

## Best next step

Parent-owned source-only extraction arm is assigned to t6_fixture; do not
implement a duplicate. Preserve the methods before sealed validation and report
its covered family scope honestly. A future separately frozen causal-prefix
extraction test is needed to compare the composed graph pipeline with raw
prefix judges. Keep original event view and individual-source commitment as
named competing policies; external authority/identity resolution remains open.
