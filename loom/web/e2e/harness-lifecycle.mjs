// Test infrastructure only: do not accept an early successful exit as a suite
// result, and keep asynchronous cleanup observable until it completes or fails.
import assert from "node:assert/strict";

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
