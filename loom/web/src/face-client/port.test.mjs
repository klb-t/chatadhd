// Mechanism fixtures only; no model, backend, rendering or Python benchmark.
// node loom/web/src/face-client/sanity.cjs (existing system Babel; no installation)
import { test } from "node:test";
import assert from "node:assert/strict";
const { createFaceClientPort, faceParameterLibrary, faceParams } = await import(process.env.FACE_PORT_MODULE ?? new URL("./port.ts", import.meta.url).href);

const vector = [0.1, -0.2, 0.3, -0.4, 0.5, 0.6, -0.7, 0.8, 0.9];
const entry = () => ({ id: "fixture", version: "1", label: { pl: "Atrapa", en: "Fixture" }, params: [...vector] });
const flush = () => new Promise(resolve => queueMicrotask(resolve));
function fixture() {
  const sent = [];
  let receive;
  let closes = 0;
  let connects = 0;
  const port = createFaceClientPort(callback => {
    connects++;
    receive = callback;
    return { post: event => sent.push(event), close: () => { closes++; } };
  });
  const display = (event, evidence = { evidenceClass: "observed", observationRef: "fixture-observation" }) =>
    ({ ...event, kind: "display", explicit9Params: vector, evidence });
  return { port, sent, receive: event => receive(event), display, closes: () => closes, connects: () => connects };
}

test("unconfigured backend stays disabled/NOTRUN with explicit local preview only", async () => {
  const port = createFaceClientPort();
  assert.equal(port.getSnapshot().explicit, null);
  port.contextChanged();
  port.select(entry());
  await flush();
  assert.equal(port.getSnapshot().backend, "disabled");
  assert.equal(port.getSnapshot().run, "NOTRUN");
  assert.deepEqual(port.getSnapshot().explicit.params, vector);
  assert.equal(port.getSnapshot().display, null);
  port.dispose();
});

test("nine-coordinate domain, no coercion, missing values or hidden neutral", () => {
  for (const bad of [[], vector.slice(1), [...vector, 0], vector.map(String), vector.map(() => NaN),
    vector.map(() => Infinity), vector.map(() => 2), vector.map(() => -0.1)]) assert.throws(() => faceParams(bad));
  const sparse = Array(9); sparse[0] = 0;
  assert.throws(() => faceParams(sparse));
  assert.deepEqual(faceParams(vector), vector);
  assert.throws(() => faceParameterLibrary([entry(), entry()]));
  assert.throws(() => faceParameterLibrary([{ ...entry(), label: { pl: "Atrapa" } }]));
  assert.deepEqual(faceParameterLibrary([]), []);
});

test("one connection, one coalesced transport slot; allowlist strips text and labels", async () => {
  const f = fixture();
  f.port.contextChanged();
  const input = { ...entry(), conversationText: "PRIVATE_FIXTURE", conversationId: "PRIVATE_ID" };
  for (let i = 0; i < 50; i++) f.port.select(input);
  input.params[0] = 1;
  input.label.pl = "changed";
  await flush();
  assert.equal(f.connects(), 1);
  assert.equal(f.sent.length, 1);
  const event = f.sent[0];
  assert.deepEqual(Object.keys(event).sort(), ["contextId", "evidence", "explicit9Params", "kind", "version"]);
  assert.equal(JSON.stringify(event).includes("PRIVATE"), false);
  assert.equal(JSON.stringify(event).includes("label"), false);
  assert.equal(event.explicit9Params[0], vector[0]);
  assert.equal(f.port.getSnapshot().explicit.label.pl, "Atrapa");
  assert.ok(Object.isFrozen(event));
  assert.ok(Object.isFrozen(event.explicit9Params));
  f.port.dispose();
});

test("A→B→A rejects delayed context and parameter epochs, including before post", async () => {
  const f = fixture();
  f.port.contextChanged(); f.port.select(entry()); await flush();
  const first = f.sent.at(-1);
  f.port.contextChanged(); f.port.select(entry()); await flush();
  const second = f.sent.at(-1);
  f.port.contextChanged(); f.port.select(entry());
  f.receive(f.display(first)); f.receive(f.display(second));
  assert.equal(f.port.getSnapshot().display, null);
  await flush();
  const third = f.sent.at(-1);
  f.receive(f.display({ ...third, contextId: first.contextId }));
  assert.equal(f.port.getSnapshot().display, null);
  f.receive(f.display(third));
  assert.equal(f.port.getSnapshot().run, "received");
  f.port.select(entry());
  f.receive(f.display(third));
  assert.equal(f.port.getSnapshot().display, null);
  await flush();
  f.receive(f.display(f.sent.at(-1)));
  assert.equal(f.port.getSnapshot().run, "received");
  f.port.dispose();
});

test("new connection has distinct identity; callbacks after dispose and queued posts are inert", async () => {
  const old = fixture(), fresh = fixture();
  old.port.contextChanged(); old.port.select(entry()); await flush();
  fresh.port.contextChanged(); fresh.port.select(entry()); await flush();
  assert.equal(old.sent[0].version, fresh.sent[0].version);
  assert.notEqual(old.sent[0].contextId, fresh.sent[0].contextId);
  fresh.receive(fresh.display(old.sent[0]));
  assert.equal(fresh.port.getSnapshot().display, null);
  let notifications = 0;
  old.port.subscribe(() => notifications++);
  old.port.select(entry());
  const before = notifications;
  old.port.dispose(); old.port.dispose();
  old.receive(old.display(old.sent[0]));
  old.port.contextChanged(); old.port.select(entry());
  await flush();
  assert.equal(old.closes(), 1);
  assert.equal(old.sent.length, 1);
  assert.equal(notifications, before);
  assert.equal(old.port.getSnapshot().explicit, null);
  fresh.port.dispose();
});

test("backend inference remains inference with unknown confidence; explicit preview stays original", async () => {
  const f = fixture();
  f.port.contextChanged(); f.port.select(entry()); await flush();
  const inference = { evidenceClass: "inferred", methodRef: "fixture-method", premiseRefs: ["fixture-premise"],
    expectedPropertyRef: "fixture-property", confidence: null, text: "PRIVATE_FIXTURE" };
  const event = f.display(f.sent[0], inference);
  event.explicit9Params = vector.map(() => 0);
  f.receive(event);
  inference.premiseRefs.push("changed");
  const snapshot = f.port.getSnapshot();
  assert.equal(snapshot.display.evidence.evidenceClass, "inferred");
  assert.equal(snapshot.display.evidence.confidence, null);
  assert.deepEqual(snapshot.display.evidence.premiseRefs, ["fixture-premise"]);
  assert.equal(JSON.stringify(snapshot.display).includes("PRIVATE"), false);
  assert.deepEqual(snapshot.explicit.params, vector);
  f.port.dispose();
});

test("malformed current backend evidence fails closed without exception payload", async () => {
  const f = fixture();
  f.port.contextChanged(); f.port.select(entry()); await flush();
  f.receive(f.display(f.sent[0], { evidenceClass: "inferred", confidence: 1 }));
  assert.equal(f.port.getSnapshot().backend, "failed");
  assert.equal(f.port.getSnapshot().run, "error");
  assert.equal(f.port.getSnapshot().display, null);
  f.port.dispose();
});

test("connect/post failures are sanitized; cleanup closes the host once", async () => {
  const failed = createFaceClientPort(() => { throw Error("PRIVATE_FIXTURE"); });
  assert.equal(failed.getSnapshot().backend, "failed");
  assert.equal(JSON.stringify(failed.getSnapshot()).includes("PRIVATE"), false);
  failed.dispose();
  let closes = 0;
  const port = createFaceClientPort(() => ({ post() { throw Error("PRIVATE_FIXTURE"); }, close() { closes++; throw Error("PRIVATE_FIXTURE"); } }));
  port.contextChanged(); port.select(entry()); await flush();
  assert.equal(port.getSnapshot().backend, "failed");
  assert.equal(JSON.stringify(port.getSnapshot()).includes("PRIVATE"), false);
  port.dispose(); port.dispose();
  assert.equal(closes, 1);
});
