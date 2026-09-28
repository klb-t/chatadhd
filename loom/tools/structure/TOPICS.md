# Topic-local segmentation and graph context experiment

`topics.py` is an executable, offline, standard-library baseline. It addresses
a specific catalog/extraction failure: a conversation can change topic halfway
through a turn or introduce a project near its end. Later context must not
retroactively make an earlier ambiguous name an identity hit.

This is a read-only projection of source observations into proposed local topics
and graph context. It does not modify the production catalog, policy, graph,
claims or core schema. Its boundaries and context links are **candidates**;
lexical matching is not verified semantic identity. There is no invented
confidence score. Mechanism tests are separate from independent quality scores.

## APIs and input

```python
segment_conversation(turns, entities, policy=None)
project_context(segmentation, graph, claims=None)
analyze(record, policy=None)  # the two steps together
```

`record` accepts the following fields:

| Field | Shape |
|---|---|
| `id` | Optional conversation ID |
| `turns` | Ordered `{id, role, text}` records, unique turn IDs |
| `entities` | `{id, aliases}` records; aliases are literal strings or objects below |
| `graph` | Optional `{nodes, edges}` existing-graph snapshot |
| `claims` | Optional newly extracted `{id, text?, source:{turn_id,char_start,char_end}}` records |

An alias object has `text`, optional `requires_any` and `excludes` string arrays,
optional `min_context`, and optional `ambiguous`. A context-required or ambiguous
alias defaults to at least one positive cue. An ambiguous alias without a cue
list therefore abstains. Any matching negative cue blocks this occurrence.
Matching is case-insensitive and whole-token; `EP` does not match `kept`,
`EP_extra` or a Unicode-adjacent word. There is no morphological alias equivalence
in this experiment. Alias context cues are literal phrases, not hidden stems.

Graph nodes use `{id, kind?, role?, qualifiers?, claim_ids?}` and edges use
`{source,target,predicate,qualifiers?,claim_ids?}`, compatible with the structure
experiment. Entity IDs must equal existing node IDs to produce a direct graph
lookup. A missing graph node remains an unresolved subject candidate. Existing
node/edge assessments are not promoted, overridden or used as proof premises.

## What the baseline does

1. Split turns at sentence/semicolon/newline boundaries and punctuated explicit
   reset/return cues. Preserve every non-whitespace span as an exact substring,
   with character offsets, UTF-8 byte offsets and a hash of the complete turn.
2. Match aliases against each span. Positive and negative context are restricted
   to that span and the configured token radius. A later span cannot change an
   earlier mention decision. Rejected matches and their reasons remain visible.
3. Start a proposed segment on an explicit reset/return, changed explicit
   anchors, a rejected active alias, lexical novelty, or focus expiry. An
   explicit named return links to the most recent earlier segment bearing that
   anchor; it does not merge either segment or invent an unnamed antecedent.
4. Propose anchored continuity only when the new span shares sufficient content
   terms with the last supported local span. Keep the anchor and intervening
   supporting observation IDs. A bare `it`, `that`, `yes`, etc. records unresolved
   possible continuity, with **no assigned focus identity**. Unanchored spans
   cannot extend focus indefinitely.
5. Look up the observation's proposed subjects in a supplied graph and show
   their one-hop neighbors. Preserve edge direction, predicate, qualifiers and
   old claim IDs. Neighbors are contextual suggestions, never additional subject
   identities or eligible inference premises. The node budget exposes omissions.
6. Project each supplied new claim's source range onto these observations.
   Invalid source ranges are blocked. Claims crossing topic boundaries retain
   all applicable segments and require scope review. A completed segment's union
   of later anchors is **never** used to contextualize an earlier claim.
7. Propose exact-repeat view compaction while retaining all original observation
   IDs. This can simplify a display without deleting history or merging claims.

All cues and heuristic budgets live in `topics_policy.json`; overrides change
the recorded policy hash. They are transparent defaults, not learned or
calibrated thresholds. Production selection gates and weights are unchanged.

## Output contract

The segmentation includes `sources`, `observations` and `segments`. Each
observation includes:

- `text` and `source:{turn_id,char_start,char_end,byte_start,byte_end,sha256}`;
- local `mentions`, including accepted/rejected status, offsets **relative to the
  observation text**, positive/negative cues and bounded context offsets;
- `focus_entity_ids`, `focus_basis`, `reference_status`, and supporting
  `focus_evidence_observation_ids`;
- `possible_continuation_entity_ids`, which are unassigned alternatives;
- its proposed boundary reason, cue and segment ID.

| `focus_basis` | `reference_status` | Interpretation |
|---|---|---|
| `local_alias` | `explicit_local_alias` | Locally supported literal name occurrence; semantic identity unverified |
| `corroborated_continuity_candidate` | `candidate_continuation` | Own lexical evidence plus earlier anchor; inferred topic context |
| `unresolved_continuation` | `unresolved` | Discourse cue only; no assigned focus |
| `unanchored` | `no_reference` | No supported subject in this span |

`project_context` returns `contexts`, `update_proposals`, `blocked_claims` and
`mutations_applied: 0`. Proposal kinds are `context_link`, `new_topic_review`,
`claim_context_review` and `compact_repeated_observation_view`. Each records
`status: candidate`, `automatic_mutation: false`, `persistable_claim: false`
and `confidence: null`. These are report records, not new Loom Assessment types.
IDs are deterministic content hashes. Root source text and graph inputs are
never changed.

## Run and verification

```sh
python loom/tools/structure/topics.py /tmp/topic_input.json
python loom/tools/structure/topics.py /tmp/topic_input.json --policy /tmp/policy.json
python -m unittest discover -s loom/tools/structure -p test_topics.py -v
```

The author-owned suite has 19 mechanism checks: late-context nonretroactivity,
mid-turn reset, named return, corroborated continuity, ambiguous pronoun
abstention, bounded negative context, identifier collisions, Unicode offsets,
focus expiry, unnamed novelty, graph direction/provenance, cross-topic claim
scope, invalid locators, lossless view compaction, budgets and determinism.
The initial suite passes. This does not measure semantic segmentation quality
or production catalog recall. The independent evaluator owns its own texts and
labels; no production fixture terms or answer keys are imported by this module.

## Known limits and next comparisons

Sentence splitting is heuristic; abbreviations, quoted control phrases, code,
lists and unusual punctuation can be split poorly. Token equality misses
morphology, synonyms and cross-language paraphrases. Repeated generic words can
produce spurious continuity; lexical novelty can over-segment a coherent topic.
Short acknowledgements remain unassigned, and generic unnamed returns cannot
be resolved. An explicit later name does not retroactively repair earlier
references; a future backward-resolution layer would need a separately labeled
inference proposal and its evidence chain.

This is not a general pronoun resolver, claim extractor, temporal entity
resolver, contradiction detector or automatic graph simplifier. One-hop
neighbors can include stale, disputed or irrelevant claims; they are shown with
source IDs and scope for further assessment. Compaction is only exact wording,
not proof of semantic equivalence. New claims are only source-range projected;
their interpretation and truth are outside this module.

Compare this conservative baseline with entity-linked coreference, semantic
change-point detection, turn-role/argument structure and graph-constrained
continuity on independently authored cases. Keep boundary accuracy, reference
resolution, retrieval quality, provenance integrity and false identity separate.
Only then consider a C++ adapter or persistence of proposed graph changes.
