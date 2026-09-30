# AnalysisPlan provenance and ecosystem audit

Known at 2026-09-30 after protocol15. Seven independently authored cases ran
using the already installed contract-deps environment; the first bare Python
invocation lacked jsonschema and stopped before snapshot/freeze/execution.
No dependencies were installed, no real provider request or canonical write.

Confirmed diagnostics in ANALYSIS_PLAN_FIRST_RESULTS_15.json:

- A stable unpinned source descriptor resolves packetA once. After the caller's
  current data becomes packetB, an identical plan reuses the first result without
  another loader/callback call. The retained first result is correct as a replay
  of that first job; it is not evidence that the current source still has those
  bytes. Updating an explicit provenance revision correctly changes identity.
- A reported zero with `measurement_provenance.money_usd = null` or `{}` is
  accepted as completed and releases the reservation from1 to0. This shows that
  presence of a provenance key alone does not establish a known measurement.
  The fixtures are fabricated bookkeeping, not actual invoices.

Positive controls: source provenance including known_at=null and the distinct
domain semantic contract arrive byte-equivalent; unknown money stays held1;
explicit source revision changes invoke a separate attempt; tool configuration
and project scope arrive intact without engine dispatch; configured automatic
acceptance remains caller data, and the reference executor applies no graph.

Recommended boundary contracts, all data-driven:

1. An adapter that claims reproducible source-bound execution should pin a
   source revision/content fingerprint and record the resolved packet digest.
   Unpinned descriptors may remain allowed, with an explicit unverified binding
   status. Replaying a first job must not silently claim refreshed source data.
   Pinning policy must work with different ecosystem graph semantics and stores.
2. Known quantities should retain nonempty typed provenance with a source/recipe
   or provider receipt. Estimated, reported, instrument-measured, unknown and
   withheld quantities have different meaning. Their budget admission treatment
   is a declared per-dimension policy; a model estimate is not an invoice.
3. Registering a callback delegates an executor binding. The reference planner
   does not execute tools from their configuration and cannot prove an arbitrary
   Python callback obeys a domain scope. A runtime/tool adapter must carry the
   explicit permission, domain contract and resource references into actual
   dispatch, as independently checked in runtime13. No universal app monopoly,
   fixed agent ceiling or blanket autoaccept prohibition is needed.

Parent and the independent deep code reviewer received the exact first paths.
Any producer correction must receive separately named source snapshots/retests;
these original diagnostics and scripted ZIP remain unchanged.
