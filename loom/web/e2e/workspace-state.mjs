// Pure runtime tests: propagation, persistence, migrations and profile isolation.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import ts from "typescript";
const source = readFileSync(new URL("../src/workspace/state.ts", import.meta.url), "utf8");
const js = ts.transpileModule(source, { compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.ES2020 } }).outputText;
const m = await import(`data:text/javascript;base64,${Buffer.from(js).toString("base64")}`);
let cases = 0;
function check(name, fn) { fn(); cases++; console.log(`PASS ${name}`); }
check("identity cycles converge and unrelated parameters remain independent", () => {
  let w = m.initialWorkspace(["graph", "graph", "graph"]);
  const [a,b,c] = w.panes.map(p => p.id);
  w = m.connect(w,a,b,"depth"); w = m.connect(w,b,a,"depth");
  w = m.changeParameter(w,a,"depth",4);
  assert.deepEqual(w.panes.map(p => p.parameters.depth), [4,4,1]);
  w = m.changeParameter(w,b,"selection",{kind:"entities",id:"e1",focus:"e1",run:"r1"});
  assert.ok(w.panes.every(p => p.parameters.selection.id === "e1"));
  assert.equal(w.panes.find(p => p.id === c).parameters.depth,1);
});
check("unlink removes ingress and egress, duplicate retains settings independently", () => {
  let w = m.initialWorkspace(); const id = w.panes[1].id;
  w = m.setWorkspaceRun(w,"r1"); w = m.unlink(w,id); w = m.setWorkspaceRun(w,"r2");
  assert.equal(w.panes[1].parameters.run,"r1");
  w = m.changeParameter(w,id,"selection",{kind:"entities",id:"e",focus:"e",run:"r1"});
  assert.equal(w.panes[0].parameters.selection,null);
  w = m.changeParameter(w,id,"filter","reference"); w = m.duplicatePane(w,id);
  assert.equal(w.panes[3].parameters.filter,"reference"); assert.equal(w.panes[3].followRun,false);
  assert.notEqual(w.panes[3].id,id);
});
check("adding views never reconnects a detached reference", () => {
  let w = m.initialWorkspace(); const id = w.panes[0].id;
  w = m.unlink(w,id); w = m.addPane(w,"graph");
  assert.ok(!w.bindings.some(b => b.source === id || b.target === id));
  assert.equal(w.panes[0].followRun,false);
});
check("pagination survives reload and refresh but resets when the data run changes", () => {
  let w = m.setWorkspaceRun(m.initialWorkspace(["candidates"]), "r1");
  w = m.changeParameter(w,w.panes[0].id,"offset",25);
  w = m.parseWorkspace(JSON.stringify(w));
  assert.equal(m.setWorkspaceRun(w,"r1").panes[0].parameters.offset,25);
  assert.equal(m.setWorkspaceRun(w,"r2").panes[0].parameters.offset,0);
});
check("round trip preserves stable IDs, empty layout, run, query, links and selection", () => {
  let w = m.initialWorkspace(); w = m.setWorkspaceRun(w,"r123");
  w = m.changeParameter(w,w.panes[0].id,"text","Goal");
  assert.deepEqual(m.parseWorkspace(JSON.stringify(w)),w);
  const empty = m.initialWorkspace([]); assert.deepEqual(m.parseWorkspace(JSON.stringify(empty)),empty);
});
check("no panel count limit and whitelist rejects execution profile fields", () => {
  const w = m.initialWorkspace(Array(14).fill("graph"));
  const raw = {...w, model_provider:"evil", permissions:{send:true}, profile:"compact"};
  const parsed = m.parseWorkspace(JSON.stringify(raw));
  assert.equal(parsed.panes.length,14); assert.equal(parsed.model_provider,undefined);
  assert.equal(parsed.permissions,undefined);
});
check("invalid snapshots are rejected, never substituted with valid-looking defaults", () => {
  const base = m.initialWorkspace();
  for (const mutate of [
    w => w.schema="future", w => w.profile="evil", w => w.panes[1].id=w.panes[0].id,
    w => w.panes[0].kind="toString", w => w.panes[0].parameters.depth=-1,
    w => w.panes[0].parameters.limit=0, w => w.panes[0].parameters.confidence=101,
    w => w.bindings[0].target="missing", w => w.bindings[0].parameter="default_model",
    w => w.active="missing", w => w.panes[0].parameters.selection={id:42},
  ]) { const w = structuredClone(base); mutate(w); assert.throws(()=>m.parseWorkspace(JSON.stringify(w))); }
});
check("legacy layout is migrated without altering its source; corrupt v2 preserved", () => {
  const store = new Map([["loom.knowledge.layout.v1", '["graph","claims"]']]);
  globalThis.localStorage={getItem:key=>store.get(key)??null};
  assert.deepEqual(m.loadWorkspace().workspace.panes.map(p=>p.kind),["graph","claims"]);
  store.set(m.STORAGE_KEY,"{broken"); assert.ok(m.loadWorkspace().error);
  assert.equal(store.get(m.STORAGE_KEY),"{broken");
});
console.log(`[workspace-state] ${cases}/${cases} groups passed`);
