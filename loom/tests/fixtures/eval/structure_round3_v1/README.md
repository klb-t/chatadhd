# Independent thought-relation fixture v1

Read [PLAN.md](PLAN.md) before use. Development is in `dev.json`; validation
is sealed by protocol in `validation.json`. The author has not inspected the
parser or existing source-document benchmark before freezing these files.

Each file is an object with `schema`, `split`, `span_unit`, `cases`. A case
contains `id`, `language`, `family`, `label` (`supported` or `abstain`), `text`,
`expected`, and `annotation_note`. `expected` is empty for abstentions and
contains one relation for supported cases. A relation has `family`,
`operation`, ordered `operands` (each with its semantic `role`), `cue`, and
`qualifiers`. Located strings carry `text`, `span` (half-open Unicode
codepoints), and `utf8_byte_span` (half-open UTF-8 byte positions).

Operation names are benchmark labels, not additions to Loom's closed
conceptual vocabulary. A scoring adapter may explicitly map existing parser
operation names to them, provided the mapping is fixed before validation.
Order reflects semantic direction, not necessarily textual order.

| Family | Operation | Operand roles in semantic order |
|---|---|---|
| conditional | implies | condition, consequence |
| cause | causes | cause, effect |
| contrast | contrasts | left, right |
| exception | except | default_rule, exception |
| exception | unless | exception_condition, default_consequence |
| means_goal | achieves | means, goal |
| generalization | generalizes | subclass, superclass |
| conjunction_alternative | and | left, right |
| conjunction_alternative | or | left, right |
| evidence_claim | supports | evidence, claim |

`qualifiers` retain distinctions a bare edge would lose: affirmative relation
polarity, stated universal scope, non-exclusive/unspecified alternatives, or
exception scope. No clause is evaluated for truth. A source assertion that a
measurement supports a claim is still a source assertion; it does not promote
the claim into a measured fact.

For exact scoring, compare source spans/text and the mapped semantic roles.
Do not silently reverse cause/condition/evidence operands, discard negative
words, or treat a detected cue as successful argument extraction. Symmetric
contrast/conjunction operands may be compared unordered if that policy is
documented and frozen before validation. All other roles are directed.

Files are immutable for this round. `manifest.json` hashes the two splits and
the written protocol. New fixtures or corrections must use a new version
and must state whether prior parser predictions informed their creation.
