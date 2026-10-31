#!/usr/bin/env node
// Component boundary: download bytes only after matching their content address.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {build} from 'esbuild';
import {chromium} from 'playwright';
import {closeHttpFixture,suiteCompletionGuard} from './harness-lifecycle.mjs';
const root=fileURLToPath(new URL('../',import.meta.url));
const bytes=Buffer.from('SYNTHETIC_ORIGINAL — żółw.');
const digest=createHash('sha256').update(bytes).digest('hex');
const bundle=await build({stdin:{contents:`import React from 'react';import {createRoot} from 'react-dom/client';
import AttachmentOriginal from './src/components/AttachmentOriginal';
const ref=location.search.includes('plain')?'not-a-content-address':'blob:${digest}/oryginał.txt';
createRoot(document.getElementById('root')).render(<div data-testid="download-fixture-ready"><AttachmentOriginal reference={ref} name="oryginał.txt"/></div>);`,resolveDir:root,loader:'tsx'},bundle:true,write:false,format:'iife',platform:'browser',jsx:'automatic',outfile:root+'attachment-original-fixture.js'});
const js=bundle.outputFiles.find(f=>f.path.endsWith('.js')).text;
let mode='valid',browser,page;const groups=[],completion=suiteCompletionGuard('attachment-original',4,groups);
const server=createServer((req,res)=>{
 const path=new URL(req.url,'http://fixture.invalid').pathname;
 if(path==='/'){res.writeHead(200,{'Content-Type':'text/html'}).end('<html lang="pl"><div id="root"></div><script src="/app.js"></script></html>');return;}
 if(path==='/app.js'){res.writeHead(200,{'Content-Type':'text/javascript'}).end(js);return;}
 if(path===`/api/client/blob/${digest}/raw`){res.writeHead(mode==='missing'?404:200,{'Content-Type':'application/octet-stream'}).end(mode==='valid'?bytes:Buffer.from('wrong synthetic bytes'));return;}
 res.writeHead(200,{'Content-Type':'application/json'}).end('{}');
});await new Promise(r=>server.listen(0,'127.0.0.1',r));
const base='http://127.0.0.1:'+server.address().port;
const check=async(name,fn)=>{await fn();groups.push(name);console.log('[attachment-original] PASS '+name);};
try{
 browser=await chromium.launch();page=await browser.newPage({locale:'pl-PL'});let downloads=0;page.on('download',()=>downloads++);
 await check('exact original is downloaded with its filename',async()=>{await page.goto(base);const [d]=await Promise.all([page.waitForEvent('download'),page.getByTestId('message-attachment-original').click()]);assert.equal(d.suggestedFilename(),'oryginał.txt');const stream=await d.createReadStream();const chunks=[];for await(const c of stream)chunks.push(c);assert.deepEqual(Buffer.concat(chunks),bytes);});
 await check('hash mismatch refuses download visibly',async()=>{mode='bad';await page.reload();const before=downloads;await page.getByTestId('message-attachment-original').click();await page.getByTestId('attachment-original-error').waitFor();assert.match(await page.getByTestId('attachment-original-error').innerText(),/hashem|hash/);assert.equal(downloads,before);});
 await check('missing original fails without a fabricated file',async()=>{mode='missing';await page.reload();const before=downloads;await page.getByTestId('message-attachment-original').click();await page.getByTestId('attachment-original-error').waitFor();assert.equal(downloads,before);});
 await check('arbitrary file paths do not become download actions',async()=>{await page.goto(base+'/?plain');await page.getByTestId('download-fixture-ready').waitFor({state:'attached'});assert.equal(await page.getByTestId('message-attachment-original').count(),0);});
}finally{await browser?.close();await closeHttpFixture(server);}
completion.complete();console.log('[attachment-original] 4/4 groups passed; synthetic bytes only');
