# Secondary raw-output/source audit

This audit is **post-outcome diagnostic**, separate from the frozen primary
matcher/compiler. Batch01 first outcomes were already observed when this audit
protocol was recorded. Batch02 model content has not yet been inspected by this
auditor. The auditor authored the fixture and is not blind to its source/gold
design. No new model calls, primary scoring changes, repair of saved model output,
node-alignment fitting or validation reads occur. A further independent reviewer
may check these source judgments. Do not represent this as another blind quality
experiment or promote a manual diagnostic to primary model score.

Preserve every attempted response/first compiled record and source hash. Distinguish:

1. Provider transport/API/finish failures, including paid output truncation.
2. JSON contract failures: duplicate object fields, arrays/types/keys, unknown
   predicates, unknown own IDs, malformed evidence representations.
3. Source-semantic defects: incorrect predicate/direction/actor/polarity,
   nonendorsement or silence converted to edge denial/assertion, correction scope,
   stale/current ambiguity and unsupported inference.
4. Strict surface-alignment misses versus legitimate source expressions outside
   the bounded reference-atom inventory.

All raw discovered node records receive one source-review category:
`used_reference_atom`, `unused_reference_control`, `legitimate_novel_reference_paraphrase`,
`source_supported_expression_outside_reference`, `unsupported_or_changed_meaning`,
or `needs_review`. The strict normalized mapping and manual category remain separate
columns. A full conditional, reporting sentence, denial statement or nonendorsement
may be a legitimate discourse/expression node even though it is not a reference
operand. Such an extra cannot be called a hallucination from nonalignment alone.
Duplicate node IDs, malformed nodes and ambiguous source identity remain diagnosed.

Each raw source assertion/event receives `source_valid`, `source_invalid` or
`needs_review`. Inspect exact source turn text, known_at, actor, own-node endpoints
and typed predicate/polarity. Existing supplied-node gold is a bounded reference,
not proof that every additional expression node or discourse relation is false.
For an actual typed content edge, however, nonendorsement/silence does not deny it;
negative propositions do not change the polarity/predicate of an affirmative
implication; quoted-person assertions are not reporter endorsements.

For source review only, a string evidence ID can identify an existing immutable
raw turn. It still violates the frozen required object shape and **remains a
primary compiler FP**; this audit does not repair/recompile it as a primary success.
Likewise duplicate JSON fields are retained with their competing values: do not
silently choose the last source endpoint as the model's intended graph. Such
records stay unavailable/ambiguous in primary and `needs_review` for source
endpoint interpretation. A truncated response does not yield a complete graph;
do not remove it from planned denominators or hallucinate its missing suffix.

Review complete original raw response and actual source, not merely a predicted
gold match. Known_at/citation/quote existence is mechanical provenance, not source
clause adequacy or content truth. Manual semantic categories may describe why
strict quality is a lower bound but must not conceal model contract failures.
Report all reviewed/missing/ambiguous record denominators, case IDs and audit
limitations. Keep first strict scores and semantic diagnostics separate.
