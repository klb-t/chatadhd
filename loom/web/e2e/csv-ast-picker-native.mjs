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
const tmp = makeTmpDir('loom-csv-ast-ui-'), root = path.join(tmp, 'accounts');
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const calls = [], wire = [], groups = [], errors = [], observations = {};
const expectedGroups = 3;
const completion = suiteCompletionGuard('csv-ast-picker-native', expectedGroups, groups);

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
  assert.equal(detected, name.toLowerCase().endsWith('.csv') ? 'csv' : 'python_ast');
  if(formatChoice) await p.getByTestId('source-upload-format').selectOption(formatChoice);
  else assert.ok((await p.getByTestId('source-upload-format-about').innerText()).length > 20);
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
const read = async (p, id) => {
  await p.getByTestId('source-occurrence').selectOption(id);
  const response = page.waitForResponse(r => r.url().endsWith('/api/client/blob/text/read'));
  await p.getByTestId('source-read').click(); const r = await response; assert.equal(r.status(), 200);
  const result = await r.json(); await p.locator('[data-testid="source-read-result"][data-node-id="' + id + '"]').waitFor();
  assert.equal(await p.getByTestId('source-text').textContent(), result.text); return result;
};
const select = async (p, id) => {
  await read(p, id); await p.getByTestId('source-hops').fill('0'); await p.getByTestId('source-select').click();
  await p.locator('[data-testid="source-selected-id"][data-node-id="' + id + '"]').waitFor();
  await p.getByTestId('source-notice').waitFor();
};
const check = async (name, fn) => { await fn(); groups.push(name); console.log('[continuous-graph-native] PASS ' + name); await page.screenshot({ path: path.join(evidence, groups.length + '.png'), fullPage: true }); };


const rawHash = async h => page.evaluate(async h=>{const r=await fetch('/api/client/blob/'+h+'/raw');if(!r.ok)throw Error('original HTTP '+r.status);const b=await r.arrayBuffer();return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',b)),x=>x.toString(16).padStart(2,'0')).join('');},h);
const csv=Buffer.from('\ufeffimię;miasto;opis\r\nŁukasz;Łódź;"pierwsza\r\ndruga; część"\r\n'),
  python=Buffer.from('raise RuntimeError("NEVER_EXECUTE")\r\nclass Żółw:\r\n    def wybrana(self): return "żółć"\r\n'),
  bad=Buffer.from('def zepsuta(:\n    pass\n');
const saved=[];
const refusal=async(expected)=>{
 const p=await panel('import-panel'),id=await p.getByTestId('source-version').inputValue(),before=await api('/api/graph/nodes?kind=text_source_projection&limit=1000'),requests=materializationWire.length;
 const response=page.waitForResponse(r=>r.url().endsWith('/api/client/blob/text'));
 await p.getByTestId('source-materialize').click();const r=await response;assert.ok(r.status()>=400);const body=await r.text();assert.match(body,expected);
 await p.getByTestId('source-error').waitFor();assert.match(await p.getByTestId('source-error').innerText(),expected);
 assert.equal(await p.getByTestId('source-version').inputValue(),id);assert.equal(materializationWire.length,requests+1);
 assert.equal(await p.getByTestId('source-occurrence').locator('option').count(),0);
 assert.deepEqual(await api('/api/graph/nodes?kind=text_source_projection&limit=1000'),before);
 return {status:r.status(),body,source_id:id,request:materializationWire.at(-1)};
};
try{
 port=await freePort();base='http://127.0.0.1:'+port;
 overlay('server_accounts',{allow_private_hosts:['127.0.0.1'],guests:{enabled:true,start:'auto',keys_file:'',sweep_interval_seconds:0,model_sources:['free'],workdir:{subdir:'guests',idle_ttl_seconds:0,max_bytes:0},protection:{pow:{bits:0,bits_under_load:0},per_session:{chat_per_minute:0}}}});
 await start();browser=await chromium.launch();const context=await browser.newContext({locale:'pl-PL',viewport:{width:1280,height:1050}});
 await context.route('**/*',route=>new URL(route.request().url()).hostname==='127.0.0.1'?route.continue():route.abort());
 await context.addInitScript(()=>{if(location.hostname==='127.0.0.1')localStorage.setItem('loom.store.policy',JSON.stringify({targets:['browser_indexeddb']}));});
 page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>{if(r.method()==='POST'&&r.url().endsWith('/api/chat'))wire.push(r.postDataJSON());if(r.method()==='POST'&&r.url().endsWith('/api/client/blob/text'))materializationWire.push(r.postDataJSON());});
 await page.goto(base);await page.getByTestId('chat-input').waitFor();await api('/api/config',{ui_language:'pl',model_registry_autorefresh:'off',semantic_analysis:false,auto_title:false},'PATCH');await page.reload();await workshop('import');
 await check('actual picker CSV and Python AST -> original/source/parts/read/global selection',async()=>{
  for(const [name,bytes,format] of [['ŻÓŁW.CSV',csv,'csv'],['żółw.PY',python,'python_ast']]){
   const stored=await upload(name,bytes,'text/plain'),made=await materialize(),p=await panel('import-panel');
   assert.ok(made.occurrence_nodes.length>0);assert.equal(materializationWire.at(-1).format,format);
   assert.equal(await p.getByTestId('projection-review').count(),1,'projection review preserved');
   const reads=[];for(const id of made.occurrence_nodes)reads.push(await api('/api/client/blob/text/read',{node_id:id}));
   const chosen=reads.find(r=>format==='csv'?r.text==='"pierwsza\r\ndruga; część"':r.text==='def wybrana(self): return "żółć"');assert.ok(chosen);
   if(format==='csv'){assert.equal(chosen.provenance.unit.kind,'csv_cell');assert.deepEqual(chosen.provenance.unit.attrs.physical_lines,[2,3]);}
   else assert.equal(chosen.provenance.structural_evidence_class,'derived');
   await select(p,chosen.node_id);assert.equal(await rawHash(stored.hash),sha(bytes));
   saved.push({stored,made,chosen});
  }
  observations.accepted=saved;
 });
 await check('actual SyntaxError refusal and explicit plain-code choice without automatic fallback',async()=>{
  await upload('zepsuta.py',bad,'text/plain');observations.syntax=await refusal(/python_ast_syntax_error/);
  // A new explicit user upload selects plain code; previous failed source is untouched.
  await page.reload();await workshop('import');await upload('jawny-kod.py',bad,'text/plain','code');const made=await materialize();assert.equal(materializationWire.at(-1).format,'code');assert.ok(made.occurrence_nodes.length>0);
  const p=await panel('import-panel');const shown=await read(p,made.occurrence_nodes[0]);assert.ok(bad.toString().includes(shown.text));observations.plain={made,shown};
 });
 await check('native restart preserves reads/selector; missing packaged helper refuses without mutation',async()=>{
  const before=(await api('/api/config')).context_execution;
  await page.goto('about:blank');await stopChild(server);server=undefined;await start();await page.goto(base);await page.getByTestId('chat-input').waitFor();await workshop('import');
  assert.deepEqual((await api('/api/config')).context_execution,before);
  const p=await panel('import-panel');for(const s of saved){await p.getByTestId('source-version').selectOption(s.made.source_id);await until(async()=>await p.getByTestId('source-occurrence').locator('option').count()===s.made.occurrence_nodes.length,'reopened occurrences');assert.deepEqual(await read(p,s.chosen.node_id),s.chosen);assert.equal(await rawHash(s.stored.hash),s.stored.hash);}
  await upload('missing-helper.py',python,'text/plain');
  await page.goto('about:blank');await stopChild(server);server=undefined;
  bin=ownPath(process.env.LOOM_MISSING_HELPER_BIN);stage=path.dirname(path.dirname(bin));assert.equal(sha(readFileSync(bin)),sha(readFileSync(originalBin)));
  await start();await page.goto(base);await page.getByTestId('chat-input').waitFor();await workshop('import');
  const missing=await panel('import-panel');const options=await missing.getByTestId('source-version').locator('option').evaluateAll(es=>es.map(e=>({value:e.value,text:e.textContent})));
  await missing.getByTestId('source-version').selectOption(options.find(e=>e.text.startsWith('missing-helper.py')).value);
  observations.missing=await refusal(/docx_reader_unavailable/);assert.equal(observations.missing.request.format,'python_ast');
  assert.deepEqual(errors,[]);assert.equal(wire.length,0,'no chat/model/provider calls');observations.persistence={restart:true,selector:before};
 });
}finally{
 observations.groups=groups;observations.errors=errors;observations.starts=starts;observations.wire=materializationWire;observations.package={serverSha:sha(readFileSync(originalBin)),webSha:sha(readFileSync(path.join(dist,'index.html')))};
 if(page&&groups.length!==expectedGroups){try{observations.failure=await page.locator('body').innerText();await page.screenshot({path:path.join(evidence,'failure.png'),fullPage:true});}catch{}}
 writeFileSync(path.join(evidence,'observations.json'),JSON.stringify(observations,null,2));await browser?.close();await stopChild(server);
}
completion.complete();console.log('[csv-ast-picker-native] combined target-environment flow PASS');
