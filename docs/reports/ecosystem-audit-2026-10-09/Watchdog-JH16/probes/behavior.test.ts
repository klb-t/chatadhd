import test from 'node:test';
import assert from 'node:assert/strict';
import { pathToFileURL } from 'node:url';
import path from 'node:path';
const root = process.env.WATCHDOG_REPO ?? process.cwd();
const source = (p: string) => import(pathToFileURL(path.join(root, p)).href);

test('WD-002: ratio declares exclude but returns the same missing row as propagate', async () => {
  const { getPrimitive } = await source('backend/watchdog_api/analysis/primitives.ts');
  const ratio = getPrimitive('ratio');
  assert.ok(ratio.contract.missingPolicies.includes('exclude'));
  const inputs = {numerator:{name:'n',unit:'count',semanticType:'count',entityIds:['a','b'],values:[null,4]},denominator:{kind:'scalar',unit:'count',value:2}};
  const excluded=ratio.run(inputs,{missingPolicy:'exclude',params:{}}), propagated=ratio.run(inputs,{missingPolicy:'propagate',params:{}});
  assert.deepEqual(excluded,propagated);
  assert.deepEqual(excluded.series.entityIds,['a','b']);
  assert.deepEqual(excluded.series.values,[null,2]);
});

test('WD-003: generation request temperature/seed do not reach HTTP body', async () => {
  const { ProfileGenerator } = await source('backend/watchdog_api/llm/profile_generator.ts');
  let sent: any;
  const p={id:'audit-mock',protocol:'openai',endpoint:'https://example.invalid',headers:{},parameters:{},outputParameter:'max_tokens'};
  const credential={isPresent:true,detail:'synthetic mock only',use:(fn:any)=>fn('synthetic-audit-token')};
  const generator=new ProfileGenerator(p,credential,async (_url:any,init:any)=>{sent=JSON.parse(init.body); return {ok:true,status:200,text:async()=>JSON.stringify({model:'model-a',choices:[{message:{content:'test'}}]})};},'model-a',100);
  await generator.generate({prompt:'test',model:'model-a',params:{temperature:0,seed:7}});
  assert.equal(sent.temperature,undefined); assert.equal(sent.seed,undefined); assert.equal(sent.max_tokens,100);
});

test('WD-001: template id/version are metadata, not template selectors; output keeps literal top three',async()=>{
  const {generateNarrative,hashNarrativePayload}=await source('backend/watchdog_api/services/narrative.ts');
  const payload={presetId:null,results:[4,3,2,1].map((n,i)=>({metricKey:'Pi',entityId:['a','b','c','d'][i],valueNumeric:n})),missingCount:0,qualityFlags:[]};
  const req={runId:'audit',payload,payloadHash:hashNarrativePayload(payload),templateId:'first',templateVersion:'1',providerId:null,model:null};
  const a=generateNarrative(req),b=generateNarrative({...req,templateId:'another-template',templateVersion:'9'});
  assert.equal(a.content,b.content); assert.match(a.content,/highest were: a, b, c\./); assert.equal(a.approvalState,'PROPOSED');
});

test('WD-009: paper context is the first 24000 UTF-16 units, even when material follows later',async()=>{
  const {PaperIntakeService}=await source('backend/watchdog_api/services/paper_intake.ts');
  let prompt='', saved:any;
  const doc={id:'audit-doc',hash:'audit-hash',body:{text:'A'.repeat(24000)+'METHODOLOGY_AT_END',coverage:'full_text'},origins:[]};
  const repo={document:()=>doc,claimAssessment:()=> 'attempt',finishAssessment:(_o:any,_id:any,body:any)=>{saved=body;return body;}};
  const assistant={profile:{contentHash:'audit-profile'},propose:async(_o:any,_t:any,p:any)=>{prompt=p;return {text:JSON.stringify({methodology:[],dataRequirements:[],operations:[],ambiguities:[],hypotheses:[],alternatives:[]}),providerKey:'mock',model:'mock',route:{},reservationId:'mock'};}};
  await new PaperIntakeService(repo,assistant,{}).assess('audit-owner','audit-doc');
  assert.equal(saved.excerptCharacters,24000);assert.equal(saved.truncated,true);assert.equal(prompt.includes('METHODOLOGY_AT_END'),false);assert.equal(saved.approvalState,'PROPOSED');
});

test('WD-010: scheduler always coalesces missed interval slots',async()=>{
  const {nextOccurrence,RecurrenceSchema}=await source('shared/automation.ts');
  assert.equal(nextOccurrence({kind:'interval',minutes:15},new Date('2026-10-09T12:01:00Z'),'2026-10-09T10:00:00Z'),'2026-10-09T12:15:00.000Z');
  assert.equal(RecurrenceSchema.safeParse({kind:'interval',minutes:15,misfirePolicy:'catch_up'}).success,false);
});

test('WD-011: novel-number guard ignores the sign of a numeric value',async()=>{
  const {assertNoNovelNumbers}=await source('backend/watchdog_api/services/narrative.ts');
  assert.doesNotThrow(()=>assertNoNovelNumbers('Observed value: -15.','Observed value: 15.','mock'));
});
