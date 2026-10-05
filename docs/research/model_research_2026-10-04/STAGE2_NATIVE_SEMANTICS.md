# Stage 2 — native semantic prompt/parameter preparation

This stage is prepared, not run. All model calls and actual costs are zero.
The latest owner sequence authorizes a separate 5 EUR OpenRouter programme;
the old dedicated $2 programme/key is not an execution input. Within that new
programme no additional owner reconfirmation is required except the configured
expected ×10 usage-growth rule. A current stage quote and the separate key's
identity, usage, liability and non-resetting limit are still execution prerequisites.

## Actual W1 API and boundaries

The inspected W1 branch `origin/gpt/knowledge-precision-2026-10-04` was
`a042ab7b45d9cc908154cf0252fbae4bca451d3c`, with accepted implementation
`ee5c74a80ec067cc770334d1a24cece5d398cdf4`. Its precision report preserves
the matched baseline `9d15d2dd0733274356f768816e207e26e13845e4` and unchanged
synthetic scorecard. The inspected semantic source/header did not differ
between W1 and current main. Precision gains there are deterministic code/data
filtering, version binding and paradigm inference; they are not model-quality gains.

The actual model entrypoint is `loom::extract::propose_semantics(StageContext,
observations, entities, claims)`. `semantic_fingerprint` binds runtime configuration;
`validate_candidate_graph_bundle` is the pure native validator. Runtime options
are `KnowledgeConfig.llm = auto`, `semantic_model`, `base_url`, and
`stage_params.extract.semantic`, including `representation` and eight integer
resource settings. `relation_v1` is the default; `occurrence_graph_v1` is the
experimental source occurrence representation used by this study. Its response
envelope is `schema_version=2`, exact `packet_hash`, and separate
`loom.candidate_graph/1` bundles grounded in `loom.source_packet/1`.

The native source still hardcodes temperature 0 and the two system prompts.
It has no caller-supplied prompt/recipe/decoding-options registry. The current
GraphPacket profile-store operation described in `docs/APPLICATION_PROFILES.md`
can retain explicitly accepted profile sources and receipts; it does not install
a new semantic method or bind a research manifest to production extraction.

Needed changes in W1's scope, reported rather than edited here:

1. Bind a versioned recipe/prompt definition and caller-supplied generation
   parameters from data into the semantic request. Preserve current prompts as
   presets, and validate declared provider capabilities.
2. Include the exact recipe version/hash, prompt bytes, generation parameters,
   provider/model identity and source-packet hash in fingerprint, cache identity,
   write-ahead checkpoint and candidate provenance. A recipe change must not
   reuse another recipe's response.
3. Expose source-only versus extracted-prior context selection as an explicit
   policy. This pilot supplies no prior entities/claims; current native selection
   can supply locally supported prior records. They are distinct context factors.
4. Add native test adapters for caller-selected prompts/parameters and checked
   CandidateGraph responses. Schema/source-span validity must remain separate
   from semantic correctness and promotion authority.

Production chat/provider selection, GraphPacket acceptance and any future UI
capability controls belong to their respective owners. Merely saving method
entities to the graph does not implement those adapters or renderer operations.

## Real synthetic DEV population

This primary source sample comes from the actual W1 scorecard's
`loom/tests/fixtures/eval/synthetic_dev/chatgpt_export.zip` and
`claude_export.zip`: 34 and 31 fictional conversations. It is distinct from the
16-case independent candidate-validator DEV fixture. It is also distinct from
W1's 1,164-file public-repository selfhost benchmark.

The configurable preset selects **15 conversation branches**, eight ChatGPT
and seven Claude, covering project changes, versions, corrections, film/music,
legal statements and code/name/domain distractors. Selection IDs and its rationale
are data in [`stage2/config.json`](stage2/config.json), not fixed quotas.
[`stage2/inputs.synthetic_dev.json`](stage2/inputs.synthetic_dev.json) retains
exact text parts on each source-current branch, archive/member hashes, source
JSON pointers, speaker and ordered node IDs. Missing byte offsets stay null;
they are not fabricated by searching repeated text. Claude's source array order
is retained when no explicit parent links exist. A parent-linked fork follows
only the exported current leaf's ancestor path; its alternative remains in the
original source archive, outside that packet.

This is a declared text projection: nontext metadata and attachments are not
model input. Original archives remain unchanged and hash-bound. Prior graph
records are empty. The underlying synthetic corpus and this selected sample are
inspected DEV; neither is unseen or a sealed holdout. Preparation reads only
the source ZIPs and separately saved source-only inputs, not `ground_truth.json`.

The existing synthetic answer key scores projects, versions, principles and
decisions across the whole pipeline. It is **not** occurrence-graph gold for
operation ports, binders, scopes and located abstentions. The stage's semantic
reference status remains `pending_preregistered_annotation_for_occurrence_graph_on_actual_synthetic_dev`.
Freeze that separate source-interpretation rubric and reference annotation
before model collection. Independently review unsupported text, quotation,
negation, ordered ports and scope; keep native's five-operation capability
boundary explicit. A parseable/grounded packet alone is not a semantic accuracy
result or a measured improvement over W1's end-to-end scorecard. The independent
16-case corpus can be a separately labelled optional mechanical control, never
silently substituted for this source population.

## Data-defined arms and costs

[`stage2/recipes.json`](stage2/recipes.json) contains exact native `kGraphPrompt`
bytes with their source-file/symbol hash, and a second version appending generic
structural scope guidance. It does not include case-specific answers. Both run
at temperatures 0 and 0.3, with the same source packets, model/provider,
`top_p=1` and 1,600-output-token allowance. Those are editable experiment presets.
The four versioned methods produce **60 exact request records** in
[`stage2/prepared/prepared.json`](stage2/prepared/prepared.json), frozen before
responses. Model, prompts, source selection, parameter variants and allowances
all live in data; the preparation tool contains no new system-prompt string.

The historical declared reservation is **$0.3852768** using the exact saved
October 2 OpenAI endpoint evidence and caps from the existing study. The byte
surrogate is `(canonical UTF-8 request bytes + 1024 + 32 × message count)` for
input allowance, plus the configured completion allowance; it is not tokenized
usage or a guaranteed bill. [`stage2/budget_preview.json`](stage2/budget_preview.json)
retains per-arm estimates, actual cost zero and `current_quote_status: unquoted`.
New dispatch remains blocked pending a fresh quote and separate-key gate. No
EUR/USD parity or current currency conversion is fabricated.

`materialize_manifest` is a pure conversion to the existing runner format after
the coordinator supplies an explicitly verified USD cap and fresh saved price
evidence. It performs no account lookup or inference; its caller owns the 5 EUR
programme gate. Old frozen 432-request files are untouched. Stage 1 runs first;
stage 2's quote and allocation are evaluated against the remaining new-programme
budget afterwards. Missing/invalid/uncertain first attempts retain their slots,
cost and reservation; they are never replaced by a preferred retry.

[`stage2/jev_candidate_consistency_recipe.json`](stage2/jev_candidate_consistency_recipe.json)
is a separate, unmeasured dependent recipe: it requires a retained candidate
from an actual extraction response. It checks supplied-candidate source
consistency, not free extraction or world truth. Reference/gold graphs must
never be substituted as Jev inputs and labelled model extraction. It has no
default additional paid batch or automatic promotion.

## Reproduce and verify offline

```bash
python3 -m loom.tools.structure.stage2_semantic_variants_v1 verify \
  --prepared docs/research/model_research_2026-10-04/stage2/prepared
python3 -m unittest loom.tools.structure.test_stage2_semantic_variants_v1 -v
```

To recreate inputs and preparation, point `project-inputs --config ... --output
<new-file>` and `prepare --config ... --output <new-directory>` at these same
source/data definitions. Outputs are created exclusively. The first source
projection failure was retained in `FIRST_PROJECTION_FAILURE.json`: the Claude
export uses an empty parent string as its root sentinel. The corrected projection
preserves the original bytes and current branch; it does not alter the archive.

Six source/mechanism regressions pass: exact rebuild without gold/network,
every text pointer matching source bytes, fork isolation, Python contract
preflight, controlled 15×4 request inventory and explicit EUR-budget/unknown
current quote boundaries. Python preflight passes all 15 source packets;
native runtime wiring and live model semantics remain unmeasured. Methods,
recipes, parameter sets and any future results are hash/version-bound graph
entities through the separately owned method-export tool.
