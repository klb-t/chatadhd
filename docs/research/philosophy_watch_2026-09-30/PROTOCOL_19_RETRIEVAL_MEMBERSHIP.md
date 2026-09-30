# Retrieval interpretation and membership recount: protocol 19

2026-09-30. Results were already seen in DEV reports. This is a retrospective
independent integrity/metric audit of archived scores, not a preregistered new
model-quality experiment. Do not rerun encoders, read weights or load holdouts.
Pin archive/probe/protocol hashes before the audit outputs.

Verify all nine archived payload hashes; 96 query IDs, 252 timestamp-eligible
candidates; 60 evidence-query and66 span denominators including36 unknown queries
in precision; all29 initial and34 joint-table top1 precision/recall numerators;
all four unions preserve every member's top1 selection; every same-cardinality
single-method control actually selects that query's union cardinality. Compare
the simpler BM25 and complex encoder using the same targets, without promoting
relevance to a source assertion or truth. Target derivation remains inherited
DEV annotation, not independently reannotated gold or a fresh population.
