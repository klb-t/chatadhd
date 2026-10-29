// Temp directories of a test harness that clean up after themselves. makeTmpDir() is a drop-in for
// mkdtempSync(path.join(tmpdir(), prefix)) (os.tmpdir() honours TMPDIR). Every directory made here is removed when
// the process exits (success, failure, uncaught exception, process.exit, SIGINT/SIGTERM) - nothing else is touched.
// LOOM_KEEP_TMP=1 keeps them all. {evidence:true} marks a directory the script promises as evidence (screenshots,
// receipts): it is kept when the run fails, and its path is printed then.
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const made = [];
let armed = false;
function arm() {
  if (armed) return; armed = true;
  process.on("exit", code => {
    for (const { dir, evidence } of made.reverse()) {
      if (process.env.LOOM_KEEP_TMP || (evidence && code !== 0)) console.error(`[tmp] kept ${dir}`);
      else try { rmSync(dir, { recursive: true, force: true, maxRetries: 3, retryDelay: 100 }); } catch (error) { console.error(`[tmp] could not remove ${dir}: ${error.message}`); }
    }
  });
  for (const [signal, number] of [["SIGINT", 130], ["SIGTERM", 143]]) if (process.listenerCount(signal) === 0) process.once(signal, () => process.exit(number));
}

export function makeTmpDir(prefix, { evidence = false } = {}) {
  arm();
  const dir = mkdtempSync(path.join(tmpdir(), prefix));
  made.push({ dir, evidence });
  return dir;
}
