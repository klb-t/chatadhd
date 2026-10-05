// Offline controller tests. The fixture adapter is not a production API or a
// replacement for native privacy/graph acceptance tests.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { draftValue, mayAsk, OnboardingController, parseDraft } from "./controller.ts";
import { nativeDispatchAction, normalizeNativeSnapshot } from "./native-snapshot.ts";

const recorded = JSON.parse(await readFile(new URL("./recorded-interview.json", import.meta.url), "utf8"));
const copy = (value) => structuredClone(value);
function snapshot() {
  return {
    scenario: {
      id: "test-method", title: "Offline test", method_ref: "method:test/1", source: { synthetic: true },
      sections: [{ id: "work", title: "Work", fields: [
        { id: "project", label: "Project", category: "work" },
        { id: "style", label: "Style", category: "preferences" },
        { id: "declined", label: "Declined field", category: "private" },
        { id: "never", label: "Excluded field", category: "private" },
      ] }],
    },
    fields: {
      project: { status: "unknown", category: "work" },
      style: { status: "unknown", category: "preferences" },
      declined: { status: "declined", category: "private", value: "not for the model" },
      never: { status: "never", category: "private", value: "also not for the model" },
    },
    session: { status: "active", section: "work", sections: { work: "active" } },
    candidates: {}, history: [], privacy: [], settings: { preference_mode: "ask" },
  };
}
function fixtureAdapter() {
  let current = snapshot();
  const calls = [];
  return {
    calls,
    getSnapshot: async () => copy(current),
    dispatch: async (action) => {
      calls.push(copy(action));
      if (action.op === "answer") current.candidates[action.id] = { ...action, review: "pending", section: "work" };
      if (action.op === "review") {
        const candidate = current.candidates[action.id];
        candidate.review = action.decision;
        if (action.decision === "confirmed") current.fields[candidate.field] = {
          ...current.fields[candidate.field], status: "known", value: action.value ?? candidate.value,
          provenance: candidate.provenance, review: action.decision,
        };
      }
      if (action.op === "status") current.fields[action.field].status = action.status;
      if (action.op === "pause" || action.op === "resume") current.session.status = action.op === "pause" ? "paused" : "active";
      if (action.op === "confirm_section") current.session.sections[action.section] = action.decision;
      if (action.op === "repeat") current.session.section = action.section;
      current.history.push(copy(action));
      return copy(current);
    },
    modelRequest: async (provider) => {
      calls.push({ op: "model_request", provider });
      // This deterministic stand-in represents a native-filtered request. It
      // intentionally excludes declined/never/private profile bytes.
      return { method_ref: "method:test/1", section: "work", prompt: "saved prompt", questions: ["project", "style"], context: { work: "synthetic" }, reply_schema: {} };
    },
    completeModelRequest: async (request) => { calls.push({ op: "transport", request: copy(request) }); return copy(recorded); },
    ingestModelReply: async (reply) => {
      calls.push({ op: "ingest" });
      current.latest_reply = copy(reply);
      current.session.summary = reply.summary;
      for (const candidate of reply.candidates) current.candidates[candidate.id] = { ...candidate, provenance: "model_inferred", review: "pending", section: reply.section };
      return copy(current);
    },
  };
}
function deferred() {
  let resolve;
  const promise = new Promise((done) => { resolve = done; });
  return { promise, resolve };
}

test("recorded conversational interview stages user-stated and inferred candidates, then confirms a section", async () => {
  const adapter = fixtureAdapter();
  const controller = new OnboardingController(adapter);
  await controller.load();
  await controller.dispatch({ op: "answer", field: "project", value: "synthetic project", provenance: "user_stated", id: "user-1", time: "2026-10-04T00:00:00Z", source_refs: [] });
  assert.equal(controller.state().snapshot.fields.project.status, "unknown");
  await controller.dispatch({ op: "review", id: "user-1", decision: "confirmed" });
  assert.equal(controller.state().snapshot.fields.project.provenance, "user_stated");
  await controller.requestModel("synthetic-provider");
  assert.equal(controller.state().snapshot.latest_reply.summary, recorded.summary);
  assert.equal(controller.state().snapshot.candidates["model-style-1"].review, "pending");
  await controller.dispatch({ op: "review", id: "model-style-1", decision: "confirmed", value: "brief" });
  assert.equal(controller.state().snapshot.fields.style.value, "brief");
  assert.equal(controller.state().snapshot.fields.style.provenance, "model_inferred");
  await controller.dispatch({ op: "confirm_section", section: "work", decision: "confirmed" });
  assert.equal(controller.state().snapshot.session.sections.work, "confirmed");
  assert.equal(controller.state().busy, false);
});

test("form and conversational answers use the same native candidate/review path", async () => {
  const adapter = fixtureAdapter();
  const controller = new OnboardingController(adapter);
  await controller.dispatch({ op: "answer", field: "project", value: "form project", provenance: "form", id: "form-1", time: "2026-10-04T00:00:00Z", source_refs: [] });
  await controller.dispatch({ op: "review", id: "form-1", decision: "confirmed" });
  assert.equal(controller.state().snapshot.fields.project.provenance, "form");
  assert.equal(controller.state().snapshot.fields.project.value, "form project");
});

test("declined and never fields are not interview questions; only prepared native request reaches transport", async () => {
  const adapter = fixtureAdapter();
  const controller = new OnboardingController(adapter);
  await controller.load();
  assert.equal(mayAsk(controller.state().snapshot, "declined"), false);
  assert.equal(mayAsk(controller.state().snapshot, "never"), false);
  assert.equal(mayAsk(controller.state().snapshot, "missing"), false);
  assert.equal(mayAsk(controller.state().snapshot, "style"), true);
  await controller.requestModel("synthetic-provider");
  const transport = adapter.calls.find((call) => call.op === "transport");
  assert.deepEqual(transport.request.context, { work: "synthetic" });
  assert.equal(JSON.stringify(transport).includes("not for the model"), false);
  assert.equal(adapter.calls.find((call) => call.op === "model_request").provider, "synthetic-provider");
});

test("a policy change invalidates an in-flight model result even when transport ignores abort", async () => {
  const adapter = fixtureAdapter();
  const transportStarted = deferred();
  const transportReply = deferred();
  let signal;
  adapter.completeModelRequest = async (_request, options) => { signal = options.signal; transportStarted.resolve(); return transportReply.promise; };
  const controller = new OnboardingController(adapter);
  await controller.load();
  const running = controller.requestModel("synthetic-provider");
  await transportStarted.promise;
  await controller.dispatch({ op: "status", field: "style", status: "never" });
  assert.equal(signal.aborted, true);
  transportReply.resolve(copy(recorded));
  await running;
  assert.equal(adapter.calls.some((call) => call.op === "ingest"), false);
  assert.equal(controller.state().snapshot.fields.style.status, "never");
  assert.equal(controller.state().busy, false);
});

test("closing the view suppresses a late prepared request and any provider call", async () => {
  const preparationStarted = deferred();
  const preparation = deferred();
  const adapter = fixtureAdapter();
  adapter.modelRequest = async () => { preparationStarted.resolve(); return preparation.promise; };
  const controller = new OnboardingController(adapter);
  const running = controller.requestModel("synthetic-provider");
  await preparationStarted.promise;
  controller.dispose();
  preparation.resolve({ method_ref: "test", section: "work", prompt: "", questions: [], context: {}, reply_schema: {} });
  await running;
  assert.equal(adapter.calls.some((call) => call.op === "transport"), false);
});

test("concurrent snapshot loads and edits are serialized and retain the newest native state", async () => {
  const adapter = fixtureAdapter();
  const release = deferred();
  const started = deferred();
  adapter.getSnapshot = async () => { started.resolve(); await release.promise; return snapshot(); };
  const controller = new OnboardingController(adapter);
  const loading = controller.load();
  await started.promise;
  const edit = controller.dispatch({ op: "status", field: "style", status: "declined" });
  release.resolve();
  await Promise.all([loading, edit]);
  assert.equal(controller.state().snapshot.fields.style.status, "declined");
  assert.equal(controller.state().busy, false);
});

test("native errors are visible and do not poison later writes", async () => {
  const adapter = fixtureAdapter();
  let fail = true;
  const original = adapter.dispatch;
  adapter.dispatch = async (action) => { if (fail) { fail = false; throw new Error("native rejected write"); } return original(action); };
  const controller = new OnboardingController(adapter);
  assert.equal(await controller.dispatch({ op: "pause" }), undefined);
  assert.equal(controller.state().error, "native rejected write");
  await controller.dispatch({ op: "resume" });
  assert.equal(controller.state().error, null);
  assert.equal(controller.state().snapshot.session.status, "active");
  assert.equal(controller.state().busy, false);
});

test("pause, resume and repeat delegate to persistent native state", async () => {
  const adapter = fixtureAdapter();
  const controller = new OnboardingController(adapter);
  await controller.dispatch({ op: "pause" });
  assert.equal(controller.state().snapshot.session.status, "paused");
  await controller.dispatch({ op: "resume" });
  await controller.dispatch({ op: "repeat", section: "work" });
  assert.equal(controller.state().snapshot.session.status, "active");
  assert.equal(controller.state().snapshot.session.section, "work");
});

test("missing model/layer/scenario integration stays explicit, without a synthetic fallback", async () => {
  const base = fixtureAdapter();
  const controller = new OnboardingController({ getSnapshot: base.getSnapshot, dispatch: base.dispatch });
  await controller.requestModel("anything");
  assert.match(controller.state().error, /host has not connected/);
  await controller.dispatchLayer({ op: "exclude", key: "preferences.style" });
  assert.match(controller.state().error, /native default layers/);
  await controller.saveScenario({ prompt: "edited" });
  assert.match(controller.state().error, /graph-method editing/);
  assert.equal(base.calls.length, 0);
});

test("generic field parsing preserves types and accepts empty text as explicit input", () => {
  assert.equal(parseDraft(""), "");
  assert.equal(parseDraft("12.5", "number"), 12.5);
  assert.equal(parseDraft("false", "boolean"), false);
  assert.deepEqual(parseDraft('{"arbitrary":[1,null]}', "json"), { arbitrary: [1, null] });
  assert.throws(() => parseDraft('"1"', "number"), /Enter a number/);
  assert.equal(draftValue("text"), "text");
  assert.equal(draftValue(false, "boolean"), "false");
});

test("host request binding stays local and overrides any provider-generated token or provider ID", async () => {
  const adapter = fixtureAdapter();
  const prepare = adapter.modelRequest;
  adapter.modelRequest = async (provider) => ({ ...await prepare(provider), provider, request_token: "local-state-fingerprint",
    snapshot_revision: 9, graph_run_id: "native-graph-run", calls_authorized: false,
    method_profile: { shouldStayLocal: true }, future_private_envelope: "private" });
  let forwarded;
  let providerOptions;
  let ingested;
  adapter.completeModelRequest = async (request, options) => { forwarded = request; providerOptions = options; return { ...copy(recorded), provider: "forged", request_token: "forged", snapshot_revision: -1 }; };
  const ingest = adapter.ingestModelReply;
  adapter.ingestModelReply = async (reply) => { ingested = reply; return ingest(reply); };
  const controller = new OnboardingController(adapter);
  await controller.requestModel("chosen-provider");
  assert.equal("request_token" in forwarded, false);
  assert.equal("snapshot_revision" in forwarded, false);
  assert.equal("graph_run_id" in forwarded, false);
  assert.equal("calls_authorized" in forwarded, false);
  assert.equal("method_profile" in forwarded, false);
  assert.equal("future_private_envelope" in forwarded, false);
  assert.equal("provider" in forwarded, false);
  assert.equal(providerOptions.provider, "chosen-provider");
  assert.equal(ingested.request_token, "local-state-fingerprint");
  assert.equal(ingested.provider, "chosen-provider");
  assert.equal(ingested.snapshot_revision, 9);
  assert.equal(ingested.graph_run_id, "native-graph-run");
});

test("actual pack scenario and native snapshot projection preserve question order, privacy fields, layer keys and summaries", async () => {
  const scenario = JSON.parse(await readFile(new URL("../../../data/onboarding/scenario.pack", import.meta.url), "utf8"));
  const pack = JSON.parse(await readFile(new URL("../../../data/profiles/user.pack", import.meta.url), "utf8"));
  const privacy = pack.entries.find((entry) => entry.key === "onboarding.privacy").value;
  const style = pack.entries.find((entry) => entry.key === "preference.style");
  const native = {
    scenario_definition: scenario,
    profile: {
      fields: Object.fromEntries(scenario.fields.map((descriptor) => [descriptor.id, { ...descriptor, status: "unknown", value: null, provenance: null }])),
      privacy, settings: { preference_mode: "ask" }, candidates: {}, history: [],
      session: { status: "active", section: scenario.sections[0].id, sections: Object.fromEntries(scenario.sections.map((section) => [section.id, { status: "pending", summary: section.id === scenario.sections[0].id ? "retained native summary" : null }])) },
    },
    layers: { areas: { [style.area]: { new_defaults_mode: "proposal" } }, history: [{ op: "exclude", key: style.key }] },
    effectiveDefaults: [{ key: style.key, id: style.id, area: style.area, status: "excluded", layer: "user_exclusion", explanation: "Native exclusion reason", entity: style }],
  };
  const projected = normalizeNativeSnapshot(native);
  assert.deepEqual(projected.scenario.sections[0].fields.map((field) => field.id), scenario.sections[0].questions.map((question) => question.field));
  assert.equal(projected.scenario.sections[0].fields[0].question, scenario.sections[0].questions[0].text);
  assert.deepEqual(projected.scenario.source, scenario);
  assert.deepEqual(projected.privacy, privacy.rules);
  assert.equal(projected.session.summary, "retained native summary");
  assert.equal(projected.defaults[0].key, "preference.style");
  assert.equal(projected.defaults[0].id, "preference.style/v1");
  assert.equal(projected.defaults[0].area_mode, "proposal");
  assert.equal(projected.defaults[0].value, undefined);
  assert.equal(projected.defaults[0].reason, "Native exclusion reason");
  assert.deepEqual(native.profile.fields[scenario.fields[0].id].value, null);
});

test("native review envelope keeps candidate identity separate and native layer envelopes use keys", () => {
  const identity = { id: "event-1", time: "2026-10-04T00:00:00Z" };
  const review = nativeDispatchAction({ op: "review", id: "candidate-1", decision: "confirmed" }, identity);
  assert.equal(review.id, "event-1");
  assert.equal(review.candidate, "candidate-1");
  assert.equal(review.target, "profile");
  assert.equal(review.time, identity.time);
  const layer = nativeDispatchAction({ op: "override", key: "preference.style", value: "direct" }, identity);
  assert.equal(layer.key, "preference.style");
  assert.equal(layer.target, "layers");
  assert.equal(nativeDispatchAction({ op: "set_area_mode", area: "preferences", mode: "proposal" }, identity).target, "layers");
});

test("history-disabled users can inspect an ephemeral validated summary without changing the redacted native snapshot", async () => {
  const adapter = fixtureAdapter();
  adapter.ingestModelReply = async () => snapshot(); // Native retention redacts persisted summary.
  const controller = new OnboardingController(adapter);
  await controller.requestModel("chosen-provider");
  assert.equal(controller.state().snapshot.latest_reply, undefined);
  assert.equal(controller.state().snapshot.session.summary, undefined);
  assert.equal(controller.state().reply.summary, recorded.summary);
  assert.equal(controller.state().reply.questions[0].field, "style");
  await controller.dispatch({ op: "pause" });
  assert.equal(controller.state().reply, null);
});

test("pausing aborts the model immediately while its provider transport is pending", async () => {
  const adapter = fixtureAdapter();
  const transportStarted = deferred();
  const transportReply = deferred();
  let signal;
  adapter.completeModelRequest = async (_request, options) => { signal = options.signal; transportStarted.resolve(); return transportReply.promise; };
  const controller = new OnboardingController(adapter);
  await controller.load();
  const running = controller.requestModel("chosen-provider");
  await transportStarted.promise;
  assert.equal(controller.state().busy, true);
  await controller.dispatch({ op: "pause" });
  assert.equal(signal.aborted, true);
  assert.equal(controller.state().snapshot.session.status, "paused");
  await running;
  assert.equal(controller.state().busy, false); // Provider has not resolved yet.
  transportReply.resolve(copy(recorded));
  assert.equal(adapter.calls.some((call) => call.op === "ingest"), false);
});

test("known inferred values keep their independent declined/never question disposition", () => {
  const native = snapshot();
  native.fields.declined = { ...native.fields.declined, status: "known", question_disposition: "declined", provenance: "model_inferred" };
  native.fields.never = { ...native.fields.never, status: "known", question_disposition: "never" };
  assert.equal(mayAsk(native, "declined"), false);
  assert.equal(mayAsk(native, "never"), false);
});

test("a mutation queued during native reply ingestion cannot resurrect an ephemeral summary", async () => {
  const adapter = fixtureAdapter();
  const ingestStarted = deferred();
  const ingestResult = deferred();
  adapter.ingestModelReply = async () => { ingestStarted.resolve(); return ingestResult.promise; };
  const controller = new OnboardingController(adapter);
  const running = controller.requestModel("chosen-provider");
  await ingestStarted.promise;
  const statusChange = controller.dispatch({ op: "status", field: "style", status: "never" });
  ingestResult.resolve(snapshot());
  await Promise.all([running, statusChange]);
  assert.equal(controller.state().snapshot.fields.style.status, "never");
  assert.equal(controller.state().reply, null);
});
