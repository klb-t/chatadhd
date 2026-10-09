import assert from 'node:assert/strict';
import {createRequire,syncBuiltinESMExports} from 'node:module';
import {writeFileSync,readFileSync,renameSync} from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import http from 'node:http';import https from 'node:https';import net from 'node:net';import dgram from 'node:dgram';
const root=process.env.AUDIT_RESOURCE_REPO!,tmp=process.env.AUDIT_RESOURCE_TMP!;
const load=(p:string)=>import(pathToFileURL(path.join(root,p)).href),require=createRequire(path.join(root,'package.json'));
let network=0;const block=()=>{network++;throw new Error('AUDIT_NETWORK_BLOCKED');};
globalThis.fetch=block as any;http.request=block as any;http.get=block as any;https.request=block as any;https.get=block as any;net.Socket.prototype.connect=block as any;dgram.createSocket=block as any;syncBuiltinESMExports();
const sha=(b:Buffer|string)=>createHash('sha256').update(b).digest('hex');
const cases:any[]=[],observations:any={};
async function test(id:string,run:()=>Promise<any>|any){try{cases.push({id,phase:'acceptance',status:'PASS',evidence:await run()??{}});}catch(e:any){cases.push({id,phase:'acceptance',status:'FAIL',error:e.message});}}
const blocked=(id:string,reason:string)=>cases.push({id,phase:'acceptance',status:'BLOCKED_MISSING_CONTRACT',reason});
async function main(){
 const Database=require('better-sqlite3'),{runMigrations}=await load('backend/watchdog_api/db/migrations/index.ts');
 const {LocalFileSystemStore}=await load('backend/watchdog_api/storage/object_store/index.ts');
 const {WorkbenchRepository}=await load('backend/watchdog_api/db/repositories/workbench.ts');
 const {ResearchProjectsRepository}=await load('backend/watchdog_api/db/repositories/research_projects.ts');
 const {ResearchProjectsService}=await load('backend/watchdog_api/services/research_projects.ts');
 const {loadWorkbenchProfile}=await load('backend/watchdog_api/config/workbench.ts');
 const {defaultFigure,validateDataset}=await load('shared/workbench.ts');
 const {publicationSvg,researchPackage}=await load('backend/watchdog_api/workbench/publication.ts');
 const {readZip,createZip}=await load('backend/watchdog_api/utils/zip.ts');
 const {copySource,sourceStructure}=await load('backend/watchdog_api/sources/copy_plan.ts');
 const file=path.join(tmp,'resources.sqlite'),db=new Database(file);db.pragma('foreign_keys=ON');runMigrations(db);
 const store=new LocalFileSystemStore(path.join(tmp,'objects')),workbench=new WorkbenchRepository(db,store),repo=new ResearchProjectsRepository(db),service=new ResearchProjectsService(repo,store);
 const owner='synthetic-audit-owner',raw='x,y,unselected\n1,2,AUDIT_UNMAPPED_RAW\n3,4,AUDIT_UNMAPPED_RAW2\n';
 const dataset={version:'workbench-dataset-1',key:'audit-resources',name:'Synthetic resource fixture',description:'Publishable software fixture.',source:{url:'https://example.invalid/synthetic.csv',title:'Synthetic source',publisher:'Audit fixture',retrievedAt:'2026-10-09T00:00:00Z',license:'Synthetic fixture',sourceRecordId:'audit-source'},providerProfileId:'manual-table',measure:'Synthetic',normalization:'none',comparisonScope:'Audit only',languageMeaning:'unknown',rawInput:{mediaType:'text/csv',text:raw,separator:',',headerRecord:1},columns:['x','y'].map(key=>({key,label:key,type:'number',unit:null,semanticType:'score',description:'Synthetic numeric column'})),rows:[{id:'row1',values:{x:1,y:2},evidenceTier:'RAW_OBSERVATIONAL',qualityFlags:['SYNTHETIC'],missingReasons:{}},{id:'row2',values:{x:3,y:4},evidenceTier:'RAW_OBSERVATIONAL',qualityFlags:['SYNTHETIC'],missingReasons:{}}]};
 const data=await workbench.importDataset(dataset,owner,'audit-import');await workbench.approveDataset(data.id,owner,data.contentHash,false,'audit-review');const approved=await workbench.getDataset(data.id,owner);
 const resource=repo.resource(owner,'dataset',data.id)!;
 const revision=await service.save(owner,{name:'Synthetic frozen project',question:'Can source state change independently of a snapshot?',purpose:'Audit fixture',state:'DRAFT',meaning:'unspecified',links:[{kind:'dataset',id:data.id,expectedHash:resource.summary.hash,role:'Synthetic source',notes:''}]});
 const initial=await service.export(owner,revision.body.projectId,revision.hash);
 await test('A3-IMP-WD001.frozen-source-exact-bytes',async()=>{
  const entries=readZip(initial.bytes),desc=revision.body.links[0].files[0],bytes=entries.find((e:any)=>e.name===desc.path).content;
  assert.equal(sha(bytes),desc.sha256);assert.equal(JSON.parse(bytes.toString()).rawInput.text,raw);assert.equal(JSON.parse(bytes.toString()).source.url,dataset.source.url);
  return {archiveSha:initial.sha256,sourceFileSha:desc.sha256,unselectedRawFieldRetained:true};
 });
 await test('A3-IMP-WD002.changed-metadata-preserves-snapshot',async()=>{
  await workbench.revokeDataset(data.id,owner,'audit-revoke');const state=service.get(owner,revision.body.projectId);assert.equal(state.statuses[0].status,'CHANGED');assert.equal(state.revision.hash,revision.hash);assert.ok((await service.export(owner,revision.body.projectId,revision.hash)).bytes.equals(initial.bytes));return {status:state.statuses[0].status,archiveUnchanged:true};
 });
 await test('A3-IMP-WD003.unavailable-owned-source-preserves-history',async()=>{
  // Controlled database boundary fixture for loss of source ownership. The
  // source row is not deleted, and product permission checks are unchanged.
  db.prepare('UPDATE datasets SET owner_principal_id=? WHERE id=?').run('synthetic-other-owner',data.id);
  const state=service.get(owner,revision.body.projectId);assert.equal(state.statuses[0].status,'UNAVAILABLE');assert.equal(state.revision.hash,revision.hash);assert.ok((await service.export(owner,revision.body.projectId,revision.hash)).bytes.equals(initial.bytes));
  db.prepare('UPDATE datasets SET owner_principal_id=? WHERE id=?').run(owner,data.id);
  return {boundary:'simulated ownership change in real database; no source deletion',status:'UNAVAILABLE',archiveUnchanged:true};
 });
 await test('A3-IMP-WD004.missing-live-bytes-explicit-read-error',async()=>{
  const uri=resource.files[0].uri,local=uri.slice('file://'.length);renameSync(local,local+'.saved');
  try {await assert.rejects(()=>workbench.getDataset(data.id,owner,true),/ENOENT/);assert.ok((await service.export(owner,revision.body.projectId,revision.hash)).bytes.equals(initial.bytes));observations.missing_live_blob={projectStatus:service.get(owner,revision.body.projectId).statuses[0].status,statusScope:'Project status compares source metadata; byte-read availability is a separate real getDataset error.',frozenExportStillAvailable:true};}
  finally{renameSync(local+'.saved',local);}return observations.missing_live_blob;
 });
 await test('A3-IMP-WD005.native-reopen-frozen-source',async()=>{
  db.close();const reopened=new Database(file);try{const r=new ResearchProjectsRepository(reopened),s=new ResearchProjectsService(r,store);assert.equal(s.get(owner,revision.body.projectId).revision.hash,revision.hash);assert.ok((await s.export(owner,revision.body.projectId,revision.hash)).bytes.equals(initial.bytes));return {fileBackedSQLiteReopen:true};}finally{reopened.close();}
 });
 const profile=loadWorkbenchProfile(),figure=defaultFigure(approved,profile);figure.channels.x='x';figure.channels.y='y';
 await test('A3-IMP-WD006.headless-renderer-consumes-same-typed-record',()=>{
  const a=publicationSvg(approved,figure,profile),b=publicationSvg(approved,figure,profile);assert.equal(a,b);assert.ok(a.includes('<svg'));const bundle=researchPackage(approved,figure,profile,null),entries=readZip(bundle.bytes);assert.equal(entries.find((x:any)=>x.name==='source.csv').content.toString(),raw);assert.equal(entries.find((x:any)=>x.name==='figure.svg').content.toString(),a);return {svgSha:sha(a),rawSourceSha:sha(raw),browserStarted:false,consumer:'publicationSvg -> real shared FigureCanvas -> React renderToStaticMarkup'};
 });
 await test('A3-IMP-WD007.unknown-domain-fields-explicit-rejection',()=>{assert.throws(()=>validateDataset({...dataset,auditUnknown:'synthetic'}));assert.throws(()=>validateDataset({...dataset,version:'workbench-dataset-999'}));return {unknownDomainFieldRejected:true,unsupportedVersionRejected:true,note:'No generic archive retention or interpretation status implied.'};});
 await test('A3-IMP-WD008.explicit-mapping-preserves-lexemes-and-missingness',()=>{
  const source='{"meta":{"unmapped":"AUDIT_UNMAPPED"},"rows":[{"n":9007199254740993,"none":null},{}]}';const plan={version:'copy-plan-1',name:'Synthetic map',format:'json',rowsPointer:'/rows',fields:[{name:'number',selector:'/n',required:false},{name:'none',selector:'/none',required:false}]};
  const result=copySource(source,plan);assert.equal(result.records[0].number,'9007199254740993');assert.equal(result.provenance[0].none.explicitNull,true);assert.equal(result.provenance[1].none.missing,true);assert.equal(result.rawHash,sha(source));assert.equal(source.slice(result.provenance[0].number.startUtf16,result.provenance[0].number.endUtf16),'9007199254740993');assert.throws(()=>copySource(source,{...plan,rowsPointer:'/missing'}),/existing JSON array/);assert.throws(()=>copySource('{"rows":[{"n":1,"n":2}]}',plan),/Duplicate/);return {exactLexeme:true,rawHash:result.rawHash,unknownRowsPointerRejected:true,sourceStructure:sourceStructure(source,'json')};
 });
 await test('A3-IMP-WD009.nested-container-bytes-preserved-without-fake-parse',()=>{const inner=createZip([{name:'data.json',content:Buffer.from('{"synthetic":true}') }]);const outer=createZip([{name:'inner.zip',content:inner}]);const stored=readZip(outer)[0].content;assert.ok(stored.equals(inner));assert.equal(readZip(stored)[0].content.toString(),'{"synthetic":true}');return {scope:'Two explicit calls to existing store-only inspection reader; no claim of recursive generic ZIP reference API or compressed ZIP support.'};});
 blocked('A3-IMP-WD-REFERENCE-GRAPH','ResearchProjectsService pins typed domain resources and copies verified files. No generic ZIP-reference graph/query/selector API found in this path; creating one in the audit would be a copied implementation.');
 blocked('A3-IMP-WD-RESOURCE-POLICY-COMPOSITION','No source policy contract exposes independent embed/reference/both, eager/lazy, cache/index, snapshot/live, readonly/overlay/writeback in these existing domain services. Existing frozen project semantics are not advertised as generic resource-policy implementation.');
 blocked('A3-IMP-WD-INTERACTIVE-SAME-READ','Shared FigureCanvas is source-linked to frontend and actual headless SSR was executed. Browser E2E and view->same external resource resolver are not proven here.');
 await test('A3-IMP-WD-NETWORK-GUARD',()=>{assert.equal(network,0);return {attempts:network,block:'fetch/http/https/net/dgram'};});
 const files=['backend/watchdog_api/services/research_projects.ts','backend/watchdog_api/db/repositories/research_projects.ts','backend/watchdog_api/db/repositories/workbench.ts','backend/watchdog_api/storage/object_store/index.ts','backend/watchdog_api/workbench/publication.ts','backend/watchdog_api/utils/zip.ts','backend/watchdog_api/sources/copy_plan.ts','shared/csv_import.ts','shared/figure_renderer.tsx','shared/workbench.ts'];
 const receipt={schema:'klbt.audit.receipt/3',repo:'klb-t/Watchdog-JH16',sha:process.env.AUDIT_RESOURCE_SHA,suite:'Watchdog pinned resources and headless consumers',source_hashes:Object.fromEntries(files.map(f=>[f,sha(readFileSync(path.join(root,f)))])),tool_sha256:sha(readFileSync(new URL(import.meta.url))),cases,observations,counts:Object.fromEntries(['PASS','FAIL','BLOCKED_MISSING_CONTRACT'].map(s=>[s,cases.filter(c=>c.status===s).length])),network_attempts:network};
 writeFileSync(process.env.AUDIT_RESOURCE_OUT!,JSON.stringify(receipt,null,2)+'\n');console.log(JSON.stringify(receipt.counts));process.exitCode=cases.some(c=>c.status!=='PASS')?1:0;
}
main().catch(e=>{console.error(e);process.exitCode=2;});
