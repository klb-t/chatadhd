#!/usr/bin/env node
// Standard-library regressions; no native binary, browser or provider required.
import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { once } from "node:events";
import { createServer } from "node:http";
import { connect, createServer as createTcpServer } from "node:net";
import { assertSuiteOutput, closeHttpFixture, request, stopChild, suiteCompletionGuard } from "./harness-lifecycle.mjs";

const moduleUrl = new URL("./harness-lifecycle.mjs", import.meta.url).href;
const results = [];
const completion = suiteCompletionGuard("harness-lifecycle", 12, results);
async function check(name, run) {
  await run(); results.push(name); console.log(`[harness-lifecycle] PASS ${name}`);
}
function guarded(source) {
  return spawnSync(process.execPath, ["--input-type=module", "-e",
    `import {suiteCompletionGuard} from ${JSON.stringify(moduleUrl)}; const groups=[]; const guard=suiteCompletionGuard("regression",2,groups); ${source}`],
  { encoding: "utf8", timeout: 5000 });
}
await check("early exit0 is rejected even when the caller exits explicitly", async () => {
  const child = guarded('groups.push("one"); process.exit(0);');
  assert.ifError(child.error); assert.equal(child.status, 1);
  assert.match(child.stderr, /INCOMPLETE: 1\/2 groups/);
});
await check("partial declared count is rejected before completion", async () => {
  const child = guarded('groups.push("one"); guard.complete();');
  assert.ifError(child.error); assert.notEqual(child.status, 0);
  assert.match(child.stderr, /all declared groups must finish/);
  assert.match(child.stderr, /INCOMPLETE: 1\/2 groups/);
});
await check("only exact count and explicit final completion can exit0", async () => {
  const child = guarded('groups.push("one","two"); guard.complete();');
  assert.ifError(child.error); assert.equal(child.status, 0); assert.equal(child.stderr, "");
});
await check("unsettled cleanup cannot produce a successful suite result", async () => {
  const child = guarded('groups.push("one","two"); await new Promise(()=>{});');
  assert.ifError(child.error); assert.notEqual(child.status, 0);
  assert.match(child.stderr, /INCOMPLETE: 2\/2 groups/);
});
await check("historical seven-group output and exit0 fail the native gate", async () => {
  const stdout = Array.from({ length: 7 }, (_, i) => `[operations-panel] PASS fixture ${i}`).join("\n") + "\n";
  assert.throws(() => assertSuiteOutput({ suite: "operations-panel", expectedGroups: 8, stdout, exitCode: 0, signal: null }), /expected 8 PASS groups, received 7/);
  assert.throws(() => assertSuiteOutput({ suite: "operations-panel", expectedGroups: 8, stdout: stdout + "[operations-panel] 7/7 groups passed; fixtures\n", exitCode: 0, signal: null }), /expected 8 PASS groups/);
});
await check("full count still requires the declared final denominator and summary", async () => {
  const passes = Array.from({ length: 8 }, (_, i) => `[operations-panel] PASS fixture ${i}`).join("\n") + "\n";
  const invoke = stdout => assertSuiteOutput({ suite: "operations-panel", expectedGroups: 8, stdout, exitCode: 0, signal: null });
  assert.throws(() => invoke(passes), /one final suite summary/);
  assert.throws(() => invoke(passes + "[operations-panel] 8/7 groups passed\n"), /changed summary denominator/);
  assert.doesNotThrow(() => invoke(passes + "[operations-panel] 8/8 groups passed; actual native fixture\n"));
});
await check("native exit and signal failures cannot be masked by complete-looking output", async () => {
  const stdout = Array.from({ length: 4 }, (_, i) => `[usage-policy-native] PASS fixture ${i}`).join("\n") + "\n[usage-policy-native] 4/4 groups passed\n";
  assert.throws(() => assertSuiteOutput({ suite: "usage-policy-native", expectedGroups: 4, stdout, exitCode: 1, signal: null }), /child failed with exit 1/);
  assert.throws(() => assertSuiteOutput({ suite: "usage-policy-native", expectedGroups: 4, stdout, exitCode: null, signal: "SIGTERM" }), /child terminated by SIGTERM/);
});
await check("HTTP cleanup closes an active fixture socket before native execution", async () => {
  const server = createServer((_request, response) => { response.writeHead(200); response.write("fixture response remains open"); });
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  const client = connect(server.address().port, "127.0.0.1");
  try {
    await once(client, "connect");
    client.write("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    await once(client, "data");
    const closed = once(client, "close");
    await closeHttpFixture(server); await closed;
    assert.equal(server.listening, false); assert.equal(client.destroyed, true);
  } finally { client.destroy(); if (server.listening) await closeHttpFixture(server); }
});
await check("missing HTTP close callback fails with a referenced deadline", async () => {
  let forced = false;
  await assert.rejects(closeHttpFixture({ close() {}, closeAllConnections() { forced = true; } }, 20), /cleanup did not complete/);
  assert.equal(forced, true);
});
await check("already signal-closed child does not wait for a second close event", async () => {
  const child = spawn(process.execPath, ["-e", "setInterval(()=>{},1000)"]);
  await once(child, "spawn");
  const closed = once(child, "close"); child.kill("SIGTERM"); await closed;
  assert.equal(child.exitCode, null); assert.equal(child.signalCode, "SIGTERM");
  await stopChild(child);
});
await check("live native-style child shutdown waits for the close event", async () => {
  const child = spawn(process.execPath, ["-e", "setInterval(()=>{},1000)"]);
  await once(child, "spawn"); await stopChild(child);
  assert.equal(child.signalCode, "SIGTERM");
});
await check("request() fails with its deadline on a server that accepts and never answers, and a prompt answer is not delayed", async () => {
  const silent = createTcpServer(socket => { socket.on("data", () => {}); socket.on("error", () => {}); });  // reads, never replies
  await new Promise(resolve => silent.listen(0, "127.0.0.1", resolve));
  const prompt = createServer((_request, response) => { response.writeHead(200, { "Content-Type": "application/json" }); response.end('{"ok":true}'); });
  await new Promise(resolve => prompt.listen(0, "127.0.0.1", resolve));
  try {
    const url = `http://127.0.0.1:${silent.address().port}/never`;
    const started = Date.now();
    await assert.rejects(request(url, { method: "POST", body: "{}", timeoutMs: 300 }), error => {
      assert.match(error.message, /^request timed out after 300 ms: POST http:\/\/127\.0\.0\.1:\d+\/never$/);
      return true;
    });
    assert.ok(Date.now() - started < 3000, "gave up at the deadline, not at some library default");
    const answer = await request(`http://127.0.0.1:${prompt.address().port}/ok`, { timeoutMs: 5000 });
    assert.equal(answer.status, 200); assert.deepEqual(answer.json(), { ok: true });
    assert.equal(answer.headers.connection, "close", "one connection per call");
  } finally {
    silent.close(); silent.closeAllConnections?.();
    await closeHttpFixture(prompt);
  }
});
completion.complete();
console.log(`[harness-lifecycle] ${results.length}/12 groups passed; standard library, zero provider calls`);
