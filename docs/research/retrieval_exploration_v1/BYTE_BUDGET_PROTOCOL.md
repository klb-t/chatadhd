# Secondary source-byte budget experiment

Frozen after inspecting the first 29-method DEV results and before measuring this
second series. No feature, score, query representation or ranking changes. The
variable is context accounting/allocation: same selected-turn cardinality does
not imply the same source bytes or downstream context cost.

For each of the four already-declared membership unions, use its per-query sum
of UTF8 bytes of COMPLETE unchanged raw turn text as a shared budget for every
ranked method. Report consumed/unused bytes, selected turns, evidence hits/span
recall/turn precision separately. Bytes are an exact source size measure, not an
LLM token or monetary estimate. Full turn text must be retained; never cut quotes
or operators to fit a budget. Source speaker labels have no charged bytes here;
all methods use the same rule, so this is a source-text budget comparison only.

Two declared allocation policies: rank_prefix stops before the first ranked turn
which would exceed remaining bytes, even if cheaper later turns exist. rank_skip
skips that turn and continues in unchanged rank order, never revisiting it. The
second is an explicit cost-sensitive heuristic, not an undocumented channel veto.
An empty selection remains in every planned query denominator. Candidate source,
as_of eligibility, raw hashes, gold relevance and previous evidence targets stay
unchanged.

Because pools have at most three turns, enumerate their subsets only for a
separate gold-conditioned maximum attainable span recall ceiling at each budget.
That ceiling is NOT a retrieval method, does not establish precision, source
truth or usable model quality, and never feeds a ranking. Gold enters this oracle
only after the allocation policies/results are frozen. Primary comparisons retain
the baseline token and original MiniLM, plus ALL 29 methods; do not cherry-pick
the method with highest DEV score.

Separately diagnose role/direction sensitivity already observed in the first
scores: number of equal forward-question/reverse-question full rankings; score
delta ranges and exact rank reversals. This is an instrument observation, not a
semantic classification claim. Keep the original first scores and failure intact.
