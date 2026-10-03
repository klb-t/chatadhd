# Current native C++ source-only graph baseline

Frozen before the first native source outputs. This is a new instrument contrast
on the SAME24 previously inspected DEV conversations; gold elsewhere was already
exposed, so no blindness/holdout claim. Preparation and all native extraction runs
receive only case ID, language, source ID and unchanged turns/speakers/known_at.
No supplied nodes, aliases, queries, labels, priors, model calls or pack changes.

Pin current CLI SHA256 `8e784b882b9a509f4e1f55bae50748d738a896e5a198a02781cb0c4168c23c2c`,
the current built shared library,250-file source aggregate
`7549cbd40f54c9f894792016bc9208af030a0ad00dc466511607434b82d14882`, build environment,
the actual native42-document pack manifest/hash read through the public C ABI in
a fresh worker-disabled preflight runtime. All hashes/input serializations/policy
and scorer are saved before source outputs. Runtime pack hash must equal the pin.

Each case has a genuine lossless OpenAI-shaped JSON wrapper: exact content.parts
text, original turn ID, linear parent/child sequence, original known_at saved
literally and represented as UTC epoch create_time; original source ID in metadata.
The fixture’s named source speakers are NOT OpenAI transport roles. Primary
transport_user sets author.role=user as an explicit synthetic format projection,
with exact original identity in author.name and loom_source_speaker metadata.
This does NOT assert an observed real user role or silently claim that downstream
native knowledge preserves author.name.

The declared representation control sets author.role equal to original fixture
speaker ID. Static inspection found knowledge `walk_chatgpt` emits only user or
assistant roles and omits author.name in its message projection. This control
tests the concrete interface boundary, not an alternative successful algorithm.
Do not relabel dropped content as low semantic accuracy. Raw JSON/source metadata
must stay preserved in both arms. Zero current-source-output observations precede
this registration.

Run native knowledge catalog→extract with explicit catalog import.mode=full,
store_mode=copy,retain_raw=all,scan threads1,no priors,llm off. This keeps all24
source cases despite relevance scoring and preserves raw blobs; no lowering of
a quality gate or gold-driven selection occurs. Each case/arm uses a fresh isolated
data directory, avoiding cross-case name mining and entity merges. No evaluation
queries enter extraction. CLI runs sequentially; source-level output bytes,
stderr, run IDs, pack hashes, immutable read-only SQLite body snapshots, counts,
source/blob hashes and wall/CPU/RSS metrics are saved first. Never read secrets.

Bound60s per process and240s for the whole48 planned case-arm series. If a limit
or terminal error occurs, preserve the first artifact and mark remaining/failed
cases unavailable with their planned24-per-arm denominator; do not auto-rerun.

Evaluation after ALL native extractions: inventory observations/entities/claims/
statuses; verify each observation’s RFC6901 source pointer and relative UTF8 span
against unchanged serialized source text; report original speaker/date retention
separately from raw metadata preservation and native transport-role fields.
Verify original container raw bytes are copied into native blobs. Native observed
claims remain assertions about source content, not verified world facts.

Only use the source-free typed-edge ABI/gold metric if native output legitimately
expresses the same relation,direction,source actor,polarity and known_at contract.
Do not fabricate an implication/causation conversion from generic item/decision
claims or call missing semantic capability0% accuracy. Otherwise that comparison
is unavailable/null, while recorded native schema/count/source-grounding gaps are
real measured outcomes. Current built-in relation_patterns is a software-domain
instrument; scope/domain mismatch must be stated. No native implementation edit
or heuristic adjustment follows DEV outcomes. Preservation/availability/inventory
metrics and semantic graph-quality metrics are distinct.
