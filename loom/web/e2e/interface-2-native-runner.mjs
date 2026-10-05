#!/usr/bin/env node
// Sequential native gate: each child must finish its actual declared groups,
// print a final summary and exit successfully. An early exit0 is never a pass.
import { spawn } from "node:child_process";
import { once } from "node:events";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { assertSuiteOutput, suiteCompletionGuard } from "./harness-lifecycle.mjs";

const webRoot = fileURLToPath(new URL("../", import.meta.url));
const suites = [
  { suite: "interface-2-native", expectedGroups: 8, args: ["e2e/interface-2-native.mjs"] },
  { suite: "operations-panel", expectedGroups: 8, args: ["e2e/operations-panel.mjs", "--native"] },
  { suite: "usage-policy-native", expectedGroups: 4, args: ["e2e/usage-policy-native.mjs"] },
];
const completed = [];
const completion = suiteCompletionGuard("interface-2-native-runner", suites.length, completed);
for (const { suite, expectedGroups, args } of suites) {
  const child = spawn(process.execPath, [path.join(webRoot, args[0]), ...args.slice(1)], {
    cwd: webRoot, stdio: ["ignore", "pipe", "pipe"],
  });
  let stdout = "";
  child.stdout.on("data", chunk => { stdout += chunk.toString(); process.stdout.write(chunk); });
  child.stderr.on("data", chunk => process.stderr.write(chunk));
  const [exitCode, signal] = await once(child, "close");
  assertSuiteOutput({ suite, expectedGroups, stdout, exitCode, signal });
  completed.push(suite);
}
completion.complete();
console.log("[interface-2-native-runner] 3/3 suites complete; 20/20 declared groups (8 interface + 8 operations + 4 usage)");
