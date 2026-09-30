# Graph packet and compositional agent workflow — research proposal, 2026-09-30

The owner requested frontier methods, prompt/approach variants and agents as
first-class configurable methods, including graph completion, discovering unnamed
patterns, proposing definitions, merge/split, critique and gaps. The **packet
shape and this Python implementation are proposals**, not a newly adopted native
storage ABI. Native Claim, Entity and Observation records retain their complete
existing `to_json()` fields and meanings. No C++ code, ABI or canonical store is
changed. This exchange projection does not create a second truth store.

## Generic mechanics and boundaries

`packet.py` provides the single `loom.graph_packet/1` codec, rich record-preserving
diffs, previews, configurable acceptance, reversible application and exact
head-only inversion. The same record shapes serve input and diff output. Sources
wrap complete native Observation records, `known_at` (null means unknown) and
the exact UTF-8 text hash. Source text/locators cannot change under the same
observation ID; a corrected source needs a new record. Optional source removal
means removal from this projection, with the complete old record preserved in
history and the application receipt. Claims whose content key changes need an
explicit new record ID; assessment/display updates can use compare-and-swap.

`definitions.kind`, native Entity kinds, Claim predicates, operation annotations
and stage operations are open data. Descriptive merge/split intent does not
silently merge identities, relink claims, register native types or erase history.
An actual proposed merge/split requires explicit record additions/removals and
rewritten claims; dangling references fail before application. Novel definitions
remain definitions proposed by the instrument, not automatically supported types.

Native Assessment and per-record instrument provenance are separate. Automatic
acceptance is a policy value and works without a universal manual-review gate.
It selects a projection; it does not secretly change the native evidence class,
origin or confidence, and does not establish world truth. Model-proposed origin
metadata is preserved; verified transport metadata can bind a **separate derived
diff**. Diff `known_at` is model-proposal availability, not a rewritten source date.
An exact quote can still be attached to a wrong direction or wrong attributed
speaker: codec/source binding checks are mechanism checks, not semantic quality.

History validates each native before/after record, prior ordering/provenance,
base hash and complete backward/forward replay. This establishes internal
consistency. It cannot authenticate a whole imported history without an external
source/commit/manifest anchor. Helpers return the validated caller object without
mutation; they do not promise that a Python caller cannot mutate that object.

Resource limits are optional method data, unrestricted by the packet codec by
default. The old transport's 16 MiB / depth / request-count caps are not universal
whole-archive or swarm limits. Actual allocation/serialization/provider capability
failures remain possible and must be measured separately. The workflow has no
fixed number of stages/agents/models. The owner-confirmation threshold for an
expected usage jump is configurable data with a default of 10; it is distinct
from an absolute spending cap. Live transport is injected and declared by the
caller; authorization belongs to its current method data and session budget.

`workflow.py` executes a declared stage once, saves first transport output before
parsing, preserves critiques/raw diffs and binds instrument metadata separately.
Invalid output never receives a silent retry. Stop/continue, original/current
packet context, automatic/preview acceptance and explicit user acceptance are
data. A reviewer sees prior unaccepted proposals as proposals. Competing original
projections stay separate; an explicit policy may choose one without forced
consensus or hidden merges. This implementation writes only review artifacts.

## Bounded source-only diagnostic arm (distinct from generic workflow)

`experiment.py` is a **separate bounded DEV diagnostic**, using the unchanged
source-only free-v1/v2 compiler and strict surface aligner. The selection was
saved before new free-v2 outcomes: 12 cases, 6 EN/6 PL, 3 per inherited DEV design
block. The four blocks are attribution, negation/unknown, correction/known_at and
paraphrase. No validation inputs or labels were read by the implementation agent.
No new paid API/key use occurred here.

The one-pass baseline reuses the exact saved first source-only proposal. A new
review stage receives only raw turns/source IDs plus that exact proposal text.
It receives no reference inventory, aliases, queries or gold. The reviewer emits
an unverified critique plus a revised graph in the exact existing source-only
output form. Original proposals, critiques and revisions all remain preserved.
The unchanged full-turn source binder compiles the graph; it does not infer
speaker identity, repair unknown IDs, create relations, normalize polarity or
invent source timestamps. A baseline `finish=length`, error, missing or unverified
transport does not receive a replacement or paid review; planned denominators
remain. A complete first text with invalid JSON **may** enter this explicitly
declared new review stage as invalid data; the invalid baseline remains invalid.

Primary metrics use the unchanged strict source-only compiler/alignment/scorer:
node and typed-edge precision/recall with their separate denominators, status
events, unavailable cases and source-surface change counts. Exact-reference alias
alignment is a representation-dependent lower bound. Unmatched paraphrases are
not automatically hallucinations/world falsehoods; semantic review is separate.
Raw syntax repair tests do not measure model quality. No voting channel or judge
can veto a source proposal by an unstated rule. The baseline vs review comparison
is additional compute, not an equal-budget comparison against a larger one-pass
model. Retained baseline costs belong to the parent ledger and are not charged
twice in the review report. Review charges/unknown cost reserves are separate.

Each optional model config binds model, provider, prices, supported parameters
and response version aliases to one byte-hashed public endpoint snapshot.
Billing mismatch is a hard failure even for a semantically rejected response.
The diagnostic's USD 0.10 batch / shared non-resetting USD 2 cap and 12-case
selection are experiment presets, not graph-method capability restrictions.
Only the root coordinator may execute paid requests under the current explicit
session authorization. Frontier paid pilots are not authorized by this protocol.

## Commands and expected evidence

```sh
python3 -m unittest discover -s loom/tools/structure/agentic_graph_v1 \
  -t loom/tools/structure -p 'test_*.py'
python3 -m loom.tools.structure.agentic_graph_v1.build_schemas
python3 -m loom.tools.structure.agentic_graph_v1.experiment freeze \
  --model-config docs/research/agentic_graph_v1/gpt41mini.json
```

The diagnostic capture command takes a JSON list of complete original batch
references `{manifest, run_dir, variant}`, with one variant (`free_source_v1` or
`free_schema_hint_v2`) and the whole original 24-case inventory. It produces the
12-case transfer. `prepare-reviews` produces API-neutral bounded manifests;
`score` replays every planned review batch before reading DEV gold and preserves
first compiled artifacts before evaluation. No command in this package reads a
credential or performs a network request.

The generic workflow's scripted fixture demonstrates 1/2/3 stages and mixed
model labels only. Its result explicitly says `model_quality_measured=false`.
Model recall/precision, useful novelty, identity-resolution quality, whole-archive
cost/performance, native roundtrip/import integration and a live generic provider
workflow have **not** been established by these tests.
