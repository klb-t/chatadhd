# W3 independently authored directed commitment DEV cases

12 synthetic cases, 48 queries, 6 Polish and 6 English cases. Each language
contributes 24 queries. Six families have 2 cases / 8 queries each:
withdrawal direction; denial withdrawal and unrelated extension; other-speaker
isolation; quoted withdrawal attribution; negative antecedent identity;
negative consequent versus whole-relation denial.

Authored by a separate agent before inspecting the new W3 implementation.
This is adversarial DEV, not a blind or sealed holdout, and bilingual pairs
are correlated. No live model output, API use, private archive text, or quality
estimate is contained here. The existing source-view lifecycle schema and
policy were read as contracts; those existing cases were not copied.

`inputs_dev.json` contains only source text, node inventory and queries.
`gold_dev.json` contains source assertions, withdrawal events, expected
source views and a per-query rationale. Full-turn evidence deliberately
includes attribution context. Character offsets are Unicode code points;
byte offsets are UTF-8, both half-open.

Policy `source_view.latest_active_commitment/1` describes source commitment,
not objective truth. A withdrawn positive commitment is `refuted` for that
exact directed query; a withdrawn denial is `unknown`. Unknown reverse edges
stay unknown. All content truth remains `unverified`. Negated operand nodes
NA/NB are distinct propositions, not whole-relation polarity. Explicit-source
queries do not introduce contraposition or other logical consequences.

Coverage limits: no conflicts of simultaneous opposite assertions by one
speaker, nested quotation, paraphrase/coreference stress, temporal ties,
malformed output, or non-implication relations. These cases isolate the
specified mechanism; they do not establish general language performance.
