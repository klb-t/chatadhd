// Unit test of e2e/ts-module.mjs: nested imports, shared instances, later-added imports, cycles.
import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { createTsModuleLoader } from "../../e2e/ts-module.mjs";

const dir = mkdtempSync(path.join(tmpdir(), "loom-tsmod-"));
try {
  const put = (name, text) => { mkdirSync(path.dirname(path.join(dir, name)), { recursive: true }); writeFileSync(path.join(dir, name), text); };
  put("a.ts", 'import { b } from "./sub/b";\nimport type { T } from "./types";\nimport { shared } from "./shared";\nexport const a: number = b + 1;\nexport const sa = shared;\nexport type { T };');
  put("sub/b.ts", 'import { c } from "./deep/index";\nimport { shared } from "../shared";\nexport const b: number = c + 1;\nexport const sb = shared;\nexport * from "../reexp";');
  put("sub/deep/index.ts", 'export const c: number = 40;');
  put("shared.ts", 'export const shared = { n: Math.random() };');
  put("reexp.ts", 'export const reexp = "re";');
  put("types.ts", 'export type T = { x: number };');
  put("plain.mjs", 'export const plain = "js";');
  put("withjs.ts", 'import { plain } from "./plain.mjs";\nexport const w = plain;');

  let l = createTsModuleLoader();
  const a = await l.load(path.join(dir, "a.ts"));
  assert.equal(a.a, 42);
  assert.equal(a.sa, (await l.load(path.join(dir, "sub/b.ts"))).sb, "one instance per file");
  assert.equal((await l.load(path.join(dir, "sub/b.ts"))).reexp, "re", "export * from");
  assert.equal((await l.load(path.join(dir, "withjs.ts"))).w, "js", "existing .mjs is mapped to its file URL");

  // an import added later just works (fresh loader = fresh read)
  put("later.ts", 'export const later = 7;');
  put("sub/deep/index.ts", 'import { later } from "../../later";\nexport const c: number = 40 + later;');
  l = createTsModuleLoader();
  assert.equal((await l.load(path.join(dir, "a.ts"))).a, 49);

  // cycle: reported by name, no hang
  put("x.ts", 'import { y } from "./y";\nexport const x = 1 + y;');
  put("y.ts", 'import { x } from "./x";\nexport const y = 2 + x;');
  await assert.rejects(() => createTsModuleLoader().load(path.join(dir, "x.ts")), /import cycle .*x\.ts -> .*y\.ts -> .*x\.ts/);

  // missing import
  put("m.ts", 'import { q } from "./nope";\nexport const m = q;');
  await assert.rejects(() => createTsModuleLoader().load(path.join(dir, "m.ts")), /cannot resolve "\.\/nope"/);
  console.log("[ts-module] PASS nested, shared, later import, cycle, missing");
} finally {
  if (!process.env.LOOM_KEEP_TMP) rmSync(dir, { recursive: true, force: true }); else console.log("[ts-module] kept", dir);
}
