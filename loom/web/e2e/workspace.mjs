#!/usr/bin/env node
// Chromium -> actual Loom C++ HTTP server -> authored synthetic DEV archive.
// No provider required; no remote requests are permitted by the browser test.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";
const web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const loom = path.resolve(web,"..");
const binary = process.env.LOOM_SERVER_BIN || path.join(loom,"build/dev/server/loom-server");
assert.ok(existsSync(binary), "Build native server first or set LOOM_SERVER_BIN");
const evidenceDir=process.env.WORKSPACE_EVIDENCE_DIR;
if(evidenceDir){mkdirSync(evidenceDir,{recursive:true});assert.ok(!existsSync(path.join(evidenceDir,"workspace-perspective.json")),"Use a fresh evidence directory; previous results are retained.");}
const dir=mkdtempSync(path.join(tmpdir(),"loom-workspace-"));
const probe=createServer(); await new Promise(r=>probe.listen(0,"127.0.0.1",r));
const port=probe.address().port; await new Promise(r=>probe.close(r));
const base=`http://127.0.0.1:${port}`;
const server=spawn(binary,["--host","127.0.0.1","--port",String(port),"--data-dir",dir,"--static-dir",path.join(web,"dist")],{cwd:loom,stdio:["ignore","pipe","pipe"]});
let log="", browser, page, savedPerspective; for(const s of [server.stdout,server.stderr])s.on("data",d=>log+=d);
async function api(method,url,body){const r=await fetch(base+url,{method,headers:{"Content-Type":"application/json"},...(body===undefined?{}:{body:JSON.stringify(body)})});const result=await r.json();assert.ok(r.ok,JSON.stringify(result));return result;}
const key="loom.knowledge.workspace.v2";
let passed=0;
async function step(name,fn){await fn();passed++;console.log(`[workspace] PASS ${name}`);}
try {
  const until=Date.now()+20000;
  for(;;){try{if((await fetch(base+"/api/healthz")).ok)break;}catch{} if(Date.now()>until)throw Error(log);await new Promise(r=>setTimeout(r,100));}
  await api("PATCH","/api/config",{semantic_analysis:false, default_model:"unchanged/workspace-test",stream:false});
  const fixture=path.join(loom,"tests/fixtures/eval/synthetic_dev/chatgpt_export.zip");
  const run=await api("POST","/api/knowledge/run",{sources:[fixture],priors:false,llm:"off"});
  assert.equal(run.status,"done");
  const configBefore=await api("GET","/api/config");
  browser=await chromium.launch({headless:true,...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE?{executablePath:process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE}:{})});
  page=await browser.newPage({viewport:{width:1440,height:1000}});
  const errors=[], requests=[], external=[];
  page.on("pageerror",e=>errors.push(e.message));
  await page.route("**/*",async route=>{const req=route.request();if(new URL(req.url()).origin!==base){external.push(req.url());await route.abort();return;}requests.push({method:req.method(),path:new URL(req.url()).pathname});await route.continue();});
  const panes=()=>page.locator("[data-view-id]");
  const graph=()=>page.getByTestId("kb-pane-graph");
  const snapshot=()=>page.evaluate(k=>JSON.parse(localStorage.getItem(k)),key);
  async function open(){await page.getByTestId("nav-knowledge").click(); const hide=page.getByRole("button",{name:"Hide chat",exact:true});if(await hide.count())await hide.click();await graph().first().locator(".kb-node").first().waitFor();}
  async function settings(pane){const d=pane.locator(".kb-view-settings");if(!await d.getAttribute("open")) { if(!(await d.evaluate(e=>e.open)))await d.locator(":scope > summary").click(); }}
  await page.goto(base); await open();
  let referenceId, ids, saved;
  await step("actual run, stable identities and copied independent reference",async()=>{
    assert.equal(await page.getByLabel("Knowledge run",{exact:true}).inputValue(),run.run);
    const original=graph().first(); await original.getByLabel("Graph focus depth").fill("2");
    await original.getByLabel("Graph background opacity").fill("35");
    await original.getByRole("button",{name:"Duplicate Knowledge graph view",exact:true}).click();
    await graph().last().locator(".kb-node").first().waitFor();
    referenceId=await graph().last().getAttribute("data-view-id");
    assert.equal(await graph().last().getByLabel("Graph focus depth").inputValue(),"2");
    await settings(graph().last());
    assert.equal(await graph().last().getByLabel("Follow workspace run and limit").isChecked(),false);
    await page.getByLabel("View type",{exact:true}).selectOption("graph"); await page.getByTestId("kb-add-view").click();
    await graph().last().locator(".kb-node").first().waitFor();
    assert.equal(await graph().count(),3); ids=await panes().evaluateAll(nodes=>nodes.map(n=>n.dataset.viewId));
  });
  await step("directed depth coupling, convergent cycle, keyboard selection and reference isolation",async()=>{
    const first=graph().nth(0), linked=graph().nth(2), reference=graph().nth(1);
    await settings(linked); await linked.getByLabel("Link source view").selectOption(await first.getAttribute("data-view-id"));
    await linked.getByLabel("Link parameter").selectOption("depth"); await linked.getByRole("button",{name:"Link parameter",exact:true}).click();
    assert.equal(await linked.getByLabel("Graph focus depth").inputValue(),"2");
    await settings(first); await first.getByLabel("Link source view").selectOption(await linked.getAttribute("data-view-id"));
    await first.getByLabel("Link parameter").selectOption("depth"); await first.getByRole("button",{name:"Link parameter",exact:true}).click();
    await first.getByLabel("Graph focus depth").fill("4");
    assert.equal(await linked.getByLabel("Graph focus depth").inputValue(),"4");
    assert.equal(await reference.getByLabel("Graph focus depth").inputValue(),"2");
    await first.locator(".kb-node").first().focus();await page.keyboard.press("Enter");
    assert.equal(await linked.locator(".kb-node.focused").count(),1);
    assert.equal(await reference.locator(".kb-node.focused").count(),0);
    await page.getByTestId("kb-pane-claims").getByLabel("Filter Claims").fill("preserve");
    const snap=await snapshot(); assert.ok(snap.panes[0].parameters.selection);
  });
  await step("saved perspective, profile isolation and exact reload restoration",async()=>{
    await page.getByLabel("View profile",{exact:true}).selectOption("compact");
    await page.getByRole("button",{name:"Save perspective",exact:true}).click();
    saved=await snapshot(); savedPerspective=saved;
    await graph().first().getByLabel("Graph focus depth").fill("1");
    await page.getByLabel("View profile",{exact:true}).selectOption("stacked");
    await page.getByRole("button",{name:"Restore perspective",exact:true}).click();
    await graph().first().locator(".kb-node").first().waitFor();
    assert.deepEqual(await snapshot(),saved);
    await page.reload(); await open();
    assert.deepEqual(await panes().evaluateAll(nodes=>nodes.map(n=>n.dataset.viewId)),ids);
    assert.deepEqual(await snapshot(),saved);
    assert.equal(await graph().nth(2).getByLabel("Graph focus depth").inputValue(),"4");
    assert.equal(await graph().nth(1).getByLabel("Graph focus depth").inputValue(),"2");
    assert.equal(await page.getByTestId("kb-pane-claims").getByLabel("Filter Claims").inputValue(),"preserve");
    assert.equal(await page.getByTestId("kb-inspector").locator("h4").count(),1);
    assert.deepEqual(await api("GET","/api/config"),configBefore);
    assert.equal(requests.some(r=>r.method!=="GET" && ["/api/config","/api/chat","/api/knowledge/run"].includes(r.path)),false);
  });
  await step("run switching preserves independent reference and native run provenance",async()=>{
    const second=await api("POST","/api/knowledge/run",{sources:[fixture],priors:true,llm:"off"}); assert.equal(second.status,"done");
    await page.getByRole("button",{name:"Refresh",exact:true}).click();
    await page.getByLabel("Knowledge run",{exact:true}).selectOption(second.run);
    await graph().first().locator(".kb-node").first().waitFor();
    const snap=await snapshot(); assert.equal(snap.panes.find(p=>p.id===referenceId).parameters.run,run.run);
    assert.equal(snap.panes[0].parameters.run,second.run);
    await settings(graph().first()); await graph().first().getByText("Data version and execution status",{exact:true}).click();
    assert.match(await graph().first().locator(".kb-detail pre").innerText(),new RegExp(second.run));
  });
  await step("native context preview, persisted input and explicit non-delivery status",async()=>{
    await page.getByLabel("View type",{exact:true}).selectOption("context"); await page.getByTestId("kb-add-view").click();
    const context=page.getByTestId("kb-pane-context");
    await context.getByLabel("Context goal",{exact:true}).fill("Implement the project preserving principles");
    await context.getByLabel("Knowledge context token budget").fill("1200");
    await context.getByRole("button",{name:"Build context preview"}).click();
    await context.locator(".kb-context-band").first().waitFor();
    assert.match(await context.innerText(),/Previewing does not send a model request/);
    await context.getByLabel("Context goal",{exact:true}).fill("New goal");
    assert.match(await context.innerText(),/Inputs changed/);
    await page.reload(); await open();
    assert.equal(await page.getByTestId("kb-pane-context").getByLabel("Context goal",{exact:true}).inputValue(),"New goal");
    assert.equal(await page.getByTestId("kb-pane-context").locator(".kb-context-band").count(),0);
  });
  await step("no five-view cap, mobile layout and keyboard controls",async()=>{
    await page.getByLabel("View type",{exact:true}).selectOption("graph"); await page.getByTestId("kb-add-view").click();
    assert.equal(await panes().count(),7);
    await page.setViewportSize({width:390,height:844});
    const sidebar=page.getByTestId("sidebar"); if(!(await sidebar.getAttribute("class")).includes("collapsed"))await page.getByTestId("toggle-sidebar").click();
    await settings(graph().first());
    await graph().first().getByLabel("Graph focus depth").focus(); await page.keyboard.press("ArrowRight");
    assert.equal(await graph().first().getByLabel("Graph focus depth").inputValue(),"5");
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    if(process.env.WORKSPACE_SCREENSHOT)await page.screenshot({path:process.env.WORKSPACE_SCREENSHOT,fullPage:true});
  });
  await step("unlinked first pane stays independent when new views are added",async()=>{
    const first=panes().first(); const id=await first.getAttribute("data-view-id");
    await settings(first); await first.getByRole("button",{name:"Unlink as reference",exact:true}).click();
    await page.getByLabel("View type",{exact:true}).selectOption("candidates"); await page.getByTestId("kb-add-view").click();
    const w=await snapshot(); assert.ok(!w.bindings.some(b=>b.source===id||b.target===id));
    const candidates=page.getByTestId("kb-pane-candidates"); await candidates.getByLabel("Candidate kind",{exact:true}).fill("nonexistent-persisted-kind");
    await candidates.getByLabel("Candidate page size").selectOption("25");
    await page.reload(); await open();
    assert.equal(await page.getByTestId("kb-pane-candidates").getByLabel("Candidate kind",{exact:true}).inputValue(),"nonexistent-persisted-kind");
    assert.equal(await page.getByTestId("kb-pane-candidates").getByLabel("Candidate page size").inputValue(),"25");
  });
  await step("catalog selection restores from native data, not a saved record copy",async()=>{
    await page.getByLabel("View type",{exact:true}).selectOption("catalog"); await page.getByTestId("kb-add-view").click();
    const catalog=page.getByTestId("kb-pane-catalog"); await catalog.locator(".kb-record").first().click();
    await page.getByTestId("kb-inspector").locator("h4").waitFor();
    const w=await snapshot(), p=w.panes.find(p=>p.kind==="catalog"); assert.equal(w.active,p.id);assert.equal(p.parameters.selection.kind,"catalog");
    const text=await page.getByTestId("kb-inspector").locator("h4").innerText();
    await page.reload(); await open();
    await page.getByTestId("kb-inspector").locator("h4").waitFor();
    assert.equal(await page.getByTestId("kb-inspector").locator("h4").innerText(),text);
    assert.equal((await snapshot()).active,p.id);
  });
  await step("late catalog preview after pane close cannot corrupt saved active identity",async()=>{
    let release, arrived;
    const wait=new Promise(r=>release=r), seen=new Promise(r=>arrived=r);
    await page.route("**/api/catalog/units/**",async route=>{const response=await route.fetch();arrived();await wait;try{await route.fulfill({response});}catch{}});
    const catalog=page.getByTestId("kb-pane-catalog");
    await catalog.locator(".kb-record").nth(1).click(); await seen;
    const closedId=await catalog.getAttribute("data-view-id"); await catalog.getByRole("button",{name:"Close Source catalog view",exact:true}).click();
    release(); await page.unroute("**/api/catalog/units/**",undefined);
    // A round trip/reload also lets any already queued preview completion run.
    await api("GET","/api/healthz"); const w=await snapshot();assert.notEqual(w.active,closedId);
    await page.reload();await open();
    assert.equal(await page.getByTestId("kb-pane-catalog").count(),0);
    assert.equal(await page.getByText(/Autosave is paused/).count(),0);
  });
  await step("unavailable saved run is reported without silently selecting latest data",async()=>{
    await page.evaluate(k=>{const s=JSON.parse(localStorage.getItem(k));s.run="missing-run";for(const p of s.panes){p.parameters.run="missing-run";}localStorage.setItem(k,JSON.stringify(s));},key);
    await page.reload();await page.getByTestId("nav-knowledge").click();
    assert.equal(await page.getByLabel("Knowledge run",{exact:true}).inputValue(),"missing-run");
    assert.equal(await graph().first().locator(".kb-node").count(),0);
    assert.match(await graph().first().innerText(),/unavailable/);
  });
  assert.deepEqual(errors,[]); assert.deepEqual(external,[]);
  assert.equal(requests.some(r=>r.path==="/api/chat"),false);
  console.log(`[workspace] ${passed}/${passed} scenarios passed; real native API, authored synthetic DEV fixture, zero model calls`);
} catch(error){
  console.error(log);
  if(evidenceDir&&page)await page.screenshot({path:path.join(evidenceDir,"workspace-failure.png"),fullPage:true}).catch(()=>{});
  throw error;
}finally{
  if(browser)await browser.close(); server.kill("SIGTERM");await new Promise(r=>server.exitCode!==null?r():server.once("exit",r));rmSync(dir,{recursive:true,force:true});
  if(evidenceDir)writeFileSync(path.join(evidenceDir,"workspace-perspective.json"),JSON.stringify({fixture:"synthetic_dev/chatgpt_export.zip",passed,savedPerspective},null,2)+"\n",{flag:"wx"});
}
