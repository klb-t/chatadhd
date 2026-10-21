#!/usr/bin/env node
// A connection the server has closed while the client was not looking (S1, "przerywane zerwanie połączenia przy logowaniu nowego konta").
//   loom-server keeps an idle connection for --keep-alive-timeout-s seconds (preset 5), says so on every answer ("Keep-Alive: timeout=N"),
//   and closes it then. A client drops its idle sockets from a timer of its own (undici: the advertised time minus 2 s). If its event
//   loop is stopped for longer than the server's time (first-steps-ui.mjs did: spawnSync(loom-server user add) for seconds, on a busy
//   machine more), the timer has not run, the next POST is written onto the closed socket and fails: "fetch failed / other side closed"
//   (UND_ERR_SOCKET); a request that is not safe to repeat is not repeated. Only a stopped loop does this: the server's close is a
//   normal, advertised event, and the same sequence with a running loop is fine.
// The server here runs with a lowered time (3 s; undici pools a connection only if the advertised time exceeds its own 2 s margin), so
// the whole test takes ~15 s.
//   1. the server advertises its time, keeps a connection for it and closes the idle one at it (a raw socket)
//   2. request() of the harness (a connection of its own) survives a loop stopped for longer than the server's time
//   3. a child run with runChild() (the loop keeps running) does not strand fetch() either
//   4. the control: fetch() after a stopped loop fails with UND_ERR_SOCKET, i.e. groups 2 and 3 are proofs, not coincidences
//
// Prereq: loom-server built (LOOM_BUILD_SERVER=ON). No browser, no provider.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import net from "node:net";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { request, runChild, stopChild, suiteCompletionGuard } from "./harness-lifecycle.mjs";

const loom = path.resolve(fileURLToPath(new URL("../", import.meta.url)), "..");
const serverBin = process.env.LOOM_SERVER_BIN || path.join(loom, "build/dev/server/loom-server");
assert.ok(existsSync(serverBin), "Build the native server first.");
const keepAliveSeconds = Number(process.env.STALE_KEEP_ALIVE_S || 3);
assert.ok(keepAliveSeconds >= 3, "undici keeps a connection only if the advertised time exceeds its 2 s margin; below 3 s there is nothing to strand");
const blockMs = keepAliveSeconds * 1000 + 600;  // longer than the server keeps an idle connection

const groups = [];
const complete = suiteCompletionGuard("stale-connection", 4, groups);
let server;
const root = mkdtempSync(path.join(tmpdir(), "loom-stale-connection-"));
let serverLog = "";

function freePort() {
  return new Promise((resolve, reject) => {
    const probe = net.createServer();
    probe.once("error", reject);
    probe.listen(0, "127.0.0.1", () => { const { port } = probe.address(); probe.close(() => resolve(port)); });
  });
}
async function check(name, run) { await run(); groups.push(name); console.log("[stale-connection] PASS " + name); }
/** The event loop of this process stopped (what spawnSync does), for ms. */
function stopLoop(ms) { Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms); }
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const json = { "Content-Type": "application/json" };

/** A raw HTTP/1.1 connection: send a GET, resolve with the head (status line + headers) once the whole answer is in. */
function rawGet(socket, pathname) {
  return new Promise((resolve, reject) => {
    let data = Buffer.alloc(0);
    const onData = chunk => {
      data = Buffer.concat([data, chunk]);
      const end = data.indexOf("\r\n\r\n");
      if (end < 0) return;
      const head = data.subarray(0, end).toString("latin1");
      const length = Number(/^content-length:\s*(\d+)/im.exec(head)?.[1] ?? 0);
      if (data.length < end + 4 + length) return;
      socket.off("data", onData); socket.off("error", reject);
      resolve({ head, body: data.subarray(end + 4, end + 4 + length).toString("utf8"), at: Date.now() });
    };
    socket.on("data", onData); socket.once("error", reject);
    socket.write(`GET ${pathname} HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n`);
  });
}

try {
  const port = await freePort();
  const base = "http://127.0.0.1:" + port;
  server = spawn(serverBin, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", path.join(root, "data"), "--keep-alive-timeout-s", String(keepAliveSeconds)],
    { cwd: loom, stdio: ["ignore", "pipe", "pipe"] });
  for (const out of [server.stdout, server.stderr]) out.on("data", chunk => { serverLog += chunk.toString(); });
  for (let waited = 0; ; waited += 100) {
    const up = await request(base + "/api/healthz").then(answer => answer.ok, () => false);
    if (up) break;
    assert.ok(waited < 15000 && server.exitCode === null, "the server did not come up: " + serverLog.slice(-400));
    await sleep(100);
  }

  await check(`the server advertises ${keepAliveSeconds} s, keeps a connection for it and closes the idle one at it`, async () => {
    const socket = net.connect(port, "127.0.0.1");
    await new Promise((resolve, reject) => { socket.once("connect", resolve); socket.once("error", reject); });
    try {
      const closed = new Promise(resolve => { socket.once("close", () => resolve(Date.now())); });
      let socketError = null;
      socket.on("error", error => { socketError = error; });  // a reset after the server's close is also "closed"; kept for the message below
      const first = await rawGet(socket, "/api/healthz");
      assert.match(first.head, /^HTTP\/1\.1 200 /);
      assert.match(first.head, new RegExp(`^Keep-Alive: timeout=${keepAliveSeconds}\\b`, "im"), "the time is advertised: " + first.head);
      await sleep(500);
      const second = await rawGet(socket, "/api/healthz");  // within the time the connection is kept
      assert.match(second.head, /^HTTP\/1\.1 200 /, "a second request on the same connection, within the time");
      const closedAt = await Promise.race([closed, sleep(keepAliveSeconds * 1000 + 5000).then(() => 0)]);
      assert.ok(closedAt > 0, "the server did not close the idle connection within " + (keepAliveSeconds + 5) + " s" + (socketError ? " (" + socketError.message + ")" : ""));
      const idle = closedAt - second.at;
      assert.ok(idle >= keepAliveSeconds * 1000 - 300 && idle <= keepAliveSeconds * 1000 + 2500, `closed after ${idle} ms of idleness, expected about ${keepAliveSeconds * 1000}`);
    } finally { socket.destroy(); }
  });

  await check("a loop stopped for longer than the server keeps a connection: request() (a connection of its own) still gets through", async () => {
    assert.equal((await request(base + "/api/healthz")).status, 200);
    stopLoop(blockMs);
    const made = await request(base + "/api/conversations", { method: "POST", headers: json, body: JSON.stringify({ title: "after a stopped loop" }) });
    assert.equal(made.status, 201, made.text);
  });

  await check("a child run without stopping the loop (runChild) does not strand fetch() either", async () => {
    await (await fetch(base + "/api/healthz")).text();
    const waited = await runChild(process.execPath, ["-e", `setTimeout(() => {}, ${blockMs})`]);
    assert.equal(waited.status, 0, waited.stderr);
    const made = await fetch(base + "/api/conversations", { method: "POST", headers: json, body: JSON.stringify({ title: "after a running loop" }) });
    const text = await made.text();
    assert.equal(made.status, 201, text);
  });

  await check("control: fetch() after a stopped loop fails with 'other side closed' (UND_ERR_SOCKET), so the groups above prove something", async () => {
    await (await fetch(base + "/api/healthz")).text();
    stopLoop(blockMs);
    await assert.rejects(fetch(base + "/api/conversations", { method: "POST", headers: json, body: JSON.stringify({ title: "stranded" }) }),
      error => { assert.equal(error.message, "fetch failed"); assert.equal(error.cause?.code, "UND_ERR_SOCKET", String(error.cause)); return true; });
  });

  complete.complete();
  console.log(`[stale-connection] ${groups.length}/4 groups passed`);
} finally {
  await stopChild(server).catch(() => undefined);
  rmSync(root, { recursive: true, force: true });
}
