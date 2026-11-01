// Real React component, synthetic transport: failures must not enable selection.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { fileURLToPath } from 'node:url';
import { build } from 'esbuild';
import { chromium } from 'playwright';
import { closeHttpFixture, suiteCompletionGuard } from './harness-lifecycle.mjs';
const root = fileURLToPath(new URL('../', import.meta.url));
const mock = `
const hash='a'.repeat(64);
const source={id:'source',title:'żółw.txt',blob_hash:hash,format:'text',parser:'loom.source_parts',metadata:{source_key:'synthetic:component'}};
const projection={schema:'loom.source_text_parts/1',source_id:'source',source_sha256:hash,source_node:'src',projection_node:'proj',version_node:'ver',occurrence_nodes:['span'],coordinate:'source_utf8_bytes',status:'partial',reason:'text_only',method:{}};
window.writes=[]; window.mode='invalid';
export const api={listSources:async()=>[source],getConfig:async()=>({context_execution:{unified:{enabled:true,legacy_seed_ids:['existing'],legacy_relation_hops:0}}}),
getNodes:async()=>[{metadata:projection}],mergeConfigKey:async(...args)=>window.writes.push(args),actions:{clientBlobTextRead:async()=>{
 if(window.mode==='denied')throw Error('synthetic_native_refusal');
 return {node_id:'span',text:'żółw',source,provenance:{schema:'loom.source_text_occurrence/1',source_id:'source',source_sha256:hash,coordinate:'source_utf8_bytes',byte_range:[0,4],index:0}};
}}};`;
const bundle = await build({stdin:{contents:`import React from 'react';import {createRoot} from 'react-dom/client';import SourcePartsPanel from './src/components/SourcePartsPanel';createRoot(document.getElementById('root')).render(<SourcePartsPanel/>);`,resolveDir:root,loader:'tsx'},bundle:true,write:false,format:'iife',platform:'browser',jsx:'automatic',outfile:root+'source-parts-fixture.js',plugins:[{name:'synthetic-transport',setup(b){b.onResolve({filter:/^\.\.\/api$/},()=>({path:'synthetic-api',namespace:'fixture'}));b.onLoad({filter:/.*/,namespace:'fixture'},()=>({contents:mock,loader:'js'}));}}]});
const js=bundle.outputFiles.find(f=>f.path.endsWith('.js')).text;
const server=createServer((req,res)=>{if(req.url==='/app.js')res.writeHead(200,{'Content-Type':'text/javascript'}).end(js);else res.writeHead(200,{'Content-Type':'text/html'}).end('<div id="root"></div><script src="/app.js"></script>');});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const groups=[],completion=suiteCompletionGuard('source-parts-component',3,groups);let browser;
try {
 browser=await chromium.launch();const page=await browser.newPage();await page.goto('http://127.0.0.1:'+server.address().port);
 await page.getByTestId('source-parts-panel').locator('summary').click();await page.getByTestId('source-read').waitFor();
 await page.getByTestId('source-read').click();await page.getByTestId('source-error').waitFor();
 assert.match(await page.getByTestId('source-error').innerText(),/text_source_occurrence_invalid/);
 assert.equal(await page.getByTestId('source-select').count(),0);assert.deepEqual(await page.evaluate(()=>window.writes),[]);groups.push('invalid UTF-8 range refuses selectable result');
 await page.evaluate(()=>{window.mode='denied';});await page.getByTestId('source-read').click();
 await page.getByText('synthetic_native_refusal',{exact:false}).waitFor();assert.equal(await page.getByTestId('source-select').count(),0);assert.deepEqual(await page.evaluate(()=>window.writes),[]);groups.push('native refusal is visible, no selection write');
 await page.getByTestId('source-list-limit').fill('0');assert.ok(await page.getByTestId('source-reload').isDisabled());
 assert.ok(await page.getByTestId('source-selected-id').innerText().then(t=>t.includes('existing')));groups.push('invalid read limit cannot reload or clear existing selector');
} finally {await browser?.close();await closeHttpFixture(server);}
completion.complete();console.log('[source-parts-component] 3/3 PASS',groups);
