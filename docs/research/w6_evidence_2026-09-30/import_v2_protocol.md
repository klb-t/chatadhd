# Import oracle amendment V2 — before V2 native evaluation

Cross-review found two concrete V1 oracle gaps: `before - after` detects lost
leaves but accepts added output fields, and scalar path records alone cannot
distinguish an array from an object with numeric keys. The original protocol,
instrument, fixtures, manifest and first results remain immutable. V1 pass
counts are not proof of exact structural equality.

V2 freezes this protocol and a new independent structural comparator before
rerunning the same native baseline on the same synthetic inputs. The comparator
encodes every object/array node with its kind, complete keys, arity and ordered
children; every scalar includes its exact Python JSON type and serialized value.
Objects ignore key order. Arrays retain order. Both added and missing fields
fail equality. Empty array/object differ, bool/integer differ, integer/float
differ, Unicode code points are not normalized. Lexical JSON number spelling
and whitespace remain the byte-preservation audit's domain.

Before native execution, twelve predeclared oracle controls must pass: added
field, missing field, array/numeric object, nested container mismatch, reordered
array, duplicated array element, bool/integer, integer/float, Unicode
normalization, empty container mismatch, harmless object-key order, identical
nested structure. Then rerun V1 unchanged in a new directory and perform exact
structural equality for each of three conversations and each of nine per-case
raw message objects. These are 24 named checks, not independent experiments.
Expect both OpenAI conversation reconstructions and all raw messages to match;
expect Anthropic original array-order mismatch to remain. Preserve all outcomes.
This is an amendment after observing V1, not a new blinded evaluation. No
production edits, owner exports, holdouts or model/provider calls.
