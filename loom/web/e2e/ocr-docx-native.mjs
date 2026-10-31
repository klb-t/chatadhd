#!/usr/bin/env node
// New GUI acceptance: actual App/ImportPanel/ChatView, own native package,
// real local OCR and OOXML extraction; only a synthetic loopback provider.
import assert from 'node:assert/strict';
import {spawn, execFileSync} from 'node:child_process';
import {createServer} from 'node:http';
import {createHash} from 'node:crypto';
import {existsSync,mkdirSync,readFileSync,writeFileSync,readdirSync} from 'node:fs';
import {createRequire} from 'node:module';
import net from 'node:net';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {request,stopChild,closeHttpFixture,suiteCompletionGuard} from './harness-lifecycle.mjs';
import {makeTmpDir} from './tmp-dirs.mjs';
const loom=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const require=createRequire(path.join(loom,'web/package.json'));
const {chromium}=require('playwright');
const bin=process.env.LOOM_SERVER_BIN,dist=path.join(loom,'web/dist');
assert.ok(bin && path.resolve(bin).startsWith(path.dirname(loom)+path.sep) && existsSync(bin));
assert.ok(existsSync(path.join(dist,'index.html')),'use the existing own web build');
const evidence=path.resolve(process.env.LOOM_MODAL_EVIDENCE);mkdirSync(evidence,{recursive:true});
const tmp=makeTmpDir('loom-modal-gui-'),root=path.join(tmp,'root');mkdirSync(path.join(root,'profiles'),{recursive:true});
const png=path.join(loom,'tests/fixtures/local_ocr/synthetic-pl.png');
const expected=readFileSync(path.join(loom,'tests/fixtures/local_ocr/expected.txt'),'utf8').trim();
const docx=path.join(tmp,'Zażółć.docx');
execFileSync('/usr/bin/python3',['-B','-c',
  'import sys; from pathlib import Path; sys.path.insert(0,sys.argv[1]); import fixtures; Path(sys.argv[2]).write_bytes(fixtures.package())',
  path.join(loom,'tools/docx_basic/tests'),docx]);
const groups=[],wire=[],providerRequests=[],observations={},pageErrors=[];
const selected=new Set((process.env.LOOM_MODAL_CASES||'1,2,3,4,5,6,7').split(',').map(Number));
assert.ok(selected.size && [...selected].every(n=>Number.isInteger(n)&&n>=1&&n<=7));
assert.ok(!selected.has(2)||selected.has(1),'case 2 needs the import in case 1');
assert.ok(!selected.has(4)||selected.has(3),'case 4 needs the document in case 3');
const completion=suiteCompletionGuard('ocr-docx-native',selected.size,groups);
let caseNumber=0;
let server,provider,browser,context,page,port,base;
const sha=b=>createHash('sha256').update(b).digest('hex');
const freePort=()=>new Promise((resolve,reject)=>{const s=net.createServer();s.once('error',reject);s.listen(0,'127.0.0.1',()=>{const p=s.address().port;s.close(()=>resolve(p));});});
const until=async(fn,label,ms=30000)=>{for(const end=Date.now()+ms;Date.now()<end;){if(await fn())return;await new Promise(r=>setTimeout(r,100));}throw Error('timeout: '+label);};
const check=async(name,fn)=>{++caseNumber;if(!selected.has(caseNumber))return;await fn();groups.push(name);console.log('[ocr-docx-native] PASS '+name);await page.screenshot({path:path.join(evidence,String(groups.length).padStart(2,'0')+'.png'),fullPage:true});};
const inPage=(route,body,method=body===undefined?'GET':'PATCH')=>page.evaluate(async({route,body,method})=>{const r=await fetch(route,{method,headers:{'Content-Type':'application/json'},...(body===undefined?{}:{body:JSON.stringify(body)})});if(!r.ok)throw Error(route+': '+r.status);return r.json();},{route,body,method});
const dismiss=async()=>{if(await page.getByTestId('guest-notice-close').isVisible())await page.getByTestId('guest-notice-close').click();};
const newChat=async()=>{await dismiss();await page.getByTestId('shell-drawer-toggle').click();await page.getByTestId('shell-new-chat').click();while(await page.getByTestId('attachment-remove').count())await page.getByTestId('attachment-remove').first().click();};
const attach=async(file,name,mime)=>{await dismiss();await page.getByTestId('shell-attach').click();const [chooser]=await Promise.all([page.waitForEvent('filechooser'),page.getByTestId('composer-menu-attach_file').click()]);await chooser.setFiles({name,mimeType:mime,buffer:typeof file==='string'?readFileSync(file):file});await page.locator('[data-testid="attachment-chip"][data-state="ready"]').waitFor();};
const clickSend=async(text)=>{await dismiss();await page.getByTestId('chat-input').fill(text);await page.getByTestId('send-chat').click();if(await page.getByTestId('guest-terms-send').isVisible().catch(()=>false))await page.getByTestId('guest-terms-send').click();};
const send=async(text,answer)=>{const before=providerRequests.length;await clickSend(text);await until(()=>providerRequests.length===before+1,'one actual provider call');await page.getByText(answer,{exact:false}).last().waitFor();await page.getByTestId('streaming-message').waitFor({state:'detached'});};
const textOf=body=>body.messages.map(m=>typeof m.content==='string'?m.content:m.content.filter(p=>p.type==='text').map(p=>p.text).join('\n')).join('\n');
const events=text=>text.split('\n').filter(l=>l.startsWith('data:')).map(l=>{try{return JSON.parse(l.slice(5));}catch{return null;}}).filter(Boolean);
const pending=()=>wire.filter(r=>r.url.endsWith('/api/chat')).flatMap(r=>events(r.response)).filter(e=>e.secret_pending).at(-1)?.secret_pending;
const original=async(digest,bytes)=>{const actual=await page.evaluate(async h=>{const r=await fetch('/api/client/blob/'+h+'/raw');if(!r.ok)throw Error('original '+r.status);return Array.from(new Uint8Array(await r.arrayBuffer()));},digest);assert.equal(sha(Buffer.from(actual)),sha(bytes));};
const downloadOriginal=async(label,bytes)=>{
  const [download]=await Promise.all([page.waitForEvent('download'),page.getByTestId('message-attachment-original').first().click()]);
  assert.equal(download.suggestedFilename(),'Zażółć.docx');const target=path.join(evidence,'original-'+label+'.docx');await download.saveAs(target);
  assert.equal(sha(readFileSync(target)),sha(bytes));
};
const overlay=(directory,domain,overrides)=>{mkdirSync(path.join(directory,'profiles'),{recursive:true});writeFileSync(path.join(directory,'profiles',domain+'.pack'),JSON.stringify({schema:'loom.runtime_profile_overlay/1',domain,overrides}));};
const startServer=async()=>{server=spawn(bin,['--host','127.0.0.1','--port',String(port),'--accounts-root',root,'--static-dir',dist],{cwd:tmp,stdio:'ignore'});await until(async()=>{try{return(await request(base+'/api/healthz',{timeoutMs:1000})).ok;}catch{return false;}},'own server');};
try {
  port=await freePort();base='http://127.0.0.1:'+port;const providerPort=await freePort(),providerBase='http://127.0.0.1:'+providerPort;
  provider=createServer(async(req,res)=>{
    if(req.method!=='POST'){res.writeHead(200,{'Content-Type':'application/json'}).end('{"data":[]}');return;}
    const chunks=[];for await(const c of req)chunks.push(c);const body=JSON.parse(Buffer.concat(chunks));providerRequests.push(body);
    const text=textOf(body);let answer='SYNTHETIC_NO_DOCUMENT';
    const end='Koniec — po tabeli.',a=text.indexOf('# Zażółć gęślą jaźń'),b=text.indexOf(end,a);
    if(a>=0 && b>=0)answer='SYNTHETIC_DOCX: '+text.slice(a,b+end.length);
    else {const match=text.match(/Termin spotkania:[^\n]*/);if(match)answer='SYNTHETIC_OCR: '+match[0];}
    if(body.stream===false){res.writeHead(200,{'Content-Type':'application/json'}).end(JSON.stringify({choices:[{message:{role:'assistant',content:answer},finish_reason:'stop'}]}));return;}
    res.writeHead(200,{'Content-Type':'text/event-stream'});res.write('data: '+JSON.stringify({choices:[{delta:{content:answer},finish_reason:null}]})+'\n\n');res.end('data: '+JSON.stringify({choices:[{delta:{},finish_reason:'stop'}]})+'\n\ndata: [DONE]\n\n');
  });await new Promise(r=>provider.listen(providerPort,'127.0.0.1',r));
  overlay(root,'server_accounts',{allow_private_hosts:['127.0.0.1'],guests:{enabled:true,start:'auto',keys_file:'',sweep_interval_seconds:0,model_sources:['free'],workdir:{subdir:'guests',idle_ttl_seconds:0,max_bytes:0},protection:{per_session:{chat_per_minute:0}}}});
  overlay(root,'free_providers',{providers:[{id:'modal_fixture',display_name:'Syntetyczny dostawca GUI',base_url:providerBase,tier:'free',enabled:true,active_when:'none',auth_kind:'none',data_terms:{retention:'none',training:'no',human_review:false,observed_at:'2026-10-31',source_rating:'synthetic',source_url:'synthetic-fixture',summary:{pl:'Atrapa lokalna.',en:'Local double.'}}}],free_model_choice:{rank_source:'table',require:{training:'no'},table:[{model:'synthetic/modal-ui',provider:'modal_fixture',quality:1}]}});
  await startServer();browser=await chromium.launch();context=await browser.newContext({locale:'pl-PL',viewport:{width:420,height:900}});
  await context.exposeBinding('recordNative',(_source,row)=>wire.push(row));
  await context.addInitScript(()=>{
    localStorage.setItem('loom.store.policy',JSON.stringify({targets:['browser_indexeddb']}));
    const fetchOriginal=window.fetch.bind(window);window.fetch=async(...args)=>{const r=await fetchOriginal(...args);const url=typeof args[0]==='string'?args[0]:args[0].url;
      if(['/api/chat','/api/import','/api/client/blob/upload'].some(p=>url?.endsWith(p)))r.clone().text().then(response=>window.recordNative({url,body:typeof args[1]?.body==='string'?args[1].body:null,response})).catch(()=>{});return r;};
  });
  page=await context.newPage();page.on('pageerror',e=>pageErrors.push(e.message));await page.goto(base);await page.getByTestId('chat-input').waitFor();
  await inPage('/api/config',{llm_provider:'local',local_model_base_url:providerBase,semantic_analysis:false,semantic_model:'',auto_title:false,intent_routing_method:'off',stream:false,ui_language:'pl',chat_flow:'plain',chat_representation:'raw'});
  let importedId,docxId,docxTitle,digest;
  await check('ImportPanel: Polish screenshot imported by real local OCR, zero provider calls',async()=>{
    await dismiss();await page.getByTestId('shell-menu-toggle').click();await page.getByTestId('shell-menu-workshop').click();
    if(!await page.getByTestId('nav-import').isVisible())await page.getByTestId('toggle-nav').click();await page.getByTestId('nav-import').click();
    await page.getByTestId('import-file-input').setInputFiles(png);await page.getByTestId('start-import').click();await page.getByTestId('import-result').waitFor();
    await until(()=>wire.some(r=>r.url.endsWith('/api/import')),'native import receipt');
    const done=events(wire.find(r=>r.url.endsWith('/api/import')).response).find(e=>e.type==='done');assert.ok(done && !done.error);
    const convs=await inPage('/api/conversations');assert.equal(convs.length,1);importedId=convs[0].id;
    const rows=await inPage('/api/conversations/'+importedId+'/messages');assert.equal(rows[0].text,expected);assert.equal(providerRequests.length,0);
    observations.import={format:done.format,truth:done.truth,title:convs[0].title,summary:await page.getByTestId('import-summary').innerText(),sourceSha:sha(readFileSync(png)),messageText:rows[0].text};
  });
  await check('ChatView: imported OCR in actual context and context-derived synthetic reply',async()=>{
    if(await page.getByTestId('close-panel').isVisible())await page.getByTestId('close-panel').click();
    if(!await page.getByTestId('nav-chat').isVisible())await page.getByTestId('toggle-nav').click();await page.getByTestId('nav-chat').click();
    await page.getByTestId('shell-drawer-toggle').click();await page.getByTestId('shell-conv').first().click();
    assert.equal(await page.getByTestId('chat-conversation').getAttribute('data-conversation'),importedId);
    await send('Odczytaj termin spotkania z tekstu.','SYNTHETIC_OCR:');assert.ok(textOf(providerRequests.at(-1)).includes(expected));
    assert.ok(!JSON.stringify(providerRequests.at(-1)).includes('image_url'));assert.equal(await page.evaluate(()=>document.documentElement.lang),'pl');
  });
  await check('ChatView: DOCX upload, PL context, partial warnings and exact original',async()=>{
    await newChat();await attach(docx,'Zażółć.docx','application/vnd.openxmlformats-officedocument.wordprocessingml.document');await send('Odczytaj polski dokument i tabelę.','SYNTHETIC_DOCX:');
    const notice=page.getByTestId('message-attachment-notices').first();await notice.waitFor();assert.match(await notice.innerText(),/nie odtworzono pełnego formatowania/);
    assert.ok(textOf(providerRequests.at(-1)).includes('Kolumna A\tKolumna B'));docxId=await page.getByTestId('chat-conversation').getAttribute('data-conversation');
    const conv=await inPage('/api/conversations/'+docxId);docxTitle=conv.title;const rows=await inPage('/api/conversations/'+docxId+'/messages');
    digest=rows[0].attachments[0].split(':')[1].split('/')[0];await original(digest,readFileSync(docx));await downloadOriginal('before',readFileSync(docx));observations.docx={digest,warning:await notice.innerText(),conversation:docxId};
  });
  await check('DOCX GUI reopen after native restart preserves warnings, context and original',async()=>{
    await stopChild(server);await startServer();await page.close();page=await context.newPage();page.on('pageerror',e=>pageErrors.push(e.message));await page.goto(base);await page.getByTestId('chat-input').waitFor();await dismiss();
    await page.getByTestId('shell-drawer-toggle').click();await page.getByTestId('shell-conv').filter({hasText:docxTitle}).first().click();
    assert.equal(await page.getByTestId('chat-conversation').getAttribute('data-conversation'),docxId);
    await page.getByTestId('message-attachment-notices').first().waitFor();assert.match(await page.getByTestId('message-attachment-notices').first().innerText(),/nie odtworzono pełnego formatowania/);
    await send('Wróć do tabeli po ponownym otwarciu.','SYNTHETIC_DOCX:');assert.ok(textOf(providerRequests.at(-1)).includes('Koniec — po tabeli.'));await original(digest,readFileSync(docx));await downloadOriginal('reopened',readFileSync(docx));
  });
  await check('malformed DOCX is visibly refused before any provider call',async()=>{
    await newChat();const before=providerRequests.length;await attach(Buffer.from('synthetic broken ZIP'),'uszkodzony.docx','application/vnd.openxmlformats-officedocument.wordprocessingml.document');await clickSend('Odczytaj plik.');await page.getByTestId('chat-error').waitFor();assert.match(await page.getByTestId('chat-error').innerText(),/uszkodzony.docx/);assert.equal(providerRequests.length,before);observations.docxError=await page.getByTestId('chat-error').innerText();
  });
  await check('actual OCR secret confirmation sends exact native digest and original request',async()=>{
    const guests=readdirSync(path.join(root,'guests'));assert.equal(guests.length,1);
    overlay(path.join(root,'guests',guests[0]),'chat_secrets',{patterns:[{id:'synthetic_ocr_marker',regex:'godzina 09:00',flags:'',label:{pl:'syntetyczny znacznik OCR',en:'synthetic OCR marker'}}]});
    await inPage('/api/config',{secrets_in_chat:'ask'});await newChat();const before=providerRequests.length;await attach(png,'sekret-syntetyczny.png','image/png');await clickSend('Odczytaj tekst obrazu.');await page.getByTestId('chat-secret-pending').waitFor();await until(()=>pending()?.image_payload_sha256,'native pending digest');
    const held=pending();assert.match(held.image_payload_sha256,/^[a-f0-9]{64}$/);assert.equal(providerRequests.length,before);const first=wire.filter(r=>r.url.endsWith('/api/chat')).at(-1);const firstBody=JSON.parse(first.body);
    await page.getByTestId('chat-secret-pending-confirm').click();await until(()=>providerRequests.length===before+1,'single approved provider call');await page.getByText('SYNTHETIC_OCR:',{exact:false}).last().waitFor();await page.getByTestId('streaming-message').waitFor({state:'detached'});
    await until(()=>wire.filter(r=>r.url.endsWith('/api/chat')).some(r=>JSON.parse(r.body||'{}').secret_confirmation),'confirmation request recorded');
    const last=JSON.parse(wire.filter(r=>r.url.endsWith('/api/chat')).at(-1).body);assert.deepEqual(last.secret_confirmation,{approved:true,image_payload_sha256:held.image_payload_sha256});assert.equal(last.message,firstBody.message);assert.deepEqual(last.attachments,firstBody.attachments);assert.ok(textOf(providerRequests.at(-1)).includes(expected));observations.confirmation={digest:held.image_payload_sha256,providerCalls:1};
  });
  await check('never-send OCR refusal has no confirmation button and no provider dispatch',async()=>{
    await inPage('/api/config',{secrets_in_chat:'never'});await newChat();const before=providerRequests.length;await attach(png,'odmowa.png','image/png');await clickSend('Odczytaj tekst obrazu.');await page.getByTestId('chat-secret-pending').waitFor();assert.equal(await page.getByTestId('chat-secret-pending-confirm').count(),0);await page.getByTestId('chat-secret-pending-dismiss').click();await page.getByTestId('chat-secret-pending').waitFor({state:'detached'});assert.equal(providerRequests.length,before);assert.deepEqual(pageErrors,[]);
  });
} finally {
  if(page && groups.length!==selected.size){try{observations.failure=await page.locator('body').innerText();await page.screenshot({path:path.join(evidence,'failure.png'),fullPage:true});}catch{}}
  writeFileSync(path.join(evidence,'receipt.json'),JSON.stringify({selected:[...selected],groups,observations,pageErrors,providerRequests,wire},null,2));
  await browser?.close();await stopChild(server);if(provider)await closeHttpFixture(provider);
}
completion.complete();console.log(`[ocr-docx-native] ${groups.length}/${selected.size} selected groups passed; real backend, synthetic provider`);
