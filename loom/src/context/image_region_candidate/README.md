# Selected OCR output as a context candidate

`image_region::candidate` (the overloads in native.h) adapts
one existing region node. It does not run OCR, create graph nodes, or store data.
`contract.cpp` is the stdlib-testable validation core; `native.cpp` binds it to
existing SourceRecord, model::Unit/Observation, JSON and BlobStore facilities.

The native adapter checks deterministic Unit/Observation/node identities,
source identities, representation format/hash/size, original blob integrity,
canonical region/text pointers, method and attribute copies, inferred evidence,
and the selected region's unambiguous 2D pixel frame and bounds. Malformed or
unsupported coordinates fail; there is no guessed coordinate conversion.

The result contains only the selected region text (optionally an exact UTF-8
prefix), its locator and geometry, method, representation hash and original
source/hash. `evidence=inferred` and `confidence=null` are also rendered in the
heading. Raw/excerpt resolution refers to the stored OCR output, never original
image text. Engine scores are not calibrated confidence. No source_text_occurrence
schema is synthesized and no original or derived bytes are rewritten.

`Access` is caller-supplied source/blob access. Both source lookups precede blob
reads; denial/missing/corruption errors propagate, without cached-content or
filesystem-path fallbacks. The convenience overload uses the caller's Database
and BlobStore, including its existing storage placement. Callers with narrower
source permissions must inject their access checks. This is not a new ACL system.
The original is hash-verified, not decoded; the frame is an inferred OCR frame.

The adapter does not traverse the graph or assign a token budget. The integrated
unified-context hook invokes it after review exclusion and inside the existing
legacy graph traversal. OCR candidates share candidate/visited/hop/detail limits
and the final rendered prompt budget with other context. At zero hops a selected
region does not admit siblings; larger reach follows existing caller policy.
With no source reader, the hook reports an unavailable candidate, not guessed text.

Run the small fixture harness from the repo root:

    PYTHONDONTWRITEBYTECODE=1 python3 loom/tests/image_region_candidate/run_focused.py

It compiles only this contract and existing SHA256/UTF-8 utilities, serially,
with a 512 MiB process memory cap and temporary outputs. It does not compile
Loom's backend. JSON/DB/unified-selector acceptance is in
`loom/tests/image_region_candidate/native.verify.cc`; the sole integrator must
execute it against an updated core using the existing native runner environment.
The two-region JSON and original.txt are synthetic mechanism fixtures, not an
OCR accuracy measurement. No real media, model, OCR engine or network is used.

Integrator binding: ChatEngine uses its existing runtime/account attachment
resolver and ProvenanceStore. No new ChatEngine member/layout or C ABI was
introduced. Both source lookups precede verified blob reads. The optional
internal context callback is appended after the existing host-occurrence
verifier, preserving CSV/AST checks. Public standalone callers without source
access still receive an unavailable candidate; cached OCR text is not a fallback.
