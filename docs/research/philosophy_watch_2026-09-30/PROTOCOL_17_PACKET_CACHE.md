# Independent exact-byte cache invariants: protocol 17

2026-09-30. No timing or model-quality claim. Freeze new cache, strict codec,
JSON helper and authored probe source hashes. Compare pure cache validation
against the unchanged strict baseline on authored native packets and histories.

Cases: whole hit preserves exact applied graph/receipt bytes; authorized immediate
parent permits extension; a rebuilt different old history forces strict fallback;
invalid old rehashed history rejects; mutable input/return cannot change stored
bytes; foreign private entry rejects; serialized public receipt cannot authorize;
resource limits still reject a primed packet; limits/policy change force misses;
zero storage still validates; >16MiB input is valid with explicit unlimited cache
policy; rebound selected strict function rejects; rebound transitive history
validator should also invalidate authority rather than return an old hit.

Binding probes mutate only process-local Python objects and restore them in
finally blocks. No production file changes or shared interpreter mutation.
Keep first outcomes. Failures justify isolated producer fixes, never weaker
native/provenance gates. A cache only accelerates a structural validator; it
cannot change model trust, source meaning or acceptance into content truth.
