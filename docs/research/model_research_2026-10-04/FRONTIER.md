# Graph completion versus pattern discovery — offline preparation, 2026-10-04

This is a matched-input research instrument, not a ranking of frontier models.
The two methods have different products: completion proposes changes to the
current graph; discovery proposes recurring structure with separate occurrence
witnesses. Neither mechanical validity nor agreement with author-written
fixtures establishes model semantic quality or content truth.

## Delivered and measured

| Measurement | Before this increment | After this increment |
|---|---:|---:|
| Prepared comparison requests over identical paired inputs | No retained matched comparison in this lane | 36 per bundle |
| Matched input pairs | No retained paired measurement | 18 per bundle |
| Public, author-owned DEV cases | No new comparison corpus | 6 |
| New counterexample tests | 0 | 17/17 |
| New plus existing frontier instrument tests | Existing sentinels retained | 73/73 |
| Scripted mechanically valid responses | Not run | 36/36 per bundle |
| Scripted whole-target / occurrence-reference agreement | Not run | 36/36 per bundle |
| Completion proposals in scripted replay | Not run | 18 added source-backed claims |
| Pattern candidates in scripted replay | Not run | 9 candidates, 18 witnessed occurrences |
| Newly measured model semantic quality | Unavailable | Unavailable (`null`) |
| New network / paid calls | 0 | 0 |

The `final/` bundle uses three explicit owner-select model placeholders. The
`historical-final/` bundle selects three identities from existing endpoint
snapshots and prepares their request bodies. Both replay **the same
author-written responses**; these are two configurations of the same mechanics
exercise, not 72 independent model observations.

DEV cases cover ordinary recurrence, opposite polarity, reversed direction,
shared source groups, unequal scopes and UTF-8 text. The first, shared-source and
UTF-8 cases have two recurring supplied relation occurrences; the other cases
exercise correct abstention. A missing second relation is explicitly present in
the retained source of every case. Reference claims and expected occurrence
sets are in `references.synthetic.json`, separately from request inputs.
The full source text is supplied; the request never includes that reference
answer key. Separate case and bundle reference hashes bind the final replay;
changing the answer key after preparation fails instead of changing the score.
No sealed holdout or private export was read.

## What the comparison preserves

Every pair contains byte-identical user input: complete GraphPacket records,
source text, hash/locator provenance and caller-declared source groups. Its
`input_sha256` and the complete request digest bind the two arms. Only the task
recipe changes. Models, providers, reasoning, generation parameters, recipes,
runtime data, agent counts, repetitions, selections and codec resource policy
are caller configuration. Configurable quantities have no invented upper
ceiling. Serialization of a requested runtime does not prove that it exists or
that its provider supports it; the historical body preview reports that gap.

Completion uses the existing GraphPacket validator, preview and history
mechanics. Every executed add/update/remove and task update must match an
explicit augmentation/loss declaration. Declarations do not authorize
unrequested writes. The entire candidate record content and task are compared
with the reference target, including existing records; adding a correct edge
while silently deleting another cannot receive reference agreement. A declared
alternative projection may be mechanically valid while failing that reference.
The source packet remains immutable and no native store is written.

Discovery checks injective node mappings and distinct source claims for every
template edge, including kind, optional literal label, direction, predicate and
exact qualifiers. Label abstraction requires a declared loss. Unrepresented
template-node semantics fail explicitly rather than being ignored. A model's
unwitnessed hypothesis can remain in the augmentation report, but cannot be
counted as a witnessed occurrence. Occurrence count, declared independent source
groups and confidence remain separate. Two occurrences from one declared group
give one group of support; those group identities themselves are not verified.

Saved model responses can be scored through a `recorded` response wrapper with
a captured model identity. The original proposal stays intact. Its instrument
origin is rebound to the saved model response beside the native assessments;
this prevents a model from making its own instrument origin look like a
recorded-source import. No provider authentication is inferred from this
offline wrapper, and the author-owned corpus still yields no independent model
quality measurement. A future live collector must retain original provider
bytes, usage, actual identity and request binding using the existing audited
transport, then adapt that saved capture to this wrapper.

The historical `frontier_panel_v1` and `frontier_runtime_v1` instruments remain
unchanged. Their scripted runtime mechanics and distinct extraction/judgment
tracks are not retroactively relabelled. `graph_patterns.py` remains a bounded
structural discovery baseline; its fingerprints are candidates requiring exact
verification, not semantic equivalence proofs. This new lane validates proposed
witnesses and prepares paired model recipes; it does not claim exhaustive motif
enumeration or launch managed agents.

## Concrete spending options, pending owner authorization

The historical configuration retains the source path, SHA-256, retrieval date,
provider, full endpoint and pricing data in every model configuration. Source:
[`frontier_panel_v1/presets.json`](../frontier_panel_v1/presets.json) and its
hash-bound `public_preflight` endpoint snapshots. These were retrieved on
**2026-09-30 around 12:42:35 UTC**, and are marked
`stale_retained_snapshot`. None is a current quote.

| Historical identity / provider | Calls in full comparison | Output token allowance used in accounting | Historical reservation |
|---|---:|---:|---:|
| `openai/gpt-6.1-sol` / `openai` | 12 | 8,192 | $2.314000 |
| `anthropic/claude-sonnet-5.5` / `anthropic` | 12 | 8,192 | $1.655168 |
| `google/gemini-3.1-pro-preview` / `google-ai-studio` | 12 | 16,384 (8,192 × configured multiplier 2) | $4.211504 |
| **Total: 6 cases × 2 methods × 3 families × 1 repetition** | **36** | — | **$8.180672** |

All selected body parameters appear in those retained snapshots. Current
endpoint availability, model capabilities and account access remain unverified.
The default runtime is one prepared single call, with low reasoning from the
retained presets. Runtime/agent continuations are outside these reservations.

Prompt allowances are **13,857–14,151** tokens per call, derived conservatively
from UTF-8 request bytes plus 1,024 framing units and 32 per message; these are
not tokenizer measurements. Input rates reserve against maximum retained
prompt/cache-write tiers. Output rates use maximum retained completion or
separately advertised internal-reasoning rates, then the configured completion
multiplier. This allows for reasoning in accounting, but does not prove the
provider caps billable reasoning at that amount. The result is **not a
guaranteed billing bound**; retries, tool calls, extra agent stages, taxes and
unadvertised charges require separate reservations.

| Optional owner-selected stage | Purpose | Calls | Historical reservation |
|---|---|---:|---:|
| One case (`repeat`) across both methods and three families | First-response format and evidence-binding check | 6 | $1.363415 |
| `repeat` + `polarity` | Add a negative structural control | 12 | $2.726830 |
| All six DEV cases | Check scope/direction/source-group/UTF-8 behavior | 36 | $8.180672 |

These are alternative selections of one plan, not mandatory spending stages.
The existing $2 cheap/Jev programme does not authorize a new frontier pilot.
No amount is charged or reserved against that account here. Before any paid
execution: owner-selected scope and explicit frontier budget, fresh endpoint
and account reconciliation, exact tokenizer/resource estimate where available,
first-response capture and the application's configured ×10 policy are needed.
Additional repetitions and runtimes scale the prepared rows; their real
continuation/tool consumption must be estimated separately.

## Reproduction

From the repository root, choosing a **new** output directory:

```bash
python loom/tools/structure/frontier_comparison_v1.py prepare --output /tmp/frontier-placeholder-fresh
python loom/tools/structure/frontier_comparison_v1.py prepare --historical-primary --output /tmp/frontier-historical-fresh
python -m unittest discover -s loom/tools/structure -p 'test_frontier*.py' -q
python loom/tools/structure/frontier_comparison_v1.py replay \
  docs/research/model_research_2026-10-04/frontier/historical-final/manifest.json \
  docs/research/model_research_2026-10-04/frontier/historical-final/responses.scripted.json \
  docs/research/model_research_2026-10-04/frontier/historical-final/references.synthetic.json \
  --output /tmp/frontier-historical-replay-fresh.json
```

Existing files and preparation directories are never overwritten. A failed
partial preparation remains inspectable; rerun into a different directory.
The two final manifests pin source/codec dependencies. Existing historical
endpoint snapshots are referenced by immutable hashes rather than duplicated.
Intermediate first-preparation/prefreeze folders are temporary scripted work,
not model negative evidence, and are outside the accepted artifact selection.

What is not done: live calls, independent semantic evaluation, comparisons on
the owner's real archives, hosted runtime availability verification, current
pricing, native graph persistence or a claim that either method is superior.
Those require new evidence; the current increment supplies a reproducible
comparison contract and a concrete owner-reviewable spending plan.
