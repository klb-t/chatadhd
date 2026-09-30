# Direct typed-graph lookup: DEV protocol

Preregistered before this method's predictions/results, after the known DEV GPT
extraction outcomes. This is a new outcome-informed development method, not
blind validation. No sealed validation, paid API, LLM or retrieval index is used.
The policy is data in `graph_direct_lookup_v1/policy.json`; code, input, compiled
graph and policy hashes are frozen before computing the 96 predictions.

## Inputs and information asymmetry

Use exactly the 24 DEV cases/96 explicit-source queries from `inputs_dev.json`
and preserved `extraction/first_score/compiled_first.json`. Do not read DEV gold
until predictions have been saved. Raw GPT/Jev judge reports may be compared
only after these predictions are frozen. They received physically truncated
prefixes per query. **GPT extraction saw the full conversation**, including
future turns, before its graph was compiled. Filtering graph records by source
`known_at` creates a **retrospective as_of projection**, not a causally
prefix-extracted pipeline. It does not prove future independence of extraction,
and its accuracy is not an information-equivalent causal comparison with raw
judges. A separate 96-prefix extraction experiment would be needed for that.

Assertion `known_at` is the supporting source turn's timestamp, not the time
the model-created graph claim became available. Preserve it literally with its
source semantics; record runtime computation time separately. Candidate model
claims never become observed world facts or canonical graph mutations here.

## Deterministic decision policy

Match exact typed predicate, directed source/target inventory IDs and attributed
speaker. Node IDs already carry operand meaning, including negated operands;
do not invert relation polarity from words inside those nodes. Keep assertions
known by the cutoff, with all original IDs/evidence/provenance and historical
records retained. Revalidate eligible quotes, exact Unicode/UTF-8 spans, source
identity, turn identity and source timestamp. This checks binding, not whether
the quote semantically justifies the extracted relation.

Apply **all** source-bound superseded events known by the cutoff to their old
assertion IDs. Event old time must precede event time; replacement time equals
event time. A replacement need not match the query's predicate/endpoints or
attribution. Cross-attribution replacement is applied as provided, with a
warning; lookup does not repair a model's invented event using gold or text
interpretation. Preserve all events for inspection, including unrelated ones.

Among remaining exact matches, select the latest timestamp. Latest positive =
supported, latest negative = refuted. Latest positives and negatives at the same
instant = conflicting/unavailable, without forcing a class. Multiple same-sign
assertions remain separate witness paths. None = unknown, even if a two-hop
path could imply the relation: this is explicit-source direct lookup. Missing
compiled cases/invalid bindings remain unavailable in planned denominators.
Superseding a negative can expose an older positive only if that older assertion
explicitly remains active. A withdrawal never creates a positive assertion.

The current compiled ABI represents supersession with a replacement assertion;
withdrawal-only events are unsupported and produce unavailable plus an explicit
warning. Do not pretend that this ABI measures withdrawal-only source judgments
or silently convert withdrawal into a negative edge. Neither graph contents,
raw model confidence nor a `content_truth` flag can establish real-world truth.

Responses preserve query/as_of, assertion IDs, original accepted records, evidence
turns/quotes/spans, source timestamps, applicable events, active/history IDs and
provenance paths. They are derived evaluation traces, not a separate signature
store or source of truth. No canonical source or graph is changed.

## Evaluation and mechanism controls

Only after saving predictions, join DEV labels and reuse the frozen primary
`graph_panel_live.score_judgments`: full 96-query confusion, per-class TP/FP/FN,
precision/recall denominators, coverage/unavailable and every failure. Preserve
per-family/language groups, simpler always-supported/refuted/unknown baselines,
and comparisons with raw judges while marking the information asymmetry.

Run the same direct lookup over the DEV oracle graph **separately** as a mechanism
control, after primary predictions. That is not extraction/model quality: it
checks behavior given authored graph records. Keep oracle results and errors
distinct from the actual GPT-extraction pipeline. Neither is blind validation.

Authored mechanism tests precede measurements: early cutoff versus correction,
repeated positives, attribution, negated operands, normalized same-instant
conflict, empty/direct-no-path unknown, no transitivity/contraposition, full
source-binding validation without semantic truth promotion, immutable history,
missing graph denominator, all-event application and unsupported withdrawal.
Preserve first outcomes and investigate errors without editing this frozen v1
policy, original graph, gold or comparator reports.
