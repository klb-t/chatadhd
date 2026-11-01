// PREPARED ONLY: real React component + synthetic transport; never native/context acceptance.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { fileURLToPath } from 'node:url';
import { build } from 'esbuild';
import { chromium } from 'playwright';
import { closeHttpFixture, suiteCompletionGuard } from './harness-lifecycle.mjs';
const root = fileURLToPath(new URL('../', import.meta.url));
const mock = `
const rawHash='a'.repeat(64), textHash='b'.repeat(64);
const d={schema:'loom.docx_source_derivation/1',original_source_id:'src_docx_'+rawHash,original_sha256:rawHash,
original_size:100,text_sha256:textHash,extraction_blob:'c'.repeat(64),transform_sha256:'d'.repeat(64),transform:{limits:{max_entries:2048}}};
const raw={id:'upload',title:'żółw.docx',blob_hash:rawHash,format:'docx',parser:'loom.client_upload',metadata:{source_key:'upload:component'}};
const source={...raw,id:'derived',blob_hash:textHash,parser:'loom.source_parts',metadata:{...raw.metadata,coordinate:'derived_utf8_bytes',derivation:d}};
const projection={schema:'loom.source_text_parts/1',source_id:'derived',source_sha256:textHash,source_node:'src',projection_node:'proj',version_node:'ver',
occurrence_nodes:['first','second'],coordinate:'derived_utf8_bytes',status:'partial',reason:'docx_partial',derivation:d,original_source_node:'n_'+d.original_source_id,
method:{level:'recognized_block',derivation:d},warnings:['docx_images_omitted'],observations:[0,1].map(i=>({ordinal:i,text:'Żółw',locator:{source:'derived',byte_start:i*8,byte_len:7},
attrs:{recognized_kind:'paragraph',original_locator:'word/document.xml#body/'+i,evidence_class:'derived',coordinate:'derived_utf8_bytes'}}))};
window.writes=[];window.requests=[];window.made=false;window.mode='ok';window.readCalls=0;
const cfg={context_execution:{unified:{enabled:true,legacy_seed_ids:['existing'],legacy_relation_hops:0,budget_tokens:9000}}};
export const api={listSources:async()=>window.made?[raw,source]:[raw],getConfig:async()=>structuredClone(cfg),getNodes:async()=>[{metadata:projection}],
mergeConfigKey:async(k,v)=>{window.writes.push([k,v]);Object.assign(cfg.context_execution.unified,v.unified);return structuredClone(cfg);},
actions:{clientBlobText:async request=>{window.requests.push(request);if(window.mode==='refuse')throw Error('synthetic_materialization_refused');window.made=true;return projection;},
clientBlobTextRead:async({node_id})=>{window.readCalls++;if(window.mode==='refuse')throw Error('synthetic_read_refused');
const result={node_id,text:'Żółw\\n',source,provenance:{schema:'loom.source_text_occurrence/1',source_id:'derived',source_sha256:textHash,coordinate:'derived_utf8_bytes',
byte_range:node_id==='first'?[0,8]:[8,16],index:node_id==='first'?0:1,derivation:d}};
if(window.mode==='invalid')result.provenance.coordinate='source_utf8_bytes';
if(window.mode==='delayed')return new Promise(r=>window.releaseRead=()=>r(result));return result;}}};`;
const bundle = await build({stdin:{contents:`import React from 'react';import {createRoot} from 'react-dom/client';import SourcePartsPanel from './src/components/SourcePartsPanel';createRoot(document.getElementById('root')).render(<SourcePartsPanel/>);`,resolveDir:root,loader:'tsx'},bundle:true,write:false,format:'iife',platform:'browser',jsx:'automatic',outfile:root+'docx-component-fixture.js',plugins:[{name:'synthetic-transport',setup(b){b.onResolve({filter:/^\.\.\/api$/},()=>({path:'synthetic-api',namespace:'fixture'}));b.onLoad({filter:/.*/,namespace:'fixture'},()=>({contents:mock,loader:'js'}));}}]});
const js=bundle.outputFiles.find(f=>f.path.endsWith('.js')).text;
const server=createServer((req,res)=>{if(req.url==='/app.js')res.writeHead(200,{'Content-Type':'text/javascript'}).end(js);else res.writeHead(200,{'Content-Type':'text/html'}).end('<div id="root"></div><script src="/app.js"></script>');});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const groups=[],completion=suiteCompletionGuard('docx-source-parts-component',4,groups);let browser;
try {
 browser=await chromium.launch();const page=await browser.newPage();await page.goto('http://127.0.0.1:'+server.address().port);
 const panel=page.getByTestId('source-parts-panel'), toggle=()=>panel.locator('summary').click();
 const ready=()=>page.waitForFunction(()=>!document.querySelector('[data-testid=source-reload]')?.disabled);
 await toggle();await ready();assert.deepEqual(await page.evaluate(()=>window.requests),[]);
 assert.equal(await page.getByTestId('source-occurrence').count(),0);
 await page.evaluate(()=>{window.mode='refuse';});await page.getByTestId('source-materialize').click();await page.getByTestId('source-error').waitFor();
 assert.equal(await page.getByTestId('source-descriptor').getAttribute('data-source-id'),'upload');
 assert.deepEqual(await page.evaluate(()=>window.writes),[]);groups.push('explicit extraction refusal retains upload and selector');
 await page.evaluate(()=>{window.mode='ok';});await page.getByTestId('source-materialize').click();await page.getByTestId('source-occurrence').waitFor();
 assert.match(await page.getByTestId('source-original').innerText(),new RegExp('a'.repeat(64)));
 assert.match(await page.getByTestId('source-derived').innerText(),new RegExp('b'.repeat(64)));
 assert.equal(await page.getByTestId('source-blocks').locator('li').count(),2);assert.equal(await page.getByTestId('source-occurrence').locator('option').count(),2);
 assert.match(await page.getByTestId('source-warnings').innerText(),/Pominięto obrazy|Images/);
 assert.equal((await page.evaluate(()=>window.requests.at(-1))).hash,'a'.repeat(64));groups.push('separate representations, repeated blocks and translated warnings');
 await page.getByTestId('source-read').click();await page.getByTestId('source-select').waitFor();
 await page.evaluate(()=>{window.mode='invalid';});await page.getByTestId('source-read').click();await page.getByTestId('source-error').waitFor();
 assert.equal(await page.getByTestId('source-select').count(),0);assert.deepEqual(await page.evaluate(()=>window.writes),[]);
 assert.match(await page.getByTestId('source-selected-id').innerText(),/existing/);groups.push('invalid derived read removes selectable preview and preserves old selection');
 await page.evaluate(()=>{window.mode='delayed';});await page.getByTestId('source-read').click();
 await page.waitForFunction(()=>typeof window.releaseRead==='function');const before=await page.evaluate(()=>window.readCalls);
 await page.evaluate(()=>document.querySelector('[data-testid=source-read]').click());assert.equal(await page.evaluate(()=>window.readCalls),before);
 await toggle();await page.getByTestId('source-parts-browser').waitFor({state:'detached'});await toggle();await ready();
 await page.evaluate(()=>window.releaseRead());await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
 assert.equal(await page.getByTestId('source-read-result').count(),0);assert.deepEqual(await page.evaluate(()=>window.writes),[]);
 groups.push('duplicate click is blocked and delayed preview cannot enter reopened panel');
} finally {await browser?.close();await closeHttpFixture(server);}
completion.complete();console.log('[docx-source-parts-component] 4/4 PASS (synthetic transport only)',groups);
