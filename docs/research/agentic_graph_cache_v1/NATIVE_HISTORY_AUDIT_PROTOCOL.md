# Nonempty native journal audit — before outcomes

The first resource benchmark used scripted empty-diff events. Its timing matrix
is already frozen and running unchanged. This separate correctness series
checks meaningful record changes rather than assuming an empty journal proves
every native history case. It will not tune timings, alter cache code or amend
first results.

Construct one fixed twelve-event sequence before execution: task replacement,
open definition add, native Entity display update, native Claim Assessment
update, unused source add, child Entity add, native Claim add, unused source
tombstone, dependent Claim/Entity removals, original source tombstone, exact
original source/Claim restoration, definition removal. All original and removed
native values, raw UTF8 text, instrument metadata, unknown source availability
times and previous collection ordering must remain reconstructible.

For both cache modes compare every head against the unchanged strict codec,
canonical packet bytes, original application receipt bytes and exact inverse.
Whole memo must use strict validation for a new head; verified-parent mode may
reuse only the previously authorized immediate parent. Source projection
tombstones remain governed by the original explicit apply policy.

Counterexample families are fixed: tampered before record; tampered before
source text; tampered before provenance; tampered after record; wrong change
action; missing native before bytes; duplicate change; wrong previous order;
stale per-record comparison hash; obsolete application inverse. Rehashing a
tampered event/head is not authorization. Test both latest and older journal
locations when available, with matching cache prefixes already primed. Retain
first results whether they pass or expose a mismatch. This is structural
equivalence testing; it does not establish model or content quality.
