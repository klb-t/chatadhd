# N5 resource reservation history — frozen protocol

Date: 2026-10-01. Base: `926072ce43b8de21a42a3abe439a7781598136f6`.
Owner: design_audit. This protocol precedes the implementation and its results.

## Contract

- Preserve `ResourceLedger.usage()` and `execute_variant.resource_usage` as the
  existing policy admission quantities. Preserve caller limits, capacity reuse,
  uncertain reservations, attempt IDs, callback replay and first-return records.
- Add `ResourceLedger.accounting()` and an additive execution-result field
  `resource_accounting`. Distinguish current admission load, current original
  reserved load, historical maximum observed reserved load, and actual
  simultaneous whole-ledger instrumented peak. The last is unavailable here:
  this callback runtime has no global instrument. Never add per-call maxima and
  call their sum an actual peak. Keep callback quantities/statuses separate.
- Under the existing thread/process lock, each new reservation contains a
  versioned observation of the original reservations still held immediately
  before/after admission. Policy `max_reservation_amount` may increase admission
  exposure; that estimate is not retroactively an original reservation.
- Store the observation in the same fsynced `reservation.json` as the reservation,
  before packet loading/callback dispatch. Bind it to the reservation's content.
  No second independently committed receipt can lose the historical peak after
  capacity is released. Admission failure creates neither record nor observation.
- Keep existing ledger headers and existing files unchanged. Read old reservation
  records without inventing overlap history. Report missing historical coverage;
  a new observation involving older held reservations is evidence only of that
  boundary. External history before opening balances is not reconstructed.
- This is append-only local accounting evidence, not measured consumption,
  proof against malicious filesystem editing, a global transaction with callbacks,
  or an exactly-once external-effect guarantee. No new ceilings or spending rules.

## Before measurement

Run the checked-in `reproduce.py` against unmodified base code. Preserve its first
JSON output, code SHA, Git HEAD and exact command as `before.json`. The fixture
reserves 10+10 concurrently then returns two scripted quantities 7; despite the
legacy fixture's `instrument_measured` labels this is not instrumentation and
does not establish an actual peak of 14.

## Gates, before implementation

1. The original threaded scenario reports current admission 20 while held and 7
   afterward; after the fix the additional historical reserved peak remains 20
   through reopen/replay, and actual instrumented peak remains unavailable.
2. Independent processes share the same ledger; synchronize callbacks so both
   reservations overlap. Reopen and retain the same evidence and attempt IDs.
3. Kill child processes at the reservation/completion persistence boundaries.
   Before durable reservation: no callback; incomplete records fail closed.
   After durable reservation/before callback: reservation/history persist and no
   automatic retry. After completion/before receipt return: reopen replays once
   without dispatch and retains the previously observed overlap.
4. Legacy completed records without observations retain admission quantities but
   receive no reconstructed historical peak. Mixed history stays explicitly
   incomplete. Opening known/unknown balances retain provenance and admission.
5. Capacity reuse, limit rejection, unknown quantities and status policy stay
   unchanged; same attempt replay adds no new reservation observation. Corrupt
   observations fail closed. Exact Decimal sums do not round away small amounts.
6. Run the inherited AnalysisPlan and coordination/analysis-graph suites without
   weakening assertions. Ask N1/N3 for a read-only peer review before freeze.

Native builds, real data, holdouts, private archives and paid calls are excluded
from this increment. ROOT owns publication and STATE.
