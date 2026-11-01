// Synthetic mechanism fixture only. No server, private source, provider or product-proof claim.
// Node 22: node --experimental-strip-types --test tests/projection-review.fixture.mjs
import assert from "node:assert/strict";
import test from "node:test";
import { checkProjection, projectionRequest, readProjectionReview, reprojectProjection, validateUpgradeTarget } from "../src/api/projection-review.ts";

const projection = {
  schema: "loom.source_text_parts/1", source_id: "source-old", source_sha256: "c".repeat(64),
  source_node: "source-node", projection_node: "projection-old", version_node: "version-old",
  occurrence_nodes: ["old-a", "old-b", "old-c"], coordinate: "source_utf8_bytes",
  status: "partial", reason: "fixture", method: { format: "text" },
};
const receipt = {
  schema: "loom.projection_upgrade/1", old_projection: projection.projection_node,
  old_receipt_sha256: "a".repeat(64), saved_integrity: "verified", current_replay: "compatible",
  refusals: [], selection_scope: "account_global", selection_changed: false,
};
const upgrade = {
  ...receipt, new_projection: "projection-new", new_source: "source-new", new_receipt_sha256: "b".repeat(64),
  mapping: [
    { old_node: "old-a", candidates: ["new-a"], status: "exact_coordinate_proposal" },
    { old_node: "old-b", candidates: [], status: "decision_required" },
    { old_node: "old-c", candidates: ["new-b", "new-c"], status: "decision_required" },
  ], activation: "explicit_selector_update_required",
  rollback: "old_projection_retained_restore_precopy_for_old_runtime_if_replay_refused", upgrade_node: "upgrade-receipt",
};
const config = {
  context_execution: { unified: { enabled: true, legacy_seed_ids: ["unrelated-node", "old-a", "old-b", "old-c"], legacy_relation_hops: 3 },
    graph_reply: { enabled: true }, unrelated: "keep" }, model: "unchanged",
};

test("projection review: exact native envelopes, refusal/mapping, stale selection and abort without selector writes", async () => {
  const calls = [];
  let fresh = structuredClone(config);
  const host = {
    async getConfig() { return structuredClone(fresh); },
    // Deliberately no mergeConfigKey/setConfig: the adapter must need neither.
    actions: { async clientBlobText(request) {
      calls.push(structuredClone(request));
      return structuredClone(request.operation === "projection_check" ? receipt : upgrade);
    } },
  };
  assert.equal(calls.length, 0);
  const checked = await checkProjection(host, projection);
  assert.deepEqual(calls[0], { operation: "projection_check", projection_node: "projection-old", selected_nodes: ["old-a", "old-b", "old-c"] });
  assert.equal(Object.hasOwn(calls[0], "expected_receipt_sha256"), false);
  const made = await reprojectProjection(host, projection, checked);
  assert.deepEqual(calls[1], { operation: "projection_reproject", projection_node: "projection-old", selected_nodes: ["old-a", "old-b", "old-c"], expected_receipt_sha256: receipt.old_receipt_sha256 });
  assert.deepEqual(made, upgrade);
  assert.deepEqual(fresh, config);

  const request = calls[0], writeRequest = calls[1];
  const refused = { ...receipt, current_replay: "refused", refusals: [{ node_id: "old-a", code: "conflict" }] };
  assert.equal(readProjectionReview(refused, request, projection).current_replay, "refused");
  const empty = { ...projection, occurrence_nodes: [] };
  assert.equal(readProjectionReview({ ...receipt, current_replay: "not_exercised" }, projectionRequest(empty, []), empty).current_replay, "not_exercised");
  for (const patch of [
    { schema: "loom.projection_upgrade/2" }, { saved_integrity: "unchecked" }, { current_replay: "unknown" }, { current_replay: ["compatible"] },
    { old_projection: "different" }, { old_receipt_sha256: "bad" }, { selection_scope: "conversation" },
    { selection_changed: true }, { extra: "unrecognized" }, { current_replay: "not_exercised" },
    { current_replay: "refused" }, { refusals: [{ node_id: "foreign", code: "conflict" }] },
    { refusals: [{ node_id: "old-a", code: "conflict" }] },
  ]) assert.throws(() => readProjectionReview({ ...receipt, ...patch }, request, projection), /projection_review_invalid/);
  assert.throws(() => readProjectionReview(receipt, projectionRequest(empty, []), empty), /projection_review_invalid/);
  assert.throws(() => readProjectionReview({ ...refused, refusals: [...refused.refusals, ...refused.refusals] }, request, projection), /projection_review_invalid/);
  for (const patch of [
    { old_receipt_sha256: "d".repeat(64) }, { new_receipt_sha256: "invalid" }, { mapping: [] },
    { mapping: [...upgrade.mapping, upgrade.mapping[0]] }, { activation: "automatic" }, { rollback: "lossless" },
    { mapping: upgrade.mapping.map(m => ({ ...m, old_node: "foreign" })) },
    { mapping: upgrade.mapping.map(m => ({ ...m, status: "equivalent" })) },
    { mapping: upgrade.mapping.map(m => ({ ...m, candidates: ["new-a", "new-a"] })) },
  ]) assert.throws(() => readProjectionReview({ ...upgrade, ...patch }, writeRequest, projection), /projection_review_invalid/);
  const target = { ...projection, source_id: "source-new", projection_node: "projection-new", occurrence_nodes: ["new-a", "new-b", "new-c"] };
  validateUpgradeTarget(made, target);
  assert.throws(() => validateUpgradeTarget(made, { ...target, occurrence_nodes: ["new-a"] }), /projection_review_invalid/);
  assert.throws(() => validateUpgradeTarget(made, { ...target, source_id: "wrong-source" }), /projection_review_invalid/);
  for (const selected of [["foreign"], ["old-a", "old-a"]]) assert.throws(() => projectionRequest(projection, selected), /projection_selection_not_owned/);
  assert.throws(() => projectionRequest({ ...projection, method: { format: "zip" } }, []), /archive_projection_upgrade_unsupported/);
  assert.throws(() => projectionRequest({ ...projection, coordinate: "archive_members" }, []), /archive_projection_upgrade_unsupported/);

  // Even a change outside this projection must be reviewed afresh, rather than silently accepted.
  fresh.context_execution.unified.legacy_seed_ids.push("another-projection-node");
  await assert.rejects(reprojectProjection(host, projection, checked), /projection_review_selection_stale/);
  assert.equal(calls.length, 2);
  fresh = structuredClone(config);
  fresh.model = "another-model";
  await reprojectProjection(host, projection, checked); // Unrelated settings do not invalidate the selection snapshot.
  assert.equal(fresh.model, "another-model");
  assert.deepEqual(fresh.context_execution, config.context_execution);

  let sent = 0;
  const before = new AbortController(); before.abort();
  await assert.rejects(checkProjection({ getConfig() { throw Error("must not read"); } }, projection, before.signal), /projection_review_aborted/);
  const duringRead = new AbortController();
  await assert.rejects(checkProjection({
    async getConfig() { duringRead.abort(); return config; },
    actions: { async clientBlobText() { sent++; return receipt; } },
  }, projection, duringRead.signal), /projection_review_aborted/);
  assert.equal(sent, 0);
  const duringAction = new AbortController();
  await assert.rejects(checkProjection({
    async getConfig() { return config; },
    actions: { async clientBlobText() { sent++; duringAction.abort(); return receipt; } },
  }, projection, duringAction.signal), /projection_review_aborted/);
  assert.equal(sent, 1);
  await assert.rejects(checkProjection({ async getConfig() { return config; } }, projection), /projection_review_transport_unavailable/);
  await assert.rejects(checkProjection({ ...host, actions: { async clientBlobText() { throw Error("native_refusal"); } } }, projection), /native_refusal/);
});
