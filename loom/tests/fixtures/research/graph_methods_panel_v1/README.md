# Independent source-to-graph methods panel

Read `PROTOCOL.md` first. `inputs_dev.json` and `gold_dev.json` may be used for
development. **Do not inspect inputs_validation.json or gold_validation.json**
until the method/scorer/recipe/configuration freeze and explicit release.
`manifest.json` checks integrity without disclosing validation text.

Input schema:

- `cases[].id`, `language`, `turns` (turn ID, speaker, known_at, unchanged text).
- `node_inventory`: supplied proposition IDs and labels for the assisted track.
  These are experiment inputs, not source truth claims.
- `judgment_queries`: candidate directed relation, attributed speaker,
  `as_of`, and `scope` (`explicit_source` or `formal_implication`). No labels.
- Only declared scored assertion records are the graph target; this is not
  an oracle for every possible fact or entity implied by a conversation.

Gold schema:

- `cases[].family` occurs only in gold, for independent error analysis.
- `source_assertions`: typed directed assertion/denial records, source speaker
  attribution, known_at, `content_truth: unverified`, and exact evidence.
- `status_events`: append-only withdrawals/supersessions with known_at and
  evidence; earlier source assertions stay in the graph.
- `judgments`: query ID, supported/refuted/unknown label, basis class and
  supporting assertion IDs; unknown is not a negative relation.
- `formal_paths`: inferred conclusions with minimal alternative support paths;
  these never become explicit direct source assertions.

Evidence uses half-open Unicode-codepoint and UTF-8 byte spans **within the
identified turn text**; `coordinate_space` is `turn.text`. Source and turn IDs
are required to distinguish identical sentences across speakers/times. Full
turn spans deliberately provide sufficient provenance for this bounded panel;
subclause span quality can be evaluated separately by a frozen adapter.

Prompts/recipes are in `recipes.json` and `prompts_*.txt`. They execute nothing
and grant no spending authorization. Mechanical integrity checks are in
`test_fixture_mechanical.py`; run them with unittest by file or discovery.
Their pass count verifies the authored file contract, not any model capability.
