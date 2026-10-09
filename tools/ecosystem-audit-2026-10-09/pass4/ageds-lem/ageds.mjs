// Run actual production modules headlessly. No browser/DOM/socket claim.
import {pathToFileURL} from 'node:url';
import path from 'node:path';
const base=process.argv[2];
const c=await import(pathToFileURL(path.join(base,'server/app/static/citations.mjs')));
const {createRangePlayer}=await import(pathToFileURL(path.join(base,'server/app/static/range-player.mjs')));
const tests=[];
function check(id,ok,observed){tests.push({id,phase:'contract',status:ok?'PASS':'FAIL',observed});}
function rejected(fn){try{fn();return false}catch{return true}}
const picked=[{segmentIndex:0,text:'synthetic A',start:0,end:1},{segmentIndex:1,text:' B',start:1,end:2}];
const selected=c.snapshotCitationSelection({artifactId:7,transcriptId:11,kind:'segments',picked});
picked[0].text='changed';picked.reverse();
check('A4-AG-WEB-SNAPSHOT',selected.quoteText==='synthetic A B'&&selected.requestBody.segmentIndices.join(',')==='0,1'&&Object.isFrozen(selected.selector.indices),'Frozen quote/ordered references survive caller mutation');
const response={id:9,artifact_id:7,derived_text_id:11,quote_text:selected.quoteText,start_ms:0,end_ms:2000,selector:JSON.parse(JSON.stringify(selected.selector))};
const reordered=Object.fromEntries(Object.entries(response.selector).reverse());
check('A4-AG-WEB-OBJECT-ORDER',c.validateCreatedCitation({...response,selector:reordered},selected).id===9,'Equivalent object key order is accepted');
check('A4-AG-WEB-LIST-ORDER',rejected(()=>c.validateCreatedCitation({...response,selector:{...response.selector,indices:[1,0]}},selected)),'List order changes occurrence and is rejected');
check('A4-AG-WEB-WRONG-VERSION',rejected(()=>c.validateCreatedCitation({...response,derived_text_id:12},selected)),'Identical quote with changed pinned version rejected');
check('A4-AG-WEB-EXACT-ID',rejected(()=>c.parseDomId('9007199254740993'))&&c.parseDomId('9007199254740991')===Number.MAX_SAFE_INTEGER,'Unsafe IDs rejected before conversion; source ID not rounded');
const version=id=>({id,artifact_id:7,run_id:null,model:null,language:null,created_at:'2026-10-09T00:00:00Z'});
const page=(ids,more=true)=>({artifactId:7,items:ids.map(version),nextBeforeId:more?ids.at(-1):null,snapshotMaxId:6,hasMore:more,limit:2});
const initial=page([6,5]);const pager=c.createHistoryPager('versions',7,initial);
const request1=pager.begin();pager.fail(request1);const request2=pager.begin();
check('A4-AG-HISTORY-RESUME',request2.beforeId===request1.beforeId&&request2.snapshotMaxId===6&&pager.count===2&&request2.token!==request1.token,'Failure retains cursor/snapshot; retry owns a new token');
check('A4-AG-HISTORY-STALE',pager.accept(request1,page([4,3]))===null&&pager.count===2,'Late interrupted request cannot mutate pager');
const bad=page([4,3]);bad.items[1].artifact_id=99;
const denied=rejected(()=>pager.accept(request2,bad));
check('A4-AG-HISTORY-ATOMIC-INVALID',denied&&pager.count===2&&pager.busy,'All rows validate before mutation; UI must call fail to release active token');
pager.fail(request2);const request3=pager.begin();const accepted=pager.accept(request3,page([4,3]));
check('A4-AG-HISTORY-RETRY',accepted.map(x=>x.id).join(',')==='4,3'&&pager.count===4&&pager.begin().beforeId===3,'Valid retry keeps original order and exact next cursor');
pager.close();check('A4-AG-HISTORY-CLOSED',pager.begin()===null,'Closed view cannot resume work');
const cap=c.historyRowBytes(initial.items[0]);const bounded=c.createHistoryPager('versions',7,initial,{maxPayloadBytes:cap});
check('A4-AG-HISTORY-PARTIAL',bounded.count===1&&bounded.hasMore&&bounded.reachedPayloadLimit&&bounded.begin()===null,'Payload cap exposes partial history and does not claim complete empty remainder');
class Audio extends EventTarget {readyState=0;duration=10;currentTime=0;starts=0;pauses=0;load(){}pause(){this.pauses++;this.dispatchEvent(new Event('pause'))}async play(){this.starts++;this.dispatchEvent(new Event('play'))}}
const audio=new Audio();const range=createRangePlayer(audio);
const first=range.play(1,2).then(()=> 'resolved',()=> 'cancelled');const second=range.play(3,4);audio.readyState=1;audio.dispatchEvent(new Event('loadedmetadata'));await second;
check('A4-AG-WEB-AUDIO-EPOCH',(await first)==='cancelled'&&audio.starts===1&&audio.currentTime===3,'Second play cancels first metadata wait before any first playback');
audio.currentTime=4;audio.dispatchEvent(new Event('timeupdate'));
check('A4-AG-WEB-AUDIO-END',audio.currentTime===4&&audio.pauses>=3,'Saved end pauses; no alignment claim');range.destroy();
console.log(JSON.stringify({schema:'klbt.audit.pass4.ageds-js/1',tests,boundaries:['Actual ES modules, no copied selection/pager implementation','EventTarget/audio fake is infrastructure; no Chromium, MediaPlayer, HTTP or audio accuracy claim','Synthetic transcript and version rows, no private source']}));
