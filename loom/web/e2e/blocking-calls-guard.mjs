#!/usr/bin/env node
// A suite that stops its own event loop (spawnSync, execFileSync, execSync, Atomics.wait) and also talks to a server with the global
// fetch() can fail now and then with "fetch failed / other side closed" (S1): fetch() pools idle sockets and drops them from a timer,
// the stopped loop does not run the timer, and the server (loom-server closes an idle connection after its Keep-Alive time, preset 5 s)
// has closed the pooled one meanwhile; a POST written onto it is not repeated. Use runChild() and request() of harness-lifecycle.mjs
// instead (createAccount() for accounts), or keep every blocking call out of the time a server is up.
// This guard reads the source of every e2e/ and tests/ file (the syntax tree, so comments, strings and the browser bundles that suites
// keep in template literals are not mistaken for calls) and fails when a file has both kinds of call and is not listed below with
// the reason it is safe. The list is checked from the other side too: an entry that no longer mixes the two must go.
//
// Prereq: none (typescript from devDependencies). No server, no browser.
import assert from "node:assert/strict";
import { readdirSync, readFileSync, statSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import ts from "typescript";
import { suiteCompletionGuard } from "./harness-lifecycle.mjs";

const web = fileURLToPath(new URL("../", import.meta.url));
const BLOCKING = new Set(["spawnSync", "execFileSync", "execSync"]);

/** Files that mix the two on purpose, and why that is safe. Relative to loom/web, forward slashes. */
const ALLOWED = {
  "e2e/stale-connection.mjs": "the control group strands fetch() on purpose (stopLoop) to prove that the other groups prove something",
  "e2e/app-native-navigation.mjs": "its spawnSync calls (git rev-parse, git status) run at the top, before the server is started and before the first fetch()",
  "e2e/conversation-view-native.mjs": "its one spawnSync (building the fixture zip) runs before the server is started and before the first fetch()",
  "e2e/onboarding-native.mjs": "its spawnSync seeds the database of a STOPPED server (seedOuterRevision, after stopServer()); the restarted server is probed in a loop (ready()) before the next POST",
  "e2e/operations-panel.mjs": "its one spawnSync (packet validator) runs in a group that ends before the native server of the last group is started; the page's own fetch() calls are inside a browser bundle",
};

/** The calls of one source text that can stop the loop, and the calls of the global fetch(). Lines are 1-based. */
function scan(text, fileName = "x.mjs") {
  const source = ts.createSourceFile(fileName, text, ts.ScriptTarget.Latest, true);
  const blocking = [], fetching = [];
  const line = node => source.getLineAndCharacterOfPosition(node.getStart(source)).line + 1;
  const visit = node => {
    if (ts.isCallExpression(node)) {
      const callee = node.expression;
      if (ts.isIdentifier(callee)) {
        if (BLOCKING.has(callee.text)) blocking.push({ call: callee.text, line: line(node) });
        else if (callee.text === "fetch") fetching.push({ call: "fetch", line: line(node) });
      } else if (ts.isPropertyAccessExpression(callee)) {
        const owner = ts.isIdentifier(callee.expression) ? callee.expression.text : "";
        if (BLOCKING.has(callee.name.text)) blocking.push({ call: owner + "." + callee.name.text, line: line(node) });
        else if (owner === "Atomics" && callee.name.text === "wait") blocking.push({ call: "Atomics.wait", line: line(node) });
        else if (callee.name.text === "fetch" && (owner === "globalThis" || owner === "window" || owner === "self")) fetching.push({ call: owner + ".fetch", line: line(node) });
      }
    }
    ts.forEachChild(node, visit);
  };
  visit(source);
  return { blocking, fetching, mixed: blocking.length > 0 && fetching.length > 0 };
}

function sources() {
  const found = [];
  const walk = directory => {
    for (const entry of readdirSync(directory)) {
      if (entry === "node_modules" || entry === "dist" || entry === "screenshots") continue;
      const full = path.join(directory, entry);
      if (statSync(full).isDirectory()) walk(full);
      else if (/\.(mjs|js)$/.test(entry)) found.push(full);
    }
  };
  for (const directory of ["e2e", "tests"]) walk(path.join(web, directory));
  return found.map(file => path.relative(web, file).split(path.sep).join("/")).sort();
}

const groups = [];
const complete = suiteCompletionGuard("blocking-calls-guard", 3, groups);
function check(name, run) { run(); groups.push(name); console.log("[blocking-calls-guard] PASS " + name); }

check("the scan sees what it must: calls, not comments, strings or browser bundles", () => {
  const mixed = scan(`import { spawnSync } from "node:child_process";\nspawnSync("a"); await fetch("http://x");`);
  assert.deepEqual(mixed.blocking, [{ call: "spawnSync", line: 2 }]);
  assert.deepEqual(mixed.fetching, [{ call: "fetch", line: 2 }]);
  assert.equal(mixed.mixed, true);
  assert.equal(scan(`cp.execFileSync("git"); globalThis.fetch(u);`).mixed, true, "member calls");
  assert.equal(scan(`Atomics.wait(a, 0, 0, 5); window.fetch(u);`).mixed, true, "Atomics.wait");
  assert.equal(scan(`execSync("x"); fetch(u);`).mixed, true);
  assert.equal(scan(`// spawnSync("x"); fetch(u)\n/* execSync(1); fetch(2) */ const a = 1;`).mixed, false, "comments");
  assert.equal(scan("const bundle = `const r = fetch(path); ${1}`; spawnSync('x');").mixed, false, "a browser bundle in a template literal is not a call of this process");
  assert.equal(scan(`const t = "fetch(u)"; spawnSync("x"); await page.request.fetch(u); context.request.fetch(u);`).mixed, false, "strings and the Playwright request API");
  assert.equal(scan(`spawnSync("x"); globalThis.fetch = async () => 1;`).mixed, false, "assigning a stub is not a call");
  assert.equal(scan(`spawn("x"); await request(u); await runChild("y", []);`).mixed, false, "the harness helpers are the cure");
});

check("no e2e or tests file calls a blocking function and the global fetch() unless it is listed with its reason", () => {
  const files = sources();
  assert.ok(files.length > 50 && files.includes("e2e/first-steps-ui.mjs") && files.includes("e2e/stale-connection.mjs"), "the scan found the suites: " + files.length);
  const unlisted = [];
  for (const file of files) {
    const found = scan(readFileSync(path.join(web, file), "utf8"), file);
    if (found.mixed && !(file in ALLOWED)) unlisted.push(`${file}: ${found.blocking.map(c => `${c.call}@${c.line}`).join(", ")} and ${found.fetching.map(c => `fetch@${c.line}`).join(", ")}`);
  }
  assert.deepEqual(unlisted, [], "These files stop their event loop and also use the global fetch(): a pooled socket that the server closes meanwhile fails the next POST with 'other side closed'. " +
    "Run the child with runChild() and talk to the server with request() (harness-lifecycle.mjs), or list the file in ALLOWED with the reason it is safe.");
});

check("every entry of the list is still needed and says why", () => {
  const stale = [];
  for (const [file, reason] of Object.entries(ALLOWED)) {
    assert.ok(typeof reason === "string" && reason.length > 20, file + ": a reason is required");
    const found = scan(readFileSync(path.join(web, file), "utf8"), file);
    if (!found.mixed) stale.push(file);
  }
  assert.deepEqual(stale, [], "These files no longer mix the two kinds of call: remove them from ALLOWED.");
});

complete.complete();
console.log(`[blocking-calls-guard] ${groups.length}/3 groups passed; ${sources().length} files read, ${Object.keys(ALLOWED).length} listed`);
