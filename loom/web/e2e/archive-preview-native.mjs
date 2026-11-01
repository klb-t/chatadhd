#!/usr/bin/env node
// FOR THE INTEGRATOR ONLY. Real App/ImportPanel/GraphView/ChatView and native
// source actions; one loopback recording provider. No builds or dependency downloads.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import net from 'node:net';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, realpathSync, writeFileSync, readdirSync, cpSync, renameSync } from 'node:fs';
import { closeHttpFixture, request, stopChild, suiteCompletionGuard } from './harness-lifecycle.mjs';
import { makeTmpDir } from './tmp-dirs.mjs';

assert.equal(process.env.LOOM_FILE_GRAPH_NATIVE, '1', 'Only the integrator runs this harness, with explicit native opt-in.');
const loom = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..'), repo = path.dirname(loom);
const ownPath = input => { assert.ok(input); const p = realpathSync(input); assert.ok(p.startsWith(repo + path.sep), 'artifact must be in the integrator worktree'); return p; };
// Run the accepted relocatable package with its own default reader manifest.

let bin = ownPath(process.env.LOOM_SERVER_BIN); const originalBin = bin, dist = ownPath(process.env.LOOM_FILE_GRAPH_DIST);
assert.ok(existsSync(path.join(dist, 'index.html')), 'use the integrator fresh continuous graph web build');
const evidence = path.resolve(process.env.LOOM_FILE_GRAPH_EVIDENCE ?? '');
assert.ok(process.env.LOOM_FILE_GRAPH_EVIDENCE && evidence.startsWith(repo + path.sep));
assert.ok(!existsSync(evidence), 'use a fresh evidence directory; preserve failed attempts');
mkdirSync(evidence, { recursive: true });
const require = createRequire(path.join(loom, 'web/package.json'));
const { chromium } = require('playwright'); // own installed dependencies only; no shared fallback
const tmp = makeTmpDir('loom-archive-preview-ui-'), root = path.join(tmp, 'accounts');
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const calls = [], wire = [], groups = [], errors = [], observations = {};
const expectedGroups = 1;
const completion = suiteCompletionGuard('archive-preview-native', expectedGroups, groups);

let stage = path.dirname(path.dirname(bin));
const readerPath = path.join(stage, 'share/loom/docx_basic/reader.py');
const runtimePath = path.join(stage, 'share/loom/docx_basic/runtime.json');
assert.ok(existsSync(readerPath) && existsSync(runtimePath), 'accepted default package helper and interpreter manifest required');
const starts = [];

const materializationWire = [], chatResults = [];
const text = m => typeof m.content === 'string' ? m.content : (m.content ?? []).filter(p => p.type === 'text').map(p => p.text).join('\n');
const systemText = body => body.messages.filter(m => m.role === 'system').map(text).join('\n');
const overlay = (domain, overrides) => {
  mkdirSync(path.join(root, 'profiles'), { recursive: true });
  writeFileSync(path.join(root, 'profiles', domain + '.pack'), JSON.stringify({ schema: 'loom.runtime_profile_overlay/1', domain, overrides }));
};
const freePort = () => new Promise((resolve, reject) => { const s = net.createServer(); s.once('error', reject); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)); }); });
const until = async (fn, label) => { for (const end = Date.now() + 30000; Date.now() < end;) { if (await fn()) return; await new Promise(r => setTimeout(r, 75)); } throw Error('timeout: ' + label); };
let server, provider, browser, page, port, base;
const start = async () => {
  const args = ['--no-new-privs', '--landlock-access', 'fs'];
  for (const rule of ['execute,read-file,read-dir:/usr', 'read-file,write-file:/dev/null',
    'execute,read-file,read-dir:' + stage, 'read-file,read-dir:' + dist,
    'execute,write-file,read-file,read-dir,remove-dir,remove-file,make-dir,make-reg,make-sym,refer,truncate:' + tmp]) {
    args.push('--landlock-rule', 'path-beneath:' + rule);
  }
  args.push(bin, '--host', '127.0.0.1', '--port', String(port), '--accounts-root', root, '--data-dir', path.join(tmp, 'data'), '--static-dir', dist);
  const env = { PATH: '/usr/bin:/bin', LANG: 'C.UTF-8', TMPDIR: tmp, LOOM_LOG_ENABLED: '0', PYTHONDONTWRITEBYTECODE: '1' };
  starts.push({ command: ['/usr/bin/setpriv', ...args], env });
  server = spawn('/usr/bin/setpriv', args, { cwd: tmp, env, stdio: 'ignore' });
  await until(async () => { try { return (await request(base + '/api/healthz', { timeoutMs: 1000 })).ok; } catch { return false; } }, 'own native server');
};
const api = (route, body, method = body === undefined ? 'GET' : 'POST') => page.evaluate(async ({ route, body, method }) => {
  const r = await fetch(route, { method, headers: { 'Content-Type': 'application/json' }, ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
  const parsed = await r.json(); if (!r.ok || parsed.error) throw Error(route + ': ' + r.status + ' ' + JSON.stringify(parsed)); return parsed;
}, { route, body, method });
const dismiss = async () => { if (await page.getByTestId('guest-notice-close').isVisible().catch(() => false)) await page.getByTestId('guest-notice-close').click(); };
const workshop = async tab => {
  await dismiss();
  if (await page.getByTestId('close-panel').isVisible().catch(() => false)) await page.getByTestId('close-panel').click();
  await until(async () => await page.getByTestId('nav-' + tab).isVisible().catch(() => false) || await page.getByTestId('shell-menu-toggle').isVisible().catch(() => false), 'shell ready after navigation');
  if (!await page.getByTestId('nav-' + tab).isVisible().catch(() => false)) {
    if (await page.getByTestId('shell-menu-toggle').isVisible().catch(() => false)) {
      await page.getByTestId('shell-menu-toggle').click(); await page.getByTestId('shell-menu-workshop').click();
      await page.getByTestId('window-switcher').waitFor({ state: 'visible' });
    }
    if (!await page.getByTestId('nav-' + tab).isVisible()) await page.getByTestId('toggle-nav').click();
  }
  await page.getByTestId('nav-' + tab).waitFor({ state: 'visible' });
  await page.getByTestId('nav-' + tab).click();
};
const panel = async host => {
  const p = page.getByTestId(host).getByTestId('source-parts-panel');
  await p.waitFor(); if (await p.getAttribute('open') === null) await p.locator('summary').click();
  await p.getByTestId('source-reload').waitFor(); await until(async () => !await p.getByTestId('source-reload').isDisabled(), 'source read');
  assert.equal(await p.getByTestId('source-error').count(), 0); return p;
};
const upload = async (name, bytes, mime, formatChoice) => {
  const p = await panel('import-panel');
  await p.getByTestId('source-upload-file').setInputFiles({ name, mimeType: mime, buffer: Buffer.from(bytes) });
  const detected = await p.getByTestId('source-upload-format').inputValue();
  assert.equal(detected, 'zip');
  if(formatChoice) await p.getByTestId('source-upload-format').selectOption(formatChoice);

  const response = page.waitForResponse(r => r.url().endsWith('/api/client/blob/upload'));
  await p.getByTestId('source-upload').click(); const r = await response; assert.equal(r.status(), 200);
  const stored = await r.json(); assert.equal(stored.hash, sha(bytes)); assert.ok(stored.source_key && stored.source_id, 'durable upload hook required');
  await p.locator('[data-testid="source-descriptor"][data-source-key="' + stored.source_key + '"][data-hash="' + stored.hash + '"]').waitFor();
  assert.equal(await p.getByTestId('source-error').count(), 0); return stored;
};
const materialize = async () => {
  const p = await panel('import-panel'), response = page.waitForResponse(r => r.url().endsWith('/api/client/blob/text'));
  await p.getByTestId('source-materialize').click(); const r = await response; assert.equal(r.status(), 200, await r.text()); const result = await r.json();
  await p.locator('[data-testid="source-descriptor"][data-source-id="' + result.source_id + '"]').waitFor();
  await until(async () => (await p.getByTestId('source-occurrence').locator('option').count()) === result.occurrence_nodes.length, 'occurrence list');
  return result;
};

const original = readFileSync(ownPath(process.env.LOOM_ARCHIVE_FIXTURE));
const entry = (id) => page.locator('[data-testid="archive-entry"][data-node-id="'+id+'"]');
let configWrites = 0; const configPatches=[];
try {
 port=await freePort();base='http://127.0.0.1:'+port;
 overlay('server_accounts',{allow_private_hosts:['127.0.0.1'],guests:{enabled:true,start:'auto',keys_file:'',sweep_interval_seconds:0,model_sources:['free'],workdir:{subdir:'guests',idle_ttl_seconds:0,max_bytes:0},protection:{pow:{bits:0,bits_under_load:0},per_session:{chat_per_minute:0}}}});
 await start();browser=await chromium.launch();const context=await browser.newContext({locale:'pl-PL',viewport:{width:1440,height:1000}});
 await context.route('**/*',route=>new URL(route.request().url()).hostname==='127.0.0.1'?route.continue():route.abort());
 await context.addInitScript(()=>{if(location.hostname==='127.0.0.1')localStorage.setItem('loom.store.policy',JSON.stringify({targets:['browser_indexeddb']}));});
 page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));
 page.on('request',r=>{if(r.method()==='POST'&&r.url().endsWith('/api/chat'))wire.push(r.postDataJSON());
   if(r.method()==='POST'&&r.url().endsWith('/api/client/blob/text'))materializationWire.push(r.postDataJSON());
   if(r.method()!=='GET'&&r.url().endsWith('/api/config')){configWrites++;configPatches.push(r.postDataJSON());}});
 await page.goto(base);await page.getByTestId('chat-input').waitFor();await api('/api/config',{ui_language:'pl',model_registry_autorefresh:'off',semantic_analysis:false,auto_title:false},'PATCH');await page.reload();await workshop('import');
 const stored=await upload('syntetyczne.zip',original,'application/zip'),made=await materialize();observations.stored=stored;observations.made=made;
 const child=made.children.find(c=>c.method?.format==='text');assert.ok(child?.occurrence_nodes.length);
 const selection={unified:{enabled:true,include_knowledge:false,legacy_seed_ids:[child.occurrence_nodes[0],'synthetic-unrelated-id'],legacy_relation_hops:2},graph_reply:{enabled:false}};
 await api('/api/config',{context_execution:selection,synthetic_preserve:'keep'},'PATCH');
 const before={config:await api('/api/config'),nodes:await api('/api/graph/nodes?limit=1000')};
 const writesBefore=configWrites, requestsBefore=materializationWire.length;
 let p=await panel('import-panel');
 assert.equal(await p.getByTestId('projection-reproject').count(),0);
 const response=page.waitForResponse(r=>r.url().endsWith('/api/client/blob/text'));
 await p.getByTestId('projection-check').click();const resp=await response;assert.equal(resp.status(),200);const receipt=await resp.json();observations.receipt=receipt;
 // Do not accept successful HTTP as proof that the strict UI validator rendered it.
 await until(async()=>await p.getByTestId('archive-preview').count()>0||await p.getByTestId('projection-review-error').count()>0,'preview or explicit refusal');
 assert.equal(await p.getByTestId('projection-review-error').count(),0,await p.getByTestId('projection-review').innerText());
 const view=receipt.archive_preview,rows=Object.fromEntries(view.structure_nodes.map(r=>[r.locator.member,r]));
 assert.equal(await p.getByTestId('archive-preview').count(),2,'root and nested ZIP rendered');
 assert.equal(await p.getByTestId('projection-replay').getAttribute('data-status'),'not_exercised');
 for(const folder of ['a','b']) {
   const f=rows[folder],leaf=rows[folder+'/same.txt'];
   assert.equal(await entry(f.node_id).getAttribute('data-parent-id'),view.root.node_id);
   assert.equal(await entry(leaf.node_id).getAttribute('data-parent-id'),f.node_id);
   assert.equal(await entry(f.node_id).locator('[data-node-id="'+leaf.node_id+'"]').count(),1,'real DOM hierarchy');
   assert.ok((await entry(leaf.node_id).innerText()).includes(leaf.sha256));
   assert.ok((await entry(leaf.node_id).innerText()).includes(leaf.provenance_id));
   assert.equal(await entry(leaf.node_id).getByTestId('archive-child-replay').getAttribute('data-status'),'compatible');
 }
 assert.notEqual(rows['a/same.txt'].node_id,rows['b/same.txt'].node_id);assert.equal(rows['a/same.txt'].sha256,rows['b/same.txt'].sha256);
 assert.notEqual(rows['a/same.txt'].source_id,rows['b/same.txt'].source_id);
 const nested=view.children.find(c=>c.locator.member==='a/nested.zip').review.archive_preview;
 assert.equal(await entry(rows['a/nested.zip'].node_id).locator('[data-source-id="'+nested.root.source_id+'"]').count(),1,'nested archive remains below member');
 const deep=nested.structure_nodes.find(r=>r.kind==='file');assert.equal(await entry(deep.node_id).getAttribute('data-parent-id'),nested.structure_nodes.find(r=>r.kind==='folder').node_id);
 assert.equal(await entry(rows['opaque.bin'].node_id).getByTestId('archive-child-unknown').getAttribute('data-status'),'not_exercised');
 assert.match(await entry(rows['opaque.bin'].node_id).innerText(),/Zachowano oryginał/);
 assert.equal(await p.getByTestId('projection-reproject').count(),0);assert.equal(await p.getByTestId('projection-confirm').count(),0);
 const download=page.waitForEvent('download');await entry(rows['a/same.txt'].node_id).getByTestId('message-attachment-original').click();const d=await download;await d.saveAs(path.join(evidence,'original.txt'));assert.equal(sha(readFileSync(path.join(evidence,'original.txt'))),rows['a/same.txt'].sha256);
 observations.before=before;observations.rendered={distinctEqualMembers:true,nestedHierarchy:true,unknownNotExercised:true,downloadHash:rows['a/same.txt'].sha256};
 await page.screenshot({path:path.join(evidence,'preview.png'),fullPage:true});
 assert.deepEqual(await api('/api/config'),before.config);assert.deepEqual(await api('/api/graph/nodes?limit=1000'),before.nodes);
 assert.equal(configWrites,writesBefore);assert.deepEqual(materializationWire.slice(requestsBefore),[{operation:'projection_check',projection_node:made.projection_node,selected_nodes:[]}]);
 // New validator uncertainty only: inject an incomplete successful wire envelope.
 // Native positive above remains unmodified. This is an explicit UI refusal mechanism check.
 const incomplete=structuredClone(receipt);incomplete.archive_preview.children.pop();
 await page.route('**/api/client/blob/text',route=>route.fulfill({status:200,contentType:'application/json',body:JSON.stringify(incomplete)}));
 await p.getByTestId('projection-check').click();await p.getByTestId('projection-review-error').waitFor();
 assert.match(await p.getByTestId('projection-review-error').innerText(),/niepełna/);assert.equal(await p.getByTestId('archive-preview').count(),0);assert.equal(await p.getByTestId('projection-reproject').count(),0);
 observations.incompleteWire={injected:true,nativeProof:false,refused:true};
 await page.unroute('**/api/client/blob/text');
 const reloadPatchStart=configPatches.length;
 await page.reload();await page.getByTestId('chat-input').waitFor();await workshop('import');p=await panel('import-panel');
 await p.getByTestId('source-version').selectOption(made.source_id);await p.getByTestId('projection-check').waitFor();
 await page.waitForTimeout(800); // existing layout-journal debounce after explicit navigation
 const afterNavigation=await api('/api/config');
 const withoutJournal=c=>Object.fromEntries(Object.entries(c).filter(([k])=>k!=='window_layout_journal'));
 assert.deepEqual(withoutJournal(afterNavigation),withoutJournal(before.config),'navigation may only append its existing layout journal');
 assert.deepEqual(afterNavigation.context_execution,before.config.context_execution);
 const previewWrites=configWrites;
 const reopened=page.waitForResponse(r=>r.url().endsWith('/api/client/blob/text'));await p.getByTestId('projection-check').click();assert.deepEqual(await (await reopened).json(),receipt);
 await p.getByTestId('archive-preview').first().waitFor();assert.equal(await p.getByTestId('projection-review-error').count(),0);
 observations.afterConfig=await api('/api/config');
 assert.deepEqual(observations.afterConfig,afterNavigation);assert.deepEqual(await api('/api/graph/nodes?limit=1000'),before.nodes);assert.equal(configWrites,previewWrites);
 for(const patch of configPatches.slice(reloadPatchStart))assert.deepEqual(Object.keys(patch),['window_layout_journal']);
 observations.navigation={layoutJournalWrites:configPatches.length-reloadPatchStart,previewWrites:configWrites-previewWrites,otherConfigUnchanged:true};
 assert.equal(wire.length,0,'no chat/provider/model calls');assert.deepEqual(errors,[]);
 observations.after={configUnchanged:true,graphNodesUnchanged:true,previewWritesBeforeReload:0,previewWritesAfterReload:configWrites-previewWrites,selectionPreservedAfterReload:true};
 observations.stored=stored;observations.made=made;observations.backend=sha(readFileSync(bin));
 groups.push('actual ZIP upload/materialization -> read-only UI descriptor/download/reload + incomplete-envelope refusal');
 console.log('[archive-preview-native] PASS combined actual browser flow');
} finally {
 observations.groups=groups;observations.errors=errors;observations.starts=starts;observations.wire=materializationWire;observations.configPatchKeys=configPatches.map(p=>Object.keys(p));observations.package={serverSha:sha(readFileSync(originalBin)),webSha:sha(readFileSync(path.join(dist,'index.html')))};
 if(page&&groups.length!==expectedGroups){try{observations.failure=await page.locator('body').innerText();await page.screenshot({path:path.join(evidence,'failure.png'),fullPage:true});}catch{}}
 writeFileSync(path.join(evidence,'observations.json'),JSON.stringify(observations,null,2));await browser?.close();await stopChild(server);
}
completion.complete();
