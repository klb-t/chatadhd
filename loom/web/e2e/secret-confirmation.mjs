#!/usr/bin/env node
// Actual ChatView + HTTP transport. Synthetic kernel events; no real secrets/model.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {fileURLToPath} from 'node:url';
import {build} from 'esbuild';
import {chromium} from 'playwright';
import {closeHttpFixture, suiteCompletionGuard} from './harness-lifecycle.mjs';
const root=fileURLToPath(new URL('../',import.meta.url));
const bundle=await build({stdin:{contents:`
import React from 'react'; import {createRoot} from 'react-dom/client';
import ChatView from './src/components/ChatView';
createRoot(document.getElementById('root')).render(<ChatView convId={null} onConversationCreated={()=>{}}/>);
`,resolveDir:root,loader:'tsx'},bundle:true,write:false,format:'iife',platform:'browser',jsx:'automatic',loader:{'.json':'json'},outfile:root+'secret-confirmation-fixture.js'});
const js=bundle.outputFiles.find(f=>f.path.endsWith('.js')).text;
const css=bundle.outputFiles.find(f=>f.path.endsWith('.css'))?.text??'';
const A='a'.repeat(64),B='b'.repeat(64),requests=[],groups=[],errors=[];
const completion=suiteCompletionGuard('secret-confirmation',4,groups);
const pending=(hash,status='needs_confirmation')=>({type:'secret_pending',request_id:'synthetic',secret_pending:{
 schema:'loom.chat_secret_pending/1',status,...(hash?{image_payload_sha256:hash}:{}),hits:[{rule:'synthetic',label:{en:'Synthetic marker',pl:'Syntetyczny znacznik'}}],
 text:{en:hash===B?'Confirm changed snapshot':'Confirm synthetic message',pl:'Potwierdź syntetyczną wiadomość'},
 actions:{send_anyway:{en:'Send anyway',pl:'Wyślij'},dont_send:{en:'Do not send',pl:'Nie wysyłaj'}}}});
const server=createServer(async(req,res)=>{
 const path=new URL(req.url,'http://fixture.invalid').pathname;
 const json=x=>{res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify(x));};
 if(path==='/') {res.writeHead(200,{'Content-Type':'text/html'});res.end('<html><head><link rel="stylesheet" href="/style.css"></head><body><div id="root"></div><script src="/app.js"></script></body></html>');return;}
 if(path==='/app.js'){res.writeHead(200,{'Content-Type':'text/javascript'});res.end(js);return;}
 if(path==='/style.css'){res.writeHead(200,{'Content-Type':'text/css'});res.end(css);return;}
 if(path==='/api/config')return json({});
 if(path==='/api/models')return json([]);
 if(path==='/api/chat'&&req.method==='POST'){
  const chunks=[];for await(const c of req)chunks.push(c);
  const body=JSON.parse(Buffer.concat(chunks).toString());requests.push(body);
  let event;
  if(body.message==='OCR synthetic') event=!body.secret_confirmation?pending(A):body.secret_confirmation.image_payload_sha256===A?pending(B):{type:'error',message:'Synthetic terminal after second confirmation'};
  else if(body.message==='Legacy synthetic')event=body.secret_confirmation?{type:'error',message:'Synthetic legacy terminal'}:pending();
  else if(body.message==='Refused synthetic')event=pending(A,'refused');
  else {res.writeHead(400).end();return;}
  res.writeHead(200,{'Content-Type':'text/event-stream'});res.end('data: '+JSON.stringify(event)+'\n\n');return;
 }
 res.writeHead(404,{'Content-Type':'application/json'});res.end('{"error":{"code":"fixture_unsupported","message":"Synthetic endpoint absent"}}');
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser,page;
const check=async(name,fn)=>{await fn();groups.push(name);console.log('[secret-confirmation] PASS '+name);};
const fresh=async text=>{await page.goto(`http://127.0.0.1:${server.address().port}`);await page.reload();await page.getByTestId('chat-input').fill(text);await page.getByTestId('send-chat').click();await page.getByTestId('chat-secret-pending').waitFor();};
try{
 browser=await chromium.launch();page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));
 await check('explicit confirmation carries the exact image digest and original question',async()=>{
  await fresh('OCR synthetic');assert.equal(requests.length,1);assert.equal(requests[0].secret_confirmation,undefined);
  await page.getByTestId('chat-secret-pending-confirm').click();
  await page.getByTestId('chat-secret-pending-title').filter({hasText:'Confirm changed snapshot'}).waitFor();
  assert.equal(requests.length,2);assert.equal(requests[1].message,requests[0].message);
  assert.deepEqual(requests[1].secret_confirmation,{approved:true,image_payload_sha256:A});
 });
 await check('changed pending snapshot requires another click and carries the new digest',async()=>{
  assert.equal(requests.length,2);await page.getByTestId('chat-secret-pending-confirm').click();
  await page.getByTestId('chat-error').filter({hasText:'Synthetic terminal after second confirmation'}).waitFor();
  assert.equal(requests.length,3);assert.deepEqual(requests[2].secret_confirmation,{approved:true,image_payload_sha256:B});
 });
 await check('legacy text confirmation remains compatible without an invented image digest',async()=>{
  await fresh('Legacy synthetic');await page.getByTestId('chat-secret-pending-confirm').click();
  await page.getByTestId('chat-error').filter({hasText:'Synthetic legacy terminal'}).waitFor();
  assert.equal(requests.length,5);assert.deepEqual(requests[4].secret_confirmation,{approved:true});
 });
 await check('refusal and dismissal cannot dispatch a confirmation',async()=>{
  await fresh('Refused synthetic');assert.equal(requests.length,6);
  assert.equal(await page.getByTestId('chat-secret-pending-confirm').count(),0);
  await page.getByTestId('chat-secret-pending-dismiss').click();await page.getByTestId('chat-secret-pending').waitFor({state:'detached'});
  assert.equal(requests.length,6);assert.deepEqual(errors,[]);
 });
}finally{await browser?.close();await closeHttpFixture(server);}
completion.complete();
console.log('[secret-confirmation] 4/4 groups passed; synthetic HTTP only');
