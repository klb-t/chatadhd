# Philosophy watch, snapshot 02: planning fidelity and configurable methods

2026-09-30. **20/20** declared offline archive-cost probes were executed against
the unchanged source hash pinned in `ARCHIVE_COST_FREEZE_02.json`. First returned
values, exceptions, generated-file inventories and source hashes are preserved
in `ARCHIVE_COST_FIRST_RESULTS.json`. There was no model call or owner-data read.
No production source was edited. Three invalid fraction probes correctly raise;
the other negative/nonfinite numeric inputs return instead of rejecting.

## Concrete defects to fix separately

| Input | First observed behavior | Consequence / smallest independent fix |
|---|---|---|
| `output_ratio=-20` | Model bands return USD −0.08 / −0.13. | Validate finite nonnegative count ratio; do not invent a maximum ratio. |
| NaN/Inf output ratio; NaN input price | NaN/Inf propagates into cost and assumptions. | Reject nonfinite domains before planning/serialization; keep unknown price explicitly unknown. |
| `prefix_tokens=-100000` | Returns USD −0.99 in both bands. | Nonnegative integer count domain. Zero remains useful and valid. |
| Boolean prefix/conversation count | Bool accepted as integer 1. | Reject bool where a count is expected; this is type fidelity, not a user budget cap. |
| SQL message status NULL | Two rows/10 characters become one row/4 active characters, without warning. | Explicitly retain/account for unknown status, or report excluded unknown rows and their denominator. SQL `!=` silently omits NULL. |
| Literal `?` or `#` in DB filename | An empty sibling `literal` is created; then `no such table: messages`. Original DB hash remains unchanged. | Use an escaped literal path URI (`Path.resolve().as_uri()` + `?mode=ro`) so mode is actually applied; test no new files. |

The URI defect contradicts the estimator's **no writes** claim even though its
target source DB is unchanged. Tiny generated probe databases lived only in
disposable temporary directories; their first source hashes and unexpected
sibling bytes are retained as evidence. No user's database was touched.

## Planning assumptions are not provider usage facts

- The first/only reading is assigned cache-read prefix prices without an
  eligible first cache write or provider support check. A theoretical cached
  scenario is useful when labelled; it cannot imply that actual initial archive
  analysis will receive that price.
- A global batch discount is applied to every model without an endpoint batch
  capability check. Preserve uncached/nonbatch controls and per-provider
  assumptions; unavailable capability is not an inferred discount.
- `fraction × conversation_count` is a fractional expectation; it is not the
  number of selected conversations from an actual inventory. At 10 conversations
  and fraction 0.01, the calculation uses 0.1 conversations.
- An excluded-only conversation remains in the total conversation denominator
  despite contributing zero eligible text. A plan can choose to read it, but the
  current character/prefix assumptions need an eligible-reading denominator.
- Small positive costs rounded to two decimals become USD 0.00. This is display
  precision, not zero resource usage or zero reported billing. Keep unrounded
  planning values for ×10 comparisons and budget accounting.

The point is accurate configurable planning: finite large budgets, arbitrary
scope/model/reasoning and automatic acceptance remain available. Domain checks
must not be repurposed as an artificial upper spending limit. The current session
restriction on paid synthetic experiments is separate from application policy.

## Existing native path is a named bounded profile, not the new general method

Source inspection of `loom/src/extract/semantic.cpp` shows hardcoded upper bounds
in `read_limits`: requests 8, observations 64, input bytes 256000, output tokens
4096 and timeout 60000 ms. `kPrompt` permits only existing IDs; `kGraphPrompt`
permits five operations and rejects unsupported structures into unknowns. Both
are constexpr recipes. `knowledge_semantic.h` says results enter candidates,
never canonical claims. These are real capability limits of the legacy bounded
method, not general requirements restricting every frontier method.

Keep that method reproducible as an explicit bounded extraction profile. New
first-class frontier methods need configurable graph completion, free discovery,
new structures/types, merge/split/reorganization, critique, gap discovery and
whole-archive reasoning. They must not silently inherit the old method's scope,
operation list, output cap or always-review preset. Real provider capability
limits should be reported faithfully, independently of defaults and user budget.

## Protocol receipt note

Protocol 01 requested a local commit in its pre-outcome freeze; the first freeze
contains exact method/protocol/probe hashes and timestamp but omitted that Git
field. The earlier inspected local commit was `6269f26feea0f744d9aa828bafd378b0fb871689`;
it is not retrospectively inserted or certified as the execution-time commit.
File hashes, not an invented commit attribution, identify the audited code.
This reporting omission does not change the 17 first mechanism outcomes.
