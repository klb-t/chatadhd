# T7 verification and handoff

- `python -m unittest loom.tools.seeding.test_prototype -v`: **23/23 pass**.
  Includes donor isolation, adversarial target-prose redaction, absent/inferable
  role exclusion, alternative provenance, baseline-win counterexample,
  local-premise abstention, negative-control denominators, prediction-before-score
  persistence, overwrite refusal, input immutability, and creation/know-time
  separation. Mechanism tests are not model-quality measurements.
- Combined own and independent reviewer suite at the V4 implementation:
  **36/36 pass** (23 own + 13 independent; final independent run 3.431 s).
  Independent tests/report are maintained separately.
- Four protocolled development configurations, plus a metadata-only replay,
  61 cases each, top-1/top-3 budgets,
  32 random rankings; first outputs, policies, protocols and code are retained.
- V1 all top-1: mapping precision 7/61, recall 7/46; frequency 8/61, 8/46.
- V2 all top-1: mapping precision 13/55, recall 13/46; capability proxies
  precision 6/9, recall 6/10, against frequency 0/15, 0/10. Roles still lose to
  frequency; exact feature labels have zero donor-vocabulary coverage.
- V3 eligibility-matched frequency control: top-1 precision **14/55**, recall
  **14/46**, top-3 precision **16/149**, recall **16/46**. It matches every
  capability prediction of weighted mapping and wins overall. Existing full
  method score dictionaries are identical to V2; no weighted-ranking gain is
  established. Preregistered control outcomes were already known from review.
- V4 preserves application alternatives as ORs of separate conjunctions, with
  explicit donor decision/source witnesses. Target pA now qualifies the pA
  application without needing pB from another application; partial pA of one
  pA AND pB application and unknown empty premises still abstain. Historical
  union mode remains available. Every V4 score dictionary exactly matches V3
  on the development fixture; this corrects graph semantics, not model quality.
- A separate scan of saved V4 predictions verified 22 applicable capability
  source-chain witnesses: nonempty complete conjunctions in visible target
  principles, donor decision/source path and unknown source know-time.
- The independent reviewer additionally rebuilt all 22 V4 application witnesses
  directly from oracle decisions, checked exact decision/source locators and
  nonempty conjunction preservation, and verified frozen hashes and full V3/V4
  score equality. Initial failing union-conjunction examples remain in the
  independent review history; final correction passes its ratchet.
- A separate recount from the JSON fixture and serialized predictions (without
  importing the prototype) verified all top-1/top-3 TP/emitted/positive counters
  for each task and each primary method, donor-project isolation, mapped
  premise membership, and corrected know-time metadata.
- All baseline score dictionaries are identical between v1/v2. Role and feature
  score dictionaries are identical too; only the capability mapping changed.
- `synthetic_lopo_v2_metadata_corrected/results.json.gz` has exactly the same
  complete score dictionary as `synthetic_lopo_v2_first/results.json.gz`. The
  correction preserves source dates separately from unknown historical source
  know-time. Old snapshots retain their explicitly labelled previous semantics.
- Frozen protocol/policy/code SHA-256 hashes were independently checked against
  each run manifest, and fixture hashes remain identical. V1 raw JSON gzip
  archives preserve byte-identical original output (`uncompressed_sha256.txt`).
- No paid calls, production graph mutation, protected C++ edits, altered gates,
  hidden holdout reads or commits from this subtask.

The latest replay command is in `README.md`; point the output to a new directory.
Current results are **development oracle completion**, not extraction or blind
validation. Global operator text could have been authored using whole-corpus
knowledge; project-level donor isolation does not prove temporal independence.
Do not present any run as predict-before-observe quality.

Next informative step: explicit situation predicates and novel target-specific
templates, then a fresh independent corpus. Principle overlap alone causes
NoteFlow uncertainty false positives and does not justify promotion to fact.
