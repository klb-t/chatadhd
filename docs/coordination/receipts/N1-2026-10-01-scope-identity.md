# N1 — named ActiveTask scope identity

Date: 2026-10-01. Test-first checkpoint `5152f20`; implementation `bd1d9f1`.
Base is the integrated W1 journal, not the alternate external W1 format.

`loom::Json` is `nlohmann::ordered_json`. Before this correction, both the
journal's `json::dump(scope)` map keys and `prepare_active_task`'s whole-object
scope equality depended on member insertion order. Thus the same named
`conversation_id`, `branch_id`, and `task_id` could be treated as another scope
when the caller reordered its JSON object. A fresh product/version-one root
could bypass the existing head; a legal successor with reordered scope keys
could instead be rejected for lacking a root.

The two public Runtime/EventLog counterexamples were committed before the fix:
reordered same-value scope cannot add another root or any provider/callback/row/
event effects; a legal successor can reorder those members while its entire
supplied specification remains byte-order-faithfully retained in JSON. The
second case also checks latest replay and stale-root rejection.

The minimal correction shares one internal key made from the three named
values, in fixed positional order. The key is used by final chain grouping,
ordered journal-head folding, legacy head seeding, and the one scope comparison
in `prepare_active_task`. No request parser, supplied specification, event
payload, hash serializer, SQLite schema or ABI was rewritten. Existing
same-product evidence checks remain strict; this is scope lookup semantics,
not a general normalization of caller data.

Existing journals whose differently ordered scope objects previously admitted
competing roots now fail explicitly as a conflicting scope history. The patch
does not silently choose a winner, erase records, or invent a fork; repair of
such a history requires an explicit future recovery operation.

At this checkpoint the deterministic native cases have been authored and the
patch passes `git diff --check`; runtime execution belongs to the central
verification lane. No local compilation was launched after the coordinator
reserved the single build/probe path during disk recovery. This receipt is not
an executed-success claim and does not alter the previous W1 receipts.
