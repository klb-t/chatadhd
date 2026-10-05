#!/usr/bin/env node
// Offline HTTP double; compiler spans/captures are the W4 golden fixture.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync, existsSync, mkdtempSync, mkdirSync, writeFileSync, rmSync } from "node:fs";
import { spawn } from "node:child_process";
import path from "node:path";
import { tmpdir } from "node:os";
import { stopChild } from "./harness-lifecycle.mjs";
import { createServer } from "node:http";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import { chromium } from "playwright";
const web = fileURLToPath(new URL("../", import.meta.url));
const fixture = JSON.parse(readFileSync(new URL("../../src/packet/tests/reply-fixtures.json", import.meta.url), "utf8")).cases[1];
const candidate = { schema: "loom.chat_graph_reply/1", mode: "answer_as_graph", status: "candidate", text: fixture.expected.response_text,
  retained_text: fixture.request.raw, compilation: fixture.expected, base_packet: fixture.request.packet,
  canonical_store_written: false, model_content_origin: "model", first_response_ref: { source_id: "offline-response-source", blob_hash: "recorded-fixture" } };
const groups = []; const pass = name => { groups.push(name); console.log(`[graph-chat-ui] PASS ${name}`); };
const bundle = await build({ stdin: { resolveDir: web, loader: "tsx", contents: `
  import React from "react"; import {createRoot} from "react-dom/client";
  import {LoomHttpApi} from "./src/api/loom-http";
  import GraphChatSettings from "./src/graph/GraphChatSettings";
  import GraphReplyWorkbench from "./src/components/GraphReplyWorkbench";
  import {instantiateGraphChatPreset} from "./src/graph/chat-graph-presets";
  import {patchNativeJson} from "./src/graph/native-json";
  import {nativeFragmentPrompt,recordedGraphReply,nativeGraphDisplayText} from "./src/graph/chat-graph";
  const request=(path,command)=>fetch(path,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(command)}).then(response=>response.json());
  const transport=new LoomHttpApi();
  window.patchJson=patchNativeJson;window.preset=instantiateGraphChatPreset;window.readGraph=recordedGraphReply;window.graphText=nativeGraphDisplayText;
  const root=createRoot(document.getElementById("root"));
  window.render=value=>root.render(<><GraphChatSettings transport={transport}/><GraphReplyWorkbench recordedReply={value} responseText={value?.retained_text||"raw preserved"} turnId="synthetic-stored-message" graphReply={command=>request("/api/graph-reply",command)} onNativeAddressFragment={(action,fragment)=>{window.address={action,fragment,prompt:nativeFragmentPrompt(action,fragment)}}}/></>);
  window.render(${JSON.stringify(candidate)});
` }, bundle: true, write: false, format: "iife", platform: "browser", outfile: "/tmp/loom-graph-chat-ui.js", jsx: "automatic" });
const js=bundle.outputFiles.find(file=>file.path.endsWith(".js")).text;
const css=bundle.outputFiles.find(file=>file.path.endsWith(".css"))?.text??"";
const commands=[],fragments=[],serverErrors=[];
let graph={},revision="initial",stale=false,tamper=false;
const modes=["off","answer_as_graph","text_plus_JSONgraph","separate_model_afterwards"];
const server=createServer(async(req,res)=>{
  if(req.method==="GET"&&req.url==="/api/methods/chat-settings") {
    commands.push({action:"chat_settings",http_method:"GET"});res.setHeader("Content-Type","application/json");res.end(JSON.stringify({scope:"global_native_config",graph_reply:graph,graph_reply_json:JSON.stringify(graph),context_execution:{graph_reply:graph},context_execution_sha256:revision,modes,profiles:[]}));return;
  }
  if(req.method!=="POST"){
    if(req.url==="/bundle.js"){res.setHeader("Content-Type","text/javascript");res.end(js);}
    else res.end(`<!doctype html><meta charset="utf-8"><style>${css}</style><div id="root"></div><script src="/bundle.js"></script>`);return;
  }
  let bytes="";for await(const chunk of req)bytes+=chunk;
  try{
    const command=JSON.parse(bytes);res.setHeader("Content-Type","application/json");
    if(req.url==="/api/methods"){
      commands.push(command);
      if(command.action==="profile_preview"){
        assert.equal(typeof command.profile_json,"string");const profile=JSON.parse(command.profile_json);
        const packet={schema:"loom.graph_packet/1",entities:profile.entities,claims:profile.claims,sources:profile.sources};
        const selection={},expected_rows={};for(const collection of ["entities","claims","sources"]){selection[collection]=packet[collection].map(row=>row.id);expected_rows[collection]=Object.fromEntries(selection[collection].map(id=>[id,null]));}
        const accept={operation:"accept",target:command.target,packet,selection,expected_rows,explicitly_accepted:true};
        const rawProfile=command.profile_json.replace(/"confidence"\s*:\s*1(?=\s*[,}])/g,'"confidence":1.0');const rawAccept=JSON.stringify(accept).replaceAll('"confidence":1,','"confidence":1.0,');
        res.end(JSON.stringify({profile,profile_json:rawProfile,accept_request:accept,accept_request_json:rawAccept}));return;
      }
      if(command.action==="accept"){assert.equal(typeof command.request_json,"string");assert.match(command.request_json,/"confidence":1\.0/);const accepted=JSON.parse(command.request_json);assert.equal(accepted.explicitly_accepted,true);res.end(JSON.stringify({receipt:{id:"native-method-library-receipt",acceptance_establishes_content_truth:false,selection:accepted.selection},row_drift:{matches:true}}));return;}
      if(command.action==="set_chat_settings"){
        if(stale||command.expected_context_execution_sha256!==revision){res.end(JSON.stringify({error:{code:"conflict",message:"Global native configuration changed; reload before saving."}}));return;}
        assert.equal(typeof command.graph_reply_json,"string");graph=JSON.parse(command.graph_reply_json);revision="saved";
      }else assert.equal(command.action,"chat_settings");
      res.end(JSON.stringify({scope:"global_native_config",graph_reply:graph,graph_reply_json:JSON.stringify(graph),context_execution:{graph_reply:graph},context_execution_sha256:revision,modes,profiles:[]}));
    }else{
      assert.equal(req.url,"/api/graph-reply");fragments.push(command);assert.equal(command.action,"fragment");assert.equal(command.message_id,"synthetic-stored-message");assert.equal(command.expected_compilation_sha256,fixture.expected.compilation_sha256);assert.equal(command.reply_result,undefined);assert.deepEqual(Object.keys(command.address),["local_id"]);
      const local=command.address.local_id,entity=fixture.expected.diff.entities.add.find(row=>row.id===fixture.expected.node_ids[local]);
      const span=fixture.expected.spans[local],source=fixture.expected.diff.sources.add.find(row=>row.observation.id===entity.attrs.source_observation_id);
      res.end(JSON.stringify({schema:"loom.graph_reply_fragment/1",local_id:local,node_id:entity.id,text:entity.attrs.text,span,
        source_observation_id:entity.attrs.source_observation_id,source_locator:{...source.observation.locator,byte_start:source.observation.locator.byte_start+span.byte_start,byte_len:span.byte_len},
        model_origin:entity.attrs.model_origin,compilation_sha256:tamper?"tampered":fixture.expected.compilation_sha256}));
    }
  }catch(error){serverErrors.push(error);res.statusCode=500;res.end(JSON.stringify({error:{message:String(error)}}));}
});
await new Promise(resolve=>server.listen(0,"127.0.0.1",resolve));
const browser=await chromium.launch({headless:true}),page=await browser.newPage();
try{
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  await page.getByTestId("graph-chat-settings").waitFor();
  assert.equal(commands.length,0,"Mount must not read or write native graph settings.");assert.match(await page.locator('[data-testid="graph-chat-settings"] > summary').textContent(),/not loaded/);
  await page.locator('[data-testid="graph-chat-settings"] > summary').click();
  await page.getByRole("combobox",{name:"Graph reply mode",exact:true}).waitFor();
  assert.deepEqual(await page.getByRole("combobox",{name:"Graph reply mode",exact:true}).locator("option").evaluateAll(rows=>rows.map(row=>row.value)),modes);
  assert.equal(commands[0]?.http_method,"GET");
  pass("Four modes read native global settings through genuine HTTP readonly GET; no ChatRequest extension");
  const presets=await page.evaluate(async()=>Promise.all(["answer_as_graph","text_plus_JSONgraph","separate_model_afterwards"].map(window.preset)));
  const canonical=value=>Array.isArray(value)?`[${value.map(canonical).join(",")}]`:value&&typeof value==="object"?`{${Object.entries(value).sort(([a],[b])=>a<b?-1:a>b?1:0).map(([key,item])=>`${JSON.stringify(key)}:${canonical(item)}`).join(",")}}`:JSON.stringify(value);
  for(const preset of presets){
    assert.equal(preset.profile.entities.length,5);assert.equal(preset.profile.claims.length,3);
    for(const entity of preset.profile.entities){const attrs=entity.attrs;if(attrs.definition)assert.equal(attrs.definition_sha256,createHash("sha256").update(canonical(attrs.definition)).digest("hex"));if(attrs.text)assert.equal(attrs.text_sha256,createHash("sha256").update(attrs.text).digest("hex"));}
    assert.equal(preset.transport.calls_authorized,true);assert.equal(preset.admission.mode,"candidate");
  }
  const patched=await page.evaluate(()=>{const raw='{"mode":"off","packet":{"confidence":1.0,"nested":[-0.0,1.25]},"text":'+JSON.stringify('quoted "string" } bracket')+'}';return window.patchJson(raw,["mode"],"answer_as_graph");});
  assert.match(patched,/"confidence":1\.0/);assert.match(patched,/\[-0\.0,1\.25\]/);
  pass("Data presets produce content-addressed native versions and unverified candidate admission");
  const original={model:"owner/chosen",messages:[{role:"system",content:"Retained context"},{role:"user",content:"Actual question"}],stream:true,temperature:0.73,max_tokens:431,plugins:[{id:"owner/plugin"}]};
  for(const preset of presets){
    const recipe=preset.profile.entities.find(e=>e.kind===preset.profile.vocabulary.kinds.recipe_version).attrs.definition;assert.deepEqual(recipe.request_bindings[0],{target:[],source:["request"]});
    let actual=structuredClone(recipe.request);const context={...preset.run_context,request:original,base_packet_sha256:"native-packet-hash",primary_text:"Retained primary answer"};
    for(const binding of recipe.request_bindings){let value=context;for(const key of binding.source)value=value[key];if(!binding.target.length){actual=structuredClone(value);continue;}let target=actual;for(const key of binding.target.slice(0,-1))target=target[key];target[binding.target.at(-1)]=structuredClone(value);}
    assert.equal(actual.model,original.model);assert.equal(actual.stream,true);assert.deepEqual(actual.plugins,original.plugins);
    if(preset.mode!=="separate_model_afterwards")assert.deepEqual(actual.messages,original.messages);else assert.equal(actual.messages[1].content,context.primary_text);
    const schema=actual.response_format.json_schema.schema;assert.equal((schema.properties.graph??schema).properties.base_packet_sha256.const,context.base_packet_sha256);
  }
  pass("Recipe binding preserves actual request controls/context and binds native packet identity");
  await page.getByRole("combobox",{name:"Graph reply data preset",exact:true}).selectOption("answer_as_graph");await page.getByRole("button",{name:"Load graph preset into draft",exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('[data-testid="graph-chat-settings-status"]')?.textContent?.includes("draft"));assert.equal(commands.filter(c=>c.action==="set_chat_settings").length,0);
  await page.getByRole("button",{name:"Install method definitions into graph",exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('[data-testid="graph-chat-settings-status"]')?.textContent?.includes("installed"));
  assert.equal(commands.filter(c=>c.action==="set_chat_settings").length,0);assert.equal(graph.mode,undefined);assert.equal(commands.filter(c=>c.action==="accept").length,1);
  await page.getByRole("button",{name:"Install method definitions into graph",exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('[data-testid="graph-chat-settings-status"]')?.textContent?.includes("installed"));
  assert.equal(commands.filter(c=>c.action==="accept").length,2);assert.match(commands.filter(c=>c.action==="profile_preview").at(-1).profile_json,/"confidence":1\.0/);assert.equal(commands.filter(c=>c.action==="set_chat_settings").length,0);
  pass("Method library installation uses an exact native acceptance receipt without activating chat");
  await page.getByTestId("save-graph-chat-settings").click();await page.waitForFunction(()=>document.querySelector('[data-testid="graph-chat-settings-status"]')?.textContent?.includes("saved"));
  assert.equal(graph.transport.calls_authorized,true);assert.equal(commands.at(-1).expected_context_execution_sha256,"initial");
  pass("Draft loading is inert; explicit save uses native CAS without a model call");
  stale=true;await page.getByRole("combobox",{name:"Graph reply mode",exact:true}).selectOption("off");await page.getByTestId("save-graph-chat-settings").click();await page.getByTestId("graph-chat-settings-error").waitFor();assert.equal(graph.mode,"answer_as_graph");
  pass("Concurrent global settings change stays visible and does not overwrite native state");
  assert.equal(await page.getByTestId("gr-fragment").count(),3);assert.equal(fragments.length,0);assert.equal(await page.getByTestId("gr-rendered-response").textContent(),"Aą🙂B");
  pass("Recorded native compilation is rendered directly with Unicode spans and model provenance");
  await page.getByRole("button",{name:"Expand this node",exact:true}).nth(1).click();await page.waitForFunction(()=>window.address?.action==="expand");assert.equal((await page.evaluate(()=>window.address)).fragment.text,"Aą🙂");assert.equal(fragments.length,1);
  await page.getByRole("button",{name:"Correct this node",exact:true}).nth(2).click();await page.waitForFunction(()=>window.address?.action==="correct");assert.match((await page.evaluate(()=>window.address)).prompt,/unverified/);assert.equal(fragments.length,2);
  pass("Expand/correct point-read the stored native capture by message and compilation hash; no model request");
  tamper=true;await page.evaluate(()=>{window.address=null});await page.getByRole("button",{name:"Expand this node",exact:true}).nth(1).click();await page.getByTestId("gr-error").waitFor();assert.equal(await page.evaluate(()=>window.address),null);assert.match(await page.getByTestId("gr-error").textContent(),/did not match/);
  pass("Mismatched native fragment hash cannot populate the composer");
  await page.evaluate(()=>window.render({schema:"loom.chat_graph_reply/1",mode:"text_plus_JSONgraph",status:"schema_error",text:"Readable original answer",retained_text:"malformed graph Ω🙂",error:{code:"invalid graph"},first_response_ref:{source_id:"retained-source"}}));
  await page.getByTestId("gr-recorded-error").waitFor();assert.match(await page.getByTestId("gr-original-response").textContent(),/malformed graph Ω🙂/);assert.equal(await page.getByTestId("gr-fragment").count(),0);
  assert.equal(await page.evaluate(()=>window.graphText(window.readGraph({graph_reply:{schema:"loom.chat_graph_reply/1",text:"Readable original answer",status:"schema_error"}}),"fallback")),"Readable original answer");assert.match(await page.getByTestId("gr-recorded-json").textContent(),/retained-source/);
  pass("Schema failure preserves readable text, exact retained response and source references");
  if(process.argv.includes("--native")) {
    const dataDir=mkdtempSync(path.join(tmpdir(),"loom-graph-chat-native-"));
    const evidenceDir=process.env.LOOM_GRAPH_CHAT_EVIDENCE_DIR;
    const nativeEvidence=[],providerEvidence=[];
    if(evidenceDir)mkdirSync(evidenceDir,{recursive:true});
    const captureNative=(endpoint,method,requestJson,responseStatus,responseJson)=>nativeEvidence.push({endpoint,method,request_json:endpoint.startsWith("/api/secrets/")?null:requestJson,response_status:responseStatus,response_json:responseJson});
    const nativeBin=process.env.LOOM_SERVER_BIN||fileURLToPath(new URL("../../build/dev/server/loom-server",import.meta.url));
    assert.ok(existsSync(nativeBin),"Build the current native server before --native.");
    const modelRequests=[],modelErrors=[];
    const fakeModel=createServer(async(req,res)=>{
      let bytes="";for await(const chunk of req)bytes+=chunk;
      try {
        assert.equal(req.method,"POST");assert.equal(req.url,"/chat/completions");
        const request=JSON.parse(bytes);modelRequests.push(request);
        const output=request.response_format?.json_schema?.schema;
        let content="Primary ordinary answer Ω🙂";
        if(output){const schema=output.properties.graph??output;const graph={schema:"loom.graph_reply/2",base_packet_sha256:schema.properties.base_packet_sha256.const,response_id:"root",nodes:[{id:"root",role:"response",text:"Native graph answer Ω🙂",children:[]}],links:[]};content=JSON.stringify(output.properties.graph?{text:"Readable graph answer Ω🙂",graph}:graph);}
        const responseJson=JSON.stringify({model:request.model,choices:[{message:{role:"assistant",content}}],usage:{prompt_tokens:10,completion_tokens:4,cost:0}});
        providerEvidence.push({endpoint:req.url,request_json:bytes,response_json:responseJson});res.setHeader("Content-Type","application/json");res.end(responseJson);
      }catch(error){modelErrors.push(error);res.statusCode=500;res.end(JSON.stringify({error:String(error)}));}
    });
    await new Promise(resolve=>fakeModel.listen(0,"127.0.0.1",resolve));
    const fakeBase=`http://127.0.0.1:${fakeModel.address().port}`;
    writeFileSync(path.join(dataDir,"config.json"),JSON.stringify({semantic_analysis:false,base_url:fakeBase,default_model:"offline/graph-chat-native",stream:false}));
    writeFileSync(path.join(dataDir,"models.json"),"[]");
    const probe=createServer();await new Promise(resolve=>probe.listen(0,"127.0.0.1",resolve));const port=probe.address().port;await new Promise(resolve=>probe.close(resolve));
    const base=`http://127.0.0.1:${port}`;let nativeLog="";
    const child=spawn(nativeBin,["--host","127.0.0.1","--port",String(port),"--data-dir",dataDir,"--token",""],{stdio:["ignore","pipe","pipe"]});
    child.stdout.on("data",chunk=>nativeLog+=chunk);child.stderr.on("data",chunk=>nativeLog+=chunk);
    const requestNative=async(endpoint,body,status=200,method="POST")=>{
      const response=await fetch(base+endpoint,{method,headers:{"Content-Type":"application/json"},...(body!==undefined?{body:JSON.stringify(body)}:{}),signal:AbortSignal.timeout(30000)});
      const responseJson=await response.text();captureNative(endpoint,method,body===undefined?null:JSON.stringify(body),response.status,responseJson);const result=JSON.parse(responseJson);assert.equal(response.status,status,JSON.stringify(result));if(status<400)assert.equal(result.error,undefined,JSON.stringify(result));return result;
    };
    try {
      let ready=false;for(let attempt=0;attempt<150;attempt++){try{if((await fetch(base+"/api/healthz",{signal:AbortSignal.timeout(1000)})).ok){ready=true;break;}}catch{}if(child.exitCode!==null)throw Error(nativeLog);await new Promise(resolve=>setTimeout(resolve,100));}
      assert.ok(ready,`Native server not ready: ${nativeLog}`);
      await requestNative("/api/secrets/api_key",{value:"synthetic-public-native-test-key"});
      const acceptedIds=[];let expectedHashes=[],lastInstalledProfile;
      for(const [presetIndex,rawPreset] of presets.entries()){
        const options=structuredClone(rawPreset);
        options.provider_manifests=[{id:"offline-graphchat-local-provider",display_name:"Offline local graph fixture",base_url:fakeBase,auth_scheme:"none",auth_secret:"",default_headers:{},capabilities:[{resource:"llm",name:"chat.completions",constraints:{}}],metadata:{chat_completions_path:"/chat/completions"}}];
        if(options.postprocess_transport)options.postprocess_transport.provider_id="offline-graphchat-local-provider";
        const before=await requestNative("/api/methods",{action:"chat_settings"});
        const preview=await requestNative("/api/methods",{action:"profile_preview",profile:options.profile,receipt_ids:[],target:"synthetic-graph-chat-methods",actor:"synthetic-native-web-ui",known_at:`2026-10-05T00:00:0${presetIndex}Z`});
        assert.equal(preview.canonical_store_written,false);assert.ok(preview.profile.sources.length>0);assert.equal(preview.resolution.leaves.length,1);assert.equal(preview.resolution.leaves[0].available,true);
        const accepted=await requestNative("/api/methods",{action:"accept",request_json:preview.accept_request_json});acceptedIds.push(accepted.receipt.id);options.profile=preview.profile;options.receipt_ids=[accepted.receipt.id];lastInstalledProfile=preview.profile_json;
        const exactOptions=await page.evaluate(({options,profileJson})=>window.patchJson(JSON.stringify(options),["profile"],null,profileJson),{options,profileJson:preview.profile_json});
        const installed=await requestNative("/api/methods",{action:"set_chat_settings",graph_reply_json:exactOptions,expected_context_execution_sha256:before.context_execution_sha256});
        assert.equal(installed.graph_reply.mode,options.mode);expectedHashes.push(installed.context_execution_sha256);
        const chatResponse=await fetch(base+"/api/chat",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({message:"Public fixture request Ω🙂",model:"offline/graph-chat-native",stream:false,trace_context:true}),signal:AbortSignal.timeout(30000)});
        assert.equal(chatResponse.status,200);const stream=await chatResponse.text();captureNative("/api/chat","POST",JSON.stringify({message:"Public fixture request Ω🙂",model:"offline/graph-chat-native",stream:false,trace_context:true}),chatResponse.status,stream);const chunks=stream.split(/\r?\n/).filter(line=>line.startsWith("data:")).map(line=>JSON.parse(line.slice(5).trim()));
        assert.equal(chunks.some(chunk=>chunk.type==="error"),false,stream);const done=chunks.find(chunk=>chunk.type==="done");assert.ok(done,stream);
        const message=await requestNative(`/api/messages/${encodeURIComponent(done.message_id)}`,undefined,200,"GET");const actual=message.metadata.graph_reply;
        assert.equal(actual.schema,"loom.chat_graph_reply/1");assert.equal(actual.status,"candidate",JSON.stringify(actual));assert.equal(actual.mode,options.mode);assert.ok(actual.compilation.compilation_sha256);assert.equal(actual.canonical_store_written,false);
        const fragment=await requestNative("/api/graph-reply",{action:"fragment",message_id:done.message_id,expected_compilation_sha256:actual.compilation.compilation_sha256,address:{local_id:"root"}});assert.equal(fragment.text,"Native graph answer Ω🙂");assert.equal(fragment.model_origin.kind,"model");assert.equal(fragment.compilation_sha256,actual.compilation.compilation_sha256);
        await requestNative("/api/graph-reply",{action:"fragment",message_id:done.message_id,expected_compilation_sha256:"tampered after capture",address:{local_id:"root"}},409);
        pass(`Native ${options.mode}: real method store, CAS activation, local ChatEngine dispatch and verified fragment`);
      }
      assert.equal(new Set(acceptedIds).size,3);assert.equal(modelRequests.length,4);assert.deepEqual(modelErrors,[]);
      const combined=await requestNative("/api/methods",{action:"load",profile_json:lastInstalledProfile,receipt_ids:acceptedIds});
      assert.equal(combined.receipts.length,3);assert.ok(Object.keys(combined.entities).length>5);
      const unchanged=await requestNative("/api/methods",{action:"chat_settings"});await requestNative("/api/methods",{action:"set_chat_settings",graph_reply:{mode:"off"},expected_context_execution_sha256:expectedHashes[0]},409);assert.equal((await requestNative("/api/methods",{action:"chat_settings"})).context_execution_sha256,unchanged.context_execution_sha256);
      pass("Native combined receipts retain separate capture occurrences; stale global activation rejects overwrite");
      console.log(`[graph-chat-ui] native provider requests=${modelRequests.length}; all served by loopback HTTP fixture; remote model calls=0`);
    } finally {await stopChild(child);fakeModel.closeAllConnections();await new Promise(resolve=>fakeModel.close(resolve));if(evidenceDir){writeFileSync(path.join(evidenceDir,"native-http.json"),JSON.stringify({schema:"loom.web_graph_chat_evidence/1",fixture:"public synthetic graph chat",remote_model_calls:0,calls:nativeEvidence},null,2));writeFileSync(path.join(evidenceDir,"loopback-provider.json"),JSON.stringify({schema:"loom.web_graph_chat_loopback_evidence/1",calls:providerEvidence},null,2));writeFileSync(path.join(evidenceDir,"native-server.log"),nativeLog);}rmSync(dataDir,{recursive:true,force:true});}
  }
  assert.deepEqual(serverErrors,[]);assert.equal(groups.length,process.argv.includes("--native")?14:10);console.log(`[graph-chat-ui] ${groups.length}/${groups.length} PASS; ${process.argv.includes("--native")?"real native ChatEngine + loopback HTTP fixture":"offline HTTP double"}; zero remote model calls`);
}finally{await browser.close();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
