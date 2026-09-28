# Context packets and reversible candidate deltas

`context_delta.py` is experiment C: source/context selection and reversible view
updates over one immutable, explicitly supplied snapshot. It complements the A/B
candidate graph compilers. It does not extract thoughts, resolve pronouns by itself,
merge entities, delete Claims, change Assessments, promote candidates, call a model
or write production state.

## Public interface

```python
prepared = prepare_context(
    base_snapshot,                 # dict, JSON string, or UTF-8 JSON bytes
    current_spans,                 # common exact span tuple + local id
    claim_refs,                    # [{claim_id, selection_reason}]
    time_cut="2026-01-03T00:00:00Z",
)
packet = prepared["packet"]        # only when status == "ready"
checked = validate_delta(packet, delta)
applied = apply_view(base_snapshot, packet, delta, previous_view=None)
restored = undo_view(applied["view"])
```

The source packet uses the shared `schema: loom.source_packet/1` contract:
`snapshot_id`, full native `observations`, `entities`, and `claims` arrays. The
snapshot may also contain `threads`, `assignments` and opaque fields that remain
untouched in the local original. Only selected observations, selected old Claims
and their endpoint Entities enter the source packet. Existing Claims keep their
complete Assessment, status, source-group metadata, selection reason and support
dependencies. Current spans are a separate `current_spans` array; they do not
become old Claim premises automatically.

Each current span is:

```json
{"id":"span_local","observation":"o_current","byte_start":0,
 "byte_len":9,"quote":"It waits."}
```

Offsets count UTF-8 bytes within the immutable Observation text. The quote must
match those exact bytes, including Unicode and punctuation. The local `id` is a
packet-relative handle. Every delta therefore binds both `base_hash` and
`packet_hash`; selecting a different substring under the same handle makes a
different packet and invalidates the old delta. Snapshot IDs alone are never
treated as content identity.

## Model context versus local undo data

`prepared.packet` is the model-facing projection. It contains no original snapshot
bytes, base canonical JSON, unselected future Observation bodies, or future thread
bodies. Existing assignment metadata is reduced to matching immutable span IDs;
it does not carry opaque assignment payloads into a prompt. Exclusion counts are
explicit. Source Observation bodies remain complete so exact span verification
and native source identity are preserved.

`prepared.audit` and `prepared.retained_input` are local-only sidecars. They retain
the exact original snapshot as `snapshot_bytes_b64`, its raw hash, canonical JSON,
submitted refs and cut. They must not be sent to a model: the original snapshot can
contain future or unselected source material. The same origin is retained locally
by the reversible view. Failed preparation returns the input and structured errors,
with no usable packet.

`validate_delta` validates against the selected packet, including its hash and
selected byte/body consistency. This is a packet-relative check. `apply_view` has
the authoritative explicit base as a separate argument and reruns packet selection
against it before applying anything. Recomputing a packet hash after changing
source assessment/body data cannot bypass this base replay. Hashes identify
content; they are not cryptographic authentication of source truth.

## Proposed changes

```json
{
  "id":"delta_1",
  "base_hash":"<packet.base_hash>",
  "packet_hash":"<packet.packet_hash>",
  "additions":[{
    "id":"draft_1","draft":{"subject":"e1","predicate":"keeps","value":"scope"},
    "current_span_ids":["span_local"],"existing_claim_ids":["claim_old"]
  }],
  "threads":[],
  "links":[],
  "references":[{
    "id":"reference_1","span_id":"span_local","status":"unresolved",
    "alternatives":[],"selected":null,"evidence_span_ids":[]
  }]
}
```

All collections are optional arrays. An addition is an uninterpreted candidate
draft, not a canonical Claim; this module does not validate its logical language
or promote fields inside it. New threads require `id`, `label`, and nonempty
`current_span_ids`. IDs cannot overwrite selected base IDs or earlier overlay IDs.

Each link has `id`, `type`, typed `source` and `target` refs, nonempty
`current_span_ids`, optional `existing_claim_ids`, and a nonempty `reason`. Typed
refs are `{kind: "span"|"claim"|"thread"|"addition", id}`. Only allowlisted old
Claims and explicit current/delta records can be referenced.

| Link type | Source → target | Interpretation retained |
|---|---|---|
| `thread_membership` | Current span → thread | Candidate membership |
| `continuation` | Current span → thread | Local continuation |
| `return` | Current span → thread | Explicit return to an earlier topic |
| `correction` | Current span/addition → old Claim/addition/thread | Proposed correction; old body/status stays intact |
| `contradiction` | Current span/addition → old Claim/addition/thread | Conflict proposal; neither side is resolved automatically |
| `analogy` | Current span/addition → old Claim/addition/thread | Comparison proposal; no identity merge |

One span can belong to multiple threads. There is no exclusive topic partition or
forced view. Old Claim targets must also be listed as explicit old dependencies.
The source span must be part of the evidence for a span-sourced link. Its cited
anchors cannot occur later than that source. Addition-sourced links must retain
the addition's current evidence spans.

Reference status is `unresolved`, `ambiguous`, or `anchored`. Unresolved may keep
no alternative; ambiguous keeps at least two with `selected: null`. Anchored needs
one selected alternative and explicit causal evidence. Unknown references never
force a guessed identity. A later delta cannot replace an already recorded
reference assignment, even by renaming the same exact source span's local handle.
Conflicting interpretations can remain distinct candidate records rather than
rewriting the earlier source.

## Time and causal ordering

The cut must be an explicit timezone-aware timestamp. Timing comes only from
record `observed_at` or snapshot `context_metadata` maps named `observation_times`,
`claim_times`, `thread_times`, `entity_times`. A `source_groups` map can explicitly
supply source-group identifiers. These fields are caller assertions about
availability. Native `created` and qualifier `valid_from` are not silently treated
as availability times.

A requested future Observation, future old Claim availability/support, or future
selected Entity body makes preparation fail. Future thread bodies are omitted.
Missing, invalid or timezone-free record timestamps remain `unknown`, with that
state retained in metadata; they are not presented as past observations. A Claim
anchor must have both its own availability and every retained supporting
Observation established at or before the source reference. Merely preceding the
global cut is insufficient.

Within one Observation, byte positions establish local order. Across Observations,
an anchor needs a strictly earlier known timestamp. Equal timestamps across records
do not establish order. A later project name therefore cannot authorize an earlier
reference assignment through an explicitly later anchor. This checks cited
chronology, not whether the model secretly used another visible cue. Whole selected
Observation text may include later clauses; preventing implicit look-ahead during
extraction requires a causal chunker or a separate source-prefix projection.

## Apply and undo

Application creates an ephemeral overlay beside the unchanged snapshot. Every
overlay record retains `context_packet_hash` and `delta_id`; local handles from
different packets remain auditable. Applying the same delta ID, content and packet
again is `unchanged`. Reusing a delta ID with changed content, colliding overlay
IDs, a changed base, or a failed validation rejects the entire attempted update
and retains the previous view. There is no last-writer-wins behavior.

`undo_view` removes the complete overlay and returns the original snapshot,
canonical JSON, source refs and exact bytes:

```python
original_bytes = base64.b64decode(restored["snapshot_bytes_b64"])
```

This restores whitespace and Unicode bytes for string/byte inputs. Dictionary
inputs restore their canonical JSON serialization. Undo is a view operation;
there is no production rollback because nothing was persisted.

## Measured mechanisms and limits

Run the author-owned suite:

```bash
python -m unittest discover -s loom/tools/structure -p test_context_delta.py -v
```

The 19 checks cover exact Unicode spans, retained assessment/source context,
future/unknown times, support chronology, ambiguous references, late anchors,
overlapping topics, all six distinct relation types, lossless undo, idempotence,
conflicting IDs (including unselected base records), base replay, renamed source handles, model-packet leakage and
failed-input retention. Read-only peer review found packet rebinding, future bytes
in the original sidecar placement, late comparison evidence and late support
availability; each has a regression check. These are mechanism results, not an
independent natural-language score. A/B graph evaluation does not measure C.

The prototype processes explicit finite inputs; it does not search the corpus,
resolve all recursive Claim-premise dependencies, assess source-group independence,
interpret arbitrary draft semantics or establish thread correctness. Missing
semantics remain missing. It restores the whole view, not an arbitrary partial
history. Selecting source/context and specifying observed-at times remain explicit
caller responsibilities. No fixture/holdout data, model provider or production
store is used by these tests.
