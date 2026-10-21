// Test infrastructure only: do not accept an early successful exit as a suite
// result, and keep asynchronous cleanup observable until it completes or fails.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { request as httpRequest } from "node:http";

export function suiteCompletionGuard(suite, expectedGroups, groups) {
  let completed = false;
  process.on("exit", code => {
    if (completed) return;
    process.stderr.write(`[${suite}] INCOMPLETE: ${groups.length}/${expectedGroups} groups; suite cleanup/summary did not complete\n`);
    if (code === 0) process.exitCode = 1;
  });
  return {
    complete() {
      assert.equal(groups.length, expectedGroups, `${suite}: all declared groups must finish`);
      completed = true;
    },
  };
}

export function assertSuiteOutput({ suite, expectedGroups, stdout, exitCode, signal }) {
  assert.equal(signal, null, `${suite}: child terminated by ${signal}`);
  assert.equal(exitCode, 0, `${suite}: child failed with exit ${exitCode}`);
  const escaped = suite.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const passes = [...stdout.matchAll(new RegExp(`^\\[${escaped}\\] PASS `, "gm"))];
  const summaries = [...stdout.matchAll(new RegExp(`^\\[${escaped}\\] (\\d+)\\/(\\d+) groups passed(?:\\s|;|$)`, "gm"))];
  assert.equal(passes.length, expectedGroups, `${suite}: expected ${expectedGroups} PASS groups, received ${passes.length}`);
  assert.equal(summaries.length, 1, `${suite}: expected one final suite summary`);
  assert.equal(Number(summaries[0][1]), expectedGroups, `${suite}: incomplete summary numerator`);
  assert.equal(Number(summaries[0][2]), expectedGroups, `${suite}: changed summary denominator`);
}

export function closeHttpFixture(server, timeoutMs = 5000) {
  return new Promise((resolve, reject) => {
    const finish = error => {
      clearTimeout(timeout);
      if (error) reject(error); else resolve();
    };
    const timeout = setTimeout(() => finish(new Error("HTTP fixture cleanup did not complete")), timeoutMs);
    server.close(finish);
    // Browser requests are already finished; retained fixture sockets must not
    // prevent the native part of the suite from starting.
    server.closeAllConnections?.();
  });
}

export function stopChild(child, timeoutMs = 5000) {
  // A signal termination leaves exitCode null. Its close event may already
  // have fired, so waiting for a new event would never settle.
  if (!child || child.exitCode !== null || child.signalCode !== null || child.pid === undefined) return Promise.resolve();
  return new Promise((resolve, reject) => {
    const finish = error => {
      clearTimeout(timeout);
      child.removeListener("close", onClose);
      child.removeListener("error", finish);
      if (error) reject(error); else resolve();
    };
    const onClose = () => finish();
    const timeout = setTimeout(() => {
      child.kill("SIGKILL");
      finish(new Error("Native test server did not stop after SIGTERM"));
    }, timeoutMs);
    child.once("close", onClose);
    child.once("error", finish);
    child.kill("SIGTERM");
  });
}

/**
 * One HTTP request over a connection of its own ("Connection: close", no pool). Nothing is kept idle, so a socket the server has
 * already closed (its keep-alive timeout, advertised as "Keep-Alive: timeout=N") can never be picked for the request.
 * Plain fetch() pools idle sockets and drops them from an idle timer; when this process's event loop was blocked (spawnSync) for
 * longer than the server keeps them, that timer has not run, the next POST is written onto the closed socket and fails with
 * "fetch failed / other side closed" (UND_ERR_SOCKET); undici does not repeat a request that is not safe to repeat.
 * The answer has fetch's shape where the e2e suites use it: status, ok, headers (lower-case names), text, json().
 */
export function request(url, { method = "GET", headers = {}, body } = {}) {
  return new Promise((resolve, reject) => {
    const sent = { ...headers, Connection: "close" };
    if (body !== undefined) sent["Content-Length"] = Buffer.byteLength(body);
    const call = httpRequest(url, { method, headers: sent, agent: false }, response => {
      const chunks = [];
      response.on("data", chunk => chunks.push(chunk));
      response.once("error", reject);
      response.once("end", () => {
        const text = Buffer.concat(chunks).toString("utf8");
        resolve({ status: response.statusCode, ok: response.statusCode >= 200 && response.statusCode < 300, headers: response.headers, text, json: () => JSON.parse(text) });
      });
    });
    call.once("error", reject);
    call.end(body);
  });
}

/**
 * A child process run to its end WITHOUT blocking this process's event loop. spawnSync stops every timer and socket of the process
 * while it runs, so a few seconds of it are enough for the server to close an idle connection that this process still believes is open.
 * Resolves with { status, signal, stdout, stderr, timedOut }; a child that is still running after timeoutMs is killed (SIGKILL).
 */
export function runChild(command, args, { input = "", timeoutMs = 120000 } = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { stdio: ["pipe", "pipe", "pipe"] });
    let stdout = "", stderr = "", timedOut = false;
    const timer = setTimeout(() => { timedOut = true; child.kill("SIGKILL"); }, timeoutMs);
    child.stdout.setEncoding("utf8").on("data", chunk => { stdout += chunk; });
    child.stderr.setEncoding("utf8").on("data", chunk => { stderr += chunk; });
    child.stdin.on("error", error => { stderr += `stdin: ${error.message}\n`; });  // the child may exit before it reads its input
    child.once("error", error => { clearTimeout(timer); reject(error); });
    child.once("close", (status, signal) => { clearTimeout(timer); resolve({ status, signal, stdout, stderr: timedOut ? `${stderr}killed after ${timeoutMs} ms\n` : stderr, timedOut }); });
    child.stdin.end(input);
  });
}
