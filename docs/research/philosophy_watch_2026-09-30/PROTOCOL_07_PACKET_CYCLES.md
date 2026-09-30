# Unrestricted JSON validity: cycle probe 07

2026-09-30. No resource ceiling is imposed on whole-archive graph packets by
default. That must still reject non-JSON cyclic Python objects. Test the pure
resource validator on a self-referential dict in a disposable child process.
An entered-marker separates startup overhead from validation. After 0.2s of
entered execution, terminate a nonreturning child; never leave a runaway worker.
This is a mechanism nontermination probe, not a speed/quality benchmark.

An acyclic DAG sharing the same dict under two keys is valid serialized JSON and
must remain accepted; detecting every repeated Python object ID as a cycle would
be overrestrictive. Snapshot code before the first probe, preserve first outcome,
and retest a producer fix separately. No API, credentials, validation or edits
outside this directory.
