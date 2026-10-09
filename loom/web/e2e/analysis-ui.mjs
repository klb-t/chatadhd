// Native ScriptedTransport and browser fixture regressions. No paid/provider calls.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { createServer } from "node:http";
import { copyFile, mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { build } from "esbuild";
import { chromium } from "playwright";
import { closeHttpFixture, suiteCompletionGuard } from "./harness-lifecycle.mjs";

const executeFile = promisify(execFile);
const web = fileURLToPath(new URL("../", import.meta.url));
const loom = resolve(web, "..");
const nativeEnabled = !process.argv.includes("--browser-only");
const expectedGroups = nativeEnabled ? 5 : 4;
const groups = [], completion = suiteCompletionGuard("analysis-ui", expectedGroups, groups);
const pass = name => { groups.push(name); console.log(`[analysis-ui] PASS ${name}`); };
const hash = value => createHash("sha256").update(typeof value === "string" ? value : JSON.stringify(value)).digest("hex");
const contract = { schema: "loom.analysis_prompt/1", id: "synthetic.analysis", version: 1,
  messages: [{ role: "user", content: [{ binding: "text" }] }], request_parameters: { max_tokens: 50, native_ratio: 1 },
  transport: { method: "POST", path: "/chat/completions", timeout_ms: 30000, stream: false, headers: { "Content-Type": "application/json" } },
  analysis_parameters: {}, output_schema: { type: "object" }, validation_mode: "strict" };
const original = { id: "method-old", kind: "fixture.method_version", label: "Original method", attrs: { definition: { execution_capability: "offline", native_ratio: 1 } } };
let profile = { vocabulary: { kinds: { method_version: "fixture.method_version" }, predicates: { version_of: "fixture.version_of", uses_recipe: "fixture.uses_recipe" } }, entities: [original], claims: [
  { id: "original-identity-edge", subject: original.id, predicate: "fixture.version_of", object: "fixture-method-identity", assessment: { status: "active" } },
  { id: "original-recipe-edge", subject: original.id, predicate: "fixture.uses_recipe", object: "fixture-graph-chat-recipe", assessment: { status: "active" } }
], sources: [], selection: { parameter_layers: ["method", "user"] } };
let serial = 0, forceConfirmation = false, loseResult = false, modelCalls = 0;
const nativeText = value => JSON.stringify(value).replaceAll('"native_ratio":1', '"native_ratio":1.0');
const overlay = (base, patch) => Object.fromEntries(Object.entries({ ...base, ...patch }).map(([key, value]) => [key,
  value && typeof value === "object" && !Array.isArray(value) && base?.[key] && typeof base[key] === "object" ? overlay(base[key], value) : value]));
const commands = [], methodCommands = [], handles = new Map();
const bundle = await build({ stdin: { contents: `
  import React from "react"; import {createRoot} from "react-dom/client";
  import AnalysisPanel from "./src/components/AnalysisPanel";
  async function post(path,body,options) { const r=await fetch(path,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body),signal:options?.signal}); const j=await r.json(); if(!r.ok)throw Error(j.error.message); return j; }
  const transport={analysis:(c,o)=>post("/api/analysis",c,o),methods:(c,o)=>post("/api/methods",c,o)};
  createRoot(document.getElementById("root")).render(<AnalysisPanel transport={transport}/>);
`, resolveDir: web, loader: "tsx" }, bundle: true, write: false, format: "iife", platform: "browser", outfile: "/tmp/analysis-ui.js", jsx: "automatic" });
const js = bundle.outputFiles.find(item => item.path.endsWith(".js")).text;
const css = bundle.outputFiles.find(item => item.path.endsWith(".css"))?.text ?? "";
const sendJson = (response, body, status = 200) => { response.statusCode = status; response.setHeader("Content-Type", "application/json"); response.end(JSON.stringify(body)); };
const server = createServer(async (request, response) => {
  if (request.url === "/component.js") { response.setHeader("Content-Type", "text/javascript"); response.end(js); return; }
  if (request.url === "/component.css") { response.setHeader("Content-Type", "text/css"); response.end(css); return; }
  if (request.method !== "POST") { response.end('<!doctype html><link rel="stylesheet" href="/component.css"><div id="root"></div><script src="/component.js"></script>'); return; }
  const chunks = []; for await (const chunk of request) chunks.push(chunk);
  const body = JSON.parse(Buffer.concat(chunks).toString());
  if (request.url === "/api/methods") {
    methodCommands.push(body);
    if (body.action === "catalog") return sendJson(response, { profiles: [{ id: "fixture-profile", profile, profile_json: nativeText(profile), receipt_ids: [] }] });
    if (body.action === "load") {
      assert.equal(typeof body.profile_json, "string"); assert.match(body.profile_json, /"native_ratio":1\.0/);
      const loadedProfile = JSON.parse(body.profile_json);
      const loaded = { profile: loadedProfile, vocabulary: loadedProfile.vocabulary, entities: Object.fromEntries(loadedProfile.entities.map(entity => [entity.id, entity])), claims: loadedProfile.claims, receipts: (body.receipt_ids ?? []).map(receipt_id => ({ receipt_id })) };
      return sendJson(response, { ...loaded, profile_json: body.profile_json, snapshot_json: nativeText(loaded) });
    }
    if (body.action === "version_edit") {
      assert.match(body.snapshot_json, /"native_ratio":1\.0/);
      const loaded = JSON.parse(body.snapshot_json), attrs = overlay(original.attrs, JSON.parse(body.attrs_patch_json));
      assert.deepEqual(body.outgoing_claim_ids, ["original-identity-edge"]);
      assert.deepEqual(body.attrs_remove_paths, ["recipe_sha256", "prompt_sha256", "preset_sha256", "parameter_set_sha256"].map(key => ["definition", key]));
      const entity = { ...original, id: `method-new-${methodCommands.length}`, attrs };
      profile = { ...loaded.profile, entities: [...loaded.profile.entities, entity], claims: [...loaded.profile.claims,
        ...loaded.profile.claims.filter(claim => body.outgoing_claim_ids.includes(claim.id)).map(claim => ({ ...claim, id: `${claim.id}-${entity.id}`, subject: entity.id }))] };
      const accept_request = { operation: "accept", packet: { entities: profile.entities }, target: body.target, expected_rows: {}, explicitly_accepted: true };
      return sendJson(response, { new_version_id: entity.id, source_id: "native-source", definition_sha256: hash(attrs.definition), profile, profile_json: nativeText(profile),
        accept_request, accept_request_json: nativeText(accept_request) });
    }
    if (body.action === "accept") {
      assert.equal(typeof body.request_json, "string"); assert.match(body.request_json, /"native_ratio":1\.0/);
      return sendJson(response, { receipt: { id: `native-receipt-${methodCommands.length}`, explicitly_accepted: true } });
    }
  }
  if (request.url !== "/api/analysis") return sendJson(response, { error: { message: "unsupported fixture endpoint" } }, 404);
  commands.push(body);
  if (body.operation === "catalog") return sendJson(response, { contracts: [{ id: "semantic.analysis", version: 1 }], runtime: { model: "fixture-model", provider: "https://fixture.invalid" } });
  if (body.operation === "resolve") {
    let text = body.prompt_snapshot_json ?? nativeText(contract);
    if (body.prompt_patch?.validation_mode) text = text.replace(/"validation_mode"\s*:\s*"[^"]*"/, `"validation_mode":${JSON.stringify(body.prompt_patch.validation_mode)}`);
    return sendJson(response, { contract: JSON.parse(text), contract_json: text, contract_hash: hash(JSON.parse(text)) });
  }
  if (body.operation === "prepare") {
    const bytes = body.body_bytes ?? JSON.stringify({ model: body.model, messages: [{ role: "user", content: body.bindings.text }] });
    const value = { schema: "loom.analysis_prepared/1", prepared_id: `prepared-${++serial}`, request_identity_hash: hash(bytes + serial),
      request_hash: hash(bytes), contract_hash: hash(JSON.parse(body.prompt_snapshot_json)), contract: JSON.parse(body.prompt_snapshot_json),
      request: { body_bytes: bytes, body_json: JSON.parse(bytes), method: "POST", url: `${body.provider}/chat/completions`, headers: { Authorization: "[redacted]" } },
      graph_binding_available: Boolean(body.method), graph_plan: body.method ? { manifest: { bindings: { method_version_id: JSON.parse(body.method.selection_json).members[0].method_version_id } } } : null,
      status: "prepared", attempted: false, result: {}, forceConfirmation };
    handles.set(value.prepared_id, value); return sendJson(response, value);
  }
  const value = handles.get(body.prepared_id);
  if (!value) return sendJson(response, { error: { message: "prepared handle unavailable" } }, 404);
  if (body.operation === "discard") { handles.delete(body.prepared_id); return sendJson(response, { status: "discarded" }); }
  if (body.operation === "inspect") return sendJson(response, value);
  if (body.operation === "execute") {
    assert.equal(body.request_identity_hash, value.request_identity_hash);
    assert.equal(body.body_bytes, undefined); assert.equal(body.prompt_snapshot_json, undefined);
    if (value.attempted) return sendJson(response, { ...value, executed: false, replayed: true });
    if (value.forceConfirmation && !body.confirmation) {
      value.status = "requires_confirmation"; value.result = { usage_decision: { receipt_id: "receipt-exact", operation_id: value.prepared_id, status: "requires_confirmation", resources: { cost_usd: { estimate: null, projected: null } } } };
      return sendJson(response, { ...value, executed: false });
    }
    if (body.confirmation?.approved === false) { value.status = "denied"; return sendJson(response, { ...value, executed: false }); }
    modelCalls++; value.attempted = true; value.status = "completed";
    value.result = { raw_response_bytes: '{"result":true}', output_validation: { checked: true, valid: true }, first_response_ref: { blob_hash: hash("response") },
      candidate_packet: { entities: [], claims: [{ predicate: "fixture.produced_by_method_version" }] }, accept_request: { operation: "accept", packet: { entities: [] }, explicitly_accepted: true },
      accept_request_json: '{"operation":"accept","packet":{"entities":[],"native_ratio":1.0},"explicitly_accepted":true}' };
    if (loseResult) { loseResult = false; return sendJson(response, { error: { message: "fixture lost response" } }, 503); }
    return sendJson(response, { ...value, executed: true, replayed: false });
  }
  return sendJson(response, { error: { message: "unknown operation" } }, 400);
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
const browser = await chromium.launch({ headless: true });
try {
  const page = await browser.newPage();
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  await page.locator('[data-testid="analysis-contract"]').filter({ hasText: "" }).waitFor();
  await page.waitForFunction(() => document.querySelector('[data-testid="analysis-contract"]')?.value.includes("synthetic.analysis"));
  assert.match(await page.locator('[data-testid="analysis-contract"]').inputValue(), /"native_ratio":1\.0/);
  assert.equal(commands.some(command => command.operation === "prepare" || command.operation === "execute"), false);
  await page.locator('[data-testid="analysis-bindings"]').fill(JSON.stringify({ text: "source α🙂", input_json: {} }));
  await page.locator('[data-testid="analysis-prepare"]').click();
  await page.locator('[data-testid="analysis-prepared"]').waitFor();
  assert.equal(modelCalls, 0); assert.equal(await page.locator('[data-testid="analysis-send"]').isDisabled(), true);
  await page.locator('[data-testid="analysis-bindings"]').fill(JSON.stringify({ text: "changed draft", input_json: {} }));
  assert.equal(await page.locator('[data-testid="analysis-prepared"]').count(), 0);
  assert.equal(await page.locator('[data-testid="analysis-send"]').isDisabled(), true);
  pass("mount and preparation are read-only; editing invalidates held identity");

  await page.getByText("Save a new graph method version", { exact: true }).click();
  await page.locator('[data-testid="analysis-method-profile"]').selectOption("fixture-profile");
  await page.getByRole("button", { name: "Load native method versions", exact: true }).click();
  await page.locator('[data-testid="analysis-method-version"]').selectOption("method-old");
  await page.locator('[data-testid="analysis-method-target"]').fill("fixture-analysis-library");
  await page.locator('[data-testid="analysis-result-kind"]').fill("fixture.analysis_response");
  await page.locator('[data-testid="analysis-version-preview"]').click();
  await page.locator('[data-testid="analysis-version-request"]').waitFor();
  assert.equal(methodCommands.filter(command => command.action === "accept").length, 0);
  await page.locator('[data-testid="analysis-version-accept"]').click();
  await page.locator('[data-testid="analysis-method-ref"]').waitFor();
  assert.equal(profile.entities[0].attrs.definition.execution_capability, "offline");
  assert.equal(profile.entities.at(-1).attrs.definition.execution_capability, "analysis.http");
  assert.equal(profile.entities.at(-1).attrs.definition.analysis_result_kind, "fixture.analysis_response");
  assert.equal(modelCalls, 0);
  pass("method edits fork actual versions and save only through explicit native acceptance");

  const exact = '{  "model" : "fixture-model", "messages" : [{"role":"user","content":"exact α🙂"}]  }\n';
  await page.locator('[data-testid="analysis-body"]').fill(exact);
  await page.locator('[data-testid="analysis-prepare"]').click();
  await page.locator('[data-testid="analysis-prepared"]').waitFor();
  assert.equal(commands.filter(command => command.operation === "prepare").at(-1).body_bytes, exact);
  await page.locator('[data-testid="analysis-send"]').click();
  await page.locator('[data-testid="analysis-result-graph"]').waitFor();
  assert.equal(modelCalls, 1); assert.equal(await page.locator('[data-testid="analysis-send"]').isDisabled(), true);
  await page.locator('[data-testid="analysis-inspect"]').click();
  await page.waitForFunction(() => document.querySelector('[data-testid="analysis-notice"]')?.textContent.includes("Saved attempt inspected"));
  assert.equal(modelCalls, 1);
  await page.locator('[data-testid="analysis-result-accept"]').click();
  await page.locator('[data-testid="analysis-result-receipt"]').waitFor();
  assert.equal(commands.filter(command => command.operation === "execute").length, 1);
  pass("exact edited bytes dispatch once; result lineage acceptance and inspection do not resend");

  forceConfirmation = true;
  await page.locator('[data-testid="analysis-bindings"]').fill(JSON.stringify({ text: "confirmation", input_json: {} }));
  await page.locator('[data-testid="analysis-prepare"]').click();
  await page.locator('[data-testid="analysis-prepared"]').waitFor();
  await page.locator('[data-testid="analysis-send"]').click();
  await page.locator('[data-testid="analysis-receipt"]').waitFor();
  assert.equal(modelCalls, 1);
  await page.locator('[data-testid="analysis-confirm-ref"]').fill("owner-reviewed-exact-receipt");
  await page.locator('[data-testid="analysis-confirm"]').click();
  await page.locator('[data-testid="analysis-result-graph"]').waitFor();
  assert.deepEqual(commands.filter(command => command.operation === "execute").at(-1).confirmation, { receipt_id: "receipt-exact", approved: true, ref: "owner-reviewed-exact-receipt" });
  assert.equal(modelCalls, 2);
  forceConfirmation = false; loseResult = true;
  await page.locator('[data-testid="analysis-bindings"]').fill(JSON.stringify({ text: "lost result", input_json: {} }));
  await page.locator('[data-testid="analysis-prepare"]').click(); await page.locator('[data-testid="analysis-prepared"]').waitFor();
  await page.locator('[data-testid="analysis-send"]').click(); await page.locator('[data-testid="analysis-error"]').waitFor();
  assert.equal(await page.locator('[data-testid="analysis-send"]').isDisabled(), true);
  await page.locator('[data-testid="analysis-inspect"]').click(); await page.locator('[data-testid="analysis-result-graph"]').waitFor();
  assert.equal(modelCalls, 3);
  const broken = "{ broken draft";
  await page.locator('[data-testid="analysis-request-patch"]').fill(broken); await page.locator('[data-testid="analysis-prepare"]').click();
  await page.locator('[data-testid="analysis-error"]').waitFor();
  assert.equal(await page.locator('[data-testid="analysis-request-patch"]').inputValue(), broken);
  assert.equal(modelCalls, 3);
  pass("confirmation binds held identity; unknown outcomes inspect without retries; invalid drafts survive");
} finally { await browser.close(); await closeHttpFixture(server); }

const native = String.raw`
#include "analysis-ui-routes.h"
#include "loom/usage_policy.h"
#include "loom/util/fs.h"
#include <cassert>
#include <iostream>
using namespace loom;
using loom_server::native_ui::Json;
template<class T> T must(Result<T> result) { if(!result) throw std::runtime_error(result.error().to_string()); return std::move(result).value(); }
int main(int argc,char** argv) {
 assert(argc==2);
 auto empty_stream=must(loom_server::analysis_ui_detail::decode("data: [DONE]\n\n",true));
 assert(!empty_stream["choices"][0]["message"].contains("content"));
 auto service_stream=must(loom_server::analysis_ui_detail::decode("data: {\"error\":{\"message\":\"scripted service error\"}}\n\ndata: [DONE]\n\n",true));
 assert(service_stream.contains("error")&&!service_stream["choices"][0]["message"].contains("content"));
 auto model_stream=must(loom_server::analysis_ui_detail::decode("data: {\"model\":\"scripted-model\",\"choices\":[{\"delta\":{\"content\":\"{}\"}}]}\n\ndata: [DONE]\n\n",true));
 assert(model_stream["model"]=="scripted-model"&&model_stream["choices"][0]["message"]["content"]=="{}");
 fsutil::TempDir dir; auto http=std::make_shared<net::ScriptedTransport>(); RuntimeOptions options;
 options.data_dir=dir.path().string(); options.start_workers=false; options.http=http;
 LoomContext context; context.rt=must(Runtime::open(options)); auto& rt=*context.rt;
 rt.config().set("loom_usage_policy", usage_policy_defaults()); rt.secrets().set("api_key", "native-fixture-private-key");
 auto contract=must(extract::prompts::resolve("semantic.analysis"));
 contract.definition["validation_mode"]="strict"; contract.definition["output_schema"]=Json{{"type","object"},{"required",Json::array({"result"})},{"properties",{{"result",{{"type","boolean"}}}}}};
 contract=must(extract::prompts::from_snapshot(contract.definition));
 // Portability regression: inspect the actual consumed native limit, not only
 // the echoed execution JSON. Preparing every boundary stays dispatch/ledger-free.
 {
  loom_server::analysis_ui_detail::State limits_state;
  auto definition=contract.definition;
  definition["analysis_parameters"]["limits"]["max_response_bytes"]=4096;
  Json limit_command{{"operation","prepare"},{"prompt_snapshot",definition},{"model","offline/test"},
   {"provider","https://scripted.invalid"},{"bindings",{{"text","synthetic response limit boundary"}}}};
  const auto maximum=std::numeric_limits<std::size_t>::max();
  auto check_limit=[&](const Json& execution,std::size_t expected) {
   limit_command["execution"]=execution;
   const auto held=must(loom_server::analysis_ui_detail::dispatch(&context,limits_state,limit_command));
   assert(held["attempted"]==false&&held["status"]=="prepared");
   const auto id=held["prepared_id"].get<std::string>();
   assert(limits_state.prepared.at(id)->max_response_bytes==expected);
   assert(http->requests().empty()&&!std::filesystem::exists(dir.path()/"usage-policy.sqlite"));
   must(loom_server::analysis_ui_detail::dispatch(&context,limits_state,Json{{"operation","discard"},{"prepared_id",id}}));
   assert(limits_state.prepared.empty());
  };
  auto reject_limit=[&](const Json& value) {
   limit_command["execution"]={{"max_response_bytes",value}};
   auto rejected=loom_server::analysis_ui_detail::dispatch(&context,limits_state,limit_command);
   assert(!rejected&&rejected.error().code==Errc::InvalidArgument);
   assert(limits_state.prepared.empty()&&http->requests().empty()&&!std::filesystem::exists(dir.path()/"usage-policy.sqlite"));
  };
  check_limit(Json::object(),4096);
  check_limit(Json{{"max_response_bytes",nullptr}},maximum);
  check_limit(Json{{"max_response_bytes",std::int64_t{0}}},0);
  check_limit(Json{{"max_response_bytes",std::int64_t{17}}},17);
  for(const auto value:{std::uint64_t{9007199254740993ULL},std::numeric_limits<std::uint64_t>::max()}) {
   if(value>maximum) reject_limit(Json(value));
   else check_limit(Json{{"max_response_bytes",value}},static_cast<std::size_t>(value));
  }
  const auto signed_large=std::int64_t{9007199254740993LL};
  if(static_cast<std::uint64_t>(signed_large)>maximum) reject_limit(Json(signed_large));
  else check_limit(Json{{"max_response_bytes",signed_large}},static_cast<std::size_t>(signed_large));
  check_limit(Json{{"max_response_bytes",maximum}},maximum);
  for(const auto& invalid:Json::array({-1,std::numeric_limits<std::int64_t>::min(),1.0,1.5,"1",true,Json::array(),Json::object()})) reject_limit(invalid);
  limit_command["prompt_snapshot"]["analysis_parameters"]["limits"]["max_response_bytes"]=nullptr;
  check_limit(Json::object(),maximum);
  limit_command["prompt_snapshot"]["analysis_parameters"]["limits"].erase("max_response_bytes");
  check_limit(Json::object(),maximum);
 }
 const std::string known="2026-10-05T00:00:00Z";
 const Json origin{{"kind","user"},{"actor","synthetic-test-owner"},{"model",nullptr},{"recipe_sha256",nullptr},{"response_sha256",nullptr}};
 // Install the actual owner-data graph-chat preset, including its real recipe
 // and prompt relations, through exactly the native profile preview/accept API.
 const auto data=must(json::parse(must(fsutil::read_file(argv[1]))));
 const auto preset=data["presets"][0];
 const auto kinds=data["vocabulary"]["kinds"],predicates=data["vocabulary"]["predicates"];
 Json profile{{"vocabulary",data["vocabulary"]},{"entities",Json::array()},{"claims",Json::array()},{"sources",Json::array()},{"selection",preset["selection"]}};
 std::map<std::string,std::string> ids;
 for(const auto& spec:preset["entities"]) {
  Json entity=data["entity_template"]; Json attrs=spec["attrs"]; std::string version_hash;
  if(attrs.contains("definition")) {version_hash=loom_server::method_ui::hash(attrs["definition"]);attrs["definition_sha256"]=version_hash;}
  if(attrs.contains("text")) {version_hash=Sha256::hex(attrs["text"].get<std::string>());attrs["text_sha256"]=version_hash;}
  const auto base=spec["id"].get<std::string>(); const auto id=version_hash.empty()?base:base+":"+version_hash; ids[base]=id;
  entity["id"]=id;entity["canonical_key"]=id;entity["kind"]=kinds[spec["role"].get<std::string>()];entity["label"]=spec["label"];entity["attrs"]=attrs;
  profile["entities"].push_back(entity);
 }
 for(const auto& spec:preset["claims"]) {
  Json row=data["claim_template"];row["subject"]=ids.at(spec["subject"].get<std::string>());row["object"]=ids.at(spec["object"].get<std::string>());row["predicate"]=predicates[spec["predicate"].get<std::string>()];
  row["id"]="graph-preset-claim:"+loom_server::method_ui::hash(Json{{"subject",row["subject"]},{"predicate",row["predicate"]},{"object",row["object"]}});
  auto claim=must(model::Claim::from_json(row));claim.id=model::Claim::make_id(claim.subject,claim.predicate,claim.object,claim.value,claim.qualifiers);profile["claims"].push_back(claim.to_json());
 }
 for(auto& member:profile["selection"]["members"]) member["method_version_id"]=ids.at(member["method_version_id"].get<std::string>());
 const std::string version_id=profile["selection"]["members"][0]["method_version_id"];
 context::MethodRegistry registry(rt.db());
 auto installation=must(loom_server::method_ui::execute(rt,Json{{"action","profile_preview"},{"profile_json",profile.dump()},{"target","synthetic-analysis-library"},{"actor","synthetic-owner"},{"known_at",known}}));
 auto installed=must(loom_server::method_ui::execute(rt,Json{{"action","accept"},{"request_json",installation["accept_request_json"]}}));
 const auto original_receipt=installed["receipt"]["id"].get<std::string>();
 auto snapshot=must(registry.load(installation["profile"],{original_receipt}));
 auto original_plan=must(registry.resolve(snapshot,Json::object(),loom_server::method_ui::capabilities()));
 assert(!original_plan["leaves"][0]["recipe"].is_null()&&!original_plan["leaves"][0]["prompt"].is_null());
 Json identity_claims=Json::array();
 for(const auto& claim:snapshot["claims"]) if(claim["subject"]==version_id&&claim["predicate"]==predicates["version_of"]&&claim["assessment"]["status"]=="active") identity_claims.push_back(claim["id"]);
 assert(!identity_claims.empty());
 Json attrs_patch{{"definition",{{"execution_capability","analysis.http"},{"analysis_contract",contract.definition},{"analysis_contract_sha256",contract.hash},{"analysis_result_kind","synthetic.analysis_response"}}}};
 Json remove_paths=Json::array();for(const auto* key:{"recipe_sha256","prompt_sha256","preset_sha256","parameter_set_sha256"})remove_paths.push_back(Json::array({"definition",key}));
 const Json edited=must(loom_server::method_ui::execute(rt,Json{{"action","version_edit"},{"snapshot_json",snapshot.dump()},{"entity_id",version_id},{"attrs_patch_json",attrs_patch.dump()},{"outgoing_claim_ids",identity_claims},{"attrs_remove_paths",remove_paths},{"target","synthetic-analysis-library"},{"actor","synthetic-owner"},{"known_at",known}}));
 assert(edited["profile_json"].get<std::string>().find("1.0")!=std::string::npos);
 auto accepted=must(registry.accept(must(json::parse(edited["accept_request_json"].get<std::string>()))));
 const auto receipt=accepted["receipt"]["id"];
 const std::string bytes="{  \"model\" : \"offline/test\", \"messages\" : [{\"role\":\"user\",\"content\":\"exact α🙂\"}]  }\n";
 Json command{{"operation","prepare"},{"prompt_snapshot",contract.definition},{"model","offline/test"},{"provider","https://scripted.invalid"},{"bindings",{{"text","synthetic source"}}},{"body_bytes",bytes},
 {"method",{{"profile_json",edited["profile_json"]},{"receipt_ids",Json::array({original_receipt,receipt})},{"selection_json",Json{{"members",Json::array({Json{{"method_version_id",edited["new_version_id"]}}})},{"fusion",nullptr}}.dump()},{"target","synthetic-analysis-library"},{"origin",origin},{"known_at",known}}}};
 loom_server::analysis_ui_detail::State state;
 for(const auto* field:{"profile_json","selection_json"}) {
  auto duplicate=command;const auto original=duplicate["method"][field].get<std::string>();
  duplicate["method"][field]="{\"native_duplicate\":0,\"native_duplicate\":1,"+original.substr(1);
  auto rejected=loom_server::analysis_ui_detail::dispatch(&context,state,duplicate);
  assert(!rejected&&rejected.error().code==Errc::Parse);
  assert(http->requests().empty()&&!std::filesystem::exists(dir.path()/"usage-policy.sqlite"));
 }
 auto prepared=must(loom_server::analysis_ui_detail::dispatch(&context,state,command));
 assert(http->requests().empty()); assert(!std::filesystem::exists(dir.path()/"usage-policy.sqlite"));
 assert(prepared.dump().find("native-fixture-private-key")==std::string::npos); assert(prepared["request"]["body_bytes"]==bytes); assert(prepared["graph_binding_available"]==true);
 const Json payload{{"choices",Json::array({Json{{"message",{{"content","{\"result\":true}"}}},{"finish_reason","stop"}}})},{"usage",{{"completion_tokens",1}}},{"model","offline/test"}};
 http->expect("POST","https://scripted.invalid",net::ScriptedTransport::Reply::json(200,payload));
 Json dispatch{{"operation","execute"},{"prepared_id",prepared["prepared_id"]},{"request_identity_hash",prepared["request_identity_hash"]}};
 auto result=must(loom_server::analysis_ui_detail::dispatch(&context,state,dispatch));
 if(result["status"]!="completed") throw std::runtime_error(result.dump());
 assert(http->requests().size()==1); assert(http->requests()[0].body==bytes); assert(net::header_value(http->requests()[0].headers,"Authorization")=="Bearer native-fixture-private-key");
 assert(result["result"]["output_validation"]["valid"]==true); assert(result["result"]["graph_binding_status"]=="native_result_run_version_edges");
 const auto result_id=result["result"]["result_entity_id"]; bool run_edge=false,version_edge=false;
 for(const auto& edge:result["result"]["candidate_packet"]["claims"]) if(edge["subject"]==result_id) {run_edge=run_edge||edge["predicate"]==predicates["produced_in_run"];version_edge=version_edge||edge["predicate"]==predicates["produced_by_method_version"];}
 assert(run_edge&&version_edge); auto stored=must(registry.accept(must(json::parse(result["result"]["accept_request_json"].get<std::string>())))); assert(stored.contains("receipt"));
 auto replay=must(loom_server::analysis_ui_detail::dispatch(&context,state,dispatch)); assert(replay["replayed"]==true); assert(http->requests().size()==1);
 command["execution"]={{"estimated_output_tokens",100}}; auto held=must(loom_server::analysis_ui_detail::dispatch(&context,state,command));
 dispatch["prepared_id"]=held["prepared_id"]; dispatch["request_identity_hash"]=held["request_identity_hash"];
 auto pause=must(loom_server::analysis_ui_detail::dispatch(&context,state,dispatch)); assert(pause["status"]=="requires_confirmation"); assert(http->requests().size()==1);
 dispatch["confirmation"]={{"receipt_id",pause["result"]["usage_decision"]["receipt_id"]},{"approved",true},{"ref","synthetic-exact-owner-confirmation"}};
 http->expect("POST","https://scripted.invalid",net::ScriptedTransport::Reply::json(200,payload));
 auto confirmed=must(loom_server::analysis_ui_detail::dispatch(&context,state,dispatch)); if(confirmed["status"]!="completed")throw std::runtime_error(confirmed.dump()); assert(http->requests().size()==2);
 auto frozen=command; frozen.erase("method"); auto raw=must(loom_server::analysis_ui_detail::dispatch(&context,state,frozen)); dispatch={{"operation","execute"},{"prepared_id",raw["prepared_id"]},{"request_identity_hash",raw["request_identity_hash"]}};
 auto unavailable=loom_server::analysis_ui_detail::dispatch(&context,state,dispatch); assert(!unavailable&&unavailable.error().code==Errc::Unavailable); assert(http->requests().size()==2);
 command["execution"]=Json::object();
 auto attempt=[&](net::ScriptedTransport::Reply reply) {
  auto fresh=must(loom_server::analysis_ui_detail::dispatch(&context,state,command));
  http->expect("POST","https://scripted.invalid",std::move(reply));
  return must(loom_server::analysis_ui_detail::dispatch(&context,state,Json{{"operation","execute"},{"prepared_id",fresh["prepared_id"]},{"request_identity_hash",fresh["request_identity_hash"]}}));
 };
 auto invalid_model=payload; invalid_model["model"]="";invalid_model["choices"][0]["message"]["content"]="{\"wrong\":true}";
 auto rejected=attempt(net::ScriptedTransport::Reply::json(200,invalid_model));
 assert(rejected["status"]=="schema_rejected"&&rejected["result"]["model_content_available"]==true);
 assert(rejected["result"]["candidate_packet"]["entities"].size()>0);
 for(const auto& entity:rejected["result"]["candidate_packet"]["entities"]) if(entity["id"]==rejected["result"]["result_entity_id"]) assert(entity["attrs"]["model_identity_basis"]=="requested_unverified");
 auto service=attempt(net::ScriptedTransport::Reply::json(500,Json{{"error",{{"message","scripted service error"}}}}));
 assert(service["status"]=="transport_error"&&service["result"]["model_content_available"]==false&&service["result"]["content_truth"]=="service_or_transport_result");
 for(const auto& entity:service["result"]["candidate_packet"]["entities"]) if(entity["id"]==service["result"]["result_entity_id"]) assert(!entity["attrs"].contains("model_origin"));
 std::string binary_response(1,static_cast<char>(0xff)); binary_response+="scripted binary error";
 auto binary_service=attempt(net::ScriptedTransport::Reply::text(503,binary_response));
 assert(binary_service["result"]["raw_response_bytes"].is_null());
 assert(must(base64::decode(binary_service["result"]["raw_response_base64"].get<std::string>(),true))==binary_response);
 auto ledger=must(UsagePolicy::open(dir.path()/"usage-policy.sqlite",usage_policy_defaults()));
 auto accounting=must(ledger->inspect(prepared["usage_estimate"]["baseline_key"].get<std::string>()));
 bool found_binary=false;
 for(const auto& operation:accounting["operations"]) if(operation["operation_id"]==binary_service["prepared_id"]) {
  found_binary=true; assert(operation["actual"]["requests"]["value"]==1); assert(operation["actual"]["response_bytes"]["value"]==binary_response.size());
  assert(operation["actual"]["output_tokens"]["value"].is_null()&&operation["actual"]["cost_usd"]["value"].is_null());
 }
 assert(found_binary&&http->requests().size()==5);
 std::cout<<"native exact-byte dispatch, actual graph edges, receipt binding and duplicate protection passed\n";
}
`;
if (nativeEnabled) {
const temporary = await mkdtemp(join(tmpdir(), "loom-analysis-native-"));
const evidenceRoot = process.env.LOOM_ANALYSIS_EVIDENCE ?? resolve(loom, "../../analysis-native-evidence");
await mkdir(evidenceRoot, { recursive: true });
const evidence = await mkdtemp(join(evidenceRoot, "attempt-"));
let phase = "setup";
try {
  const source = join(temporary, "analysis.cc"), binary = join(temporary, "analysis");
  await writeFile(source, native);
  await writeFile(join(evidence, "analysis.cc"), native);
  const input = join(web, "src/profiles/presets/graph-chat.json");
  await copyFile(input, join(evidence, "graph-chat.json"));
  for (const header of ["analysis-ui-routes.h", "method-ui-routes.h", "native-ui-common.h"])
    await copyFile(join(loom, "server/src", header), join(evidence, header));
  const core = process.env.LOOM_ANALYSIS_CORE ?? join(loom, "build/dev/libloom_core.a");
  const miniz = process.env.LOOM_ANALYSIS_MINIZ ?? join(loom, "build/dev/libloom_miniz.a");
  const sqlite = process.env.LOOM_ANALYSIS_SQLITE ?? join(loom, "build/dev/libloom_sqlite3_amalgamation.a");
  await readFile(core);
  const compilerArgs = ["-std=c++20", "-pthread", "-I" + join(loom, "include"), "-I" + join(loom, "src"), "-I" + join(loom, "server/src"),
    "-I" + join(loom, "third_party/cpp-httplib"), "-I" + join(loom, "third_party/nlohmann"), source, core, miniz, sqlite, "-ldl", "-lssl", "-lcrypto", "-o", binary];
  await writeFile(join(evidence, "commands.json"), JSON.stringify({ cwd: web, compiler: { command: "c++", args: compilerArgs }, runtime: { command: binary, args: [input] } }, null, 2));
  phase = "compiler";
  const compiled = await executeFile("c++", compilerArgs, { timeout: 180000, maxBuffer: 4 * 1024 * 1024 });
  await writeFile(join(evidence, "compiler.stdout.log"), compiled.stdout); await writeFile(join(evidence, "compiler.stderr.log"), compiled.stderr);
  // Pin the generated native executable and its consumed inputs before it runs.
  const receiptFiles = [source, binary, input, core, miniz, sqlite,
    ...["analysis-ui-routes.h", "method-ui-routes.h", "native-ui-common.h"].map(name => join(loom, "server/src", name))];
  const sha256 = Object.fromEntries(await Promise.all(receiptFiles.map(async file =>
    [file, createHash("sha256").update(await readFile(file)).digest("hex")])));
  await writeFile(join(evidence, "runtime-receipt.json"), JSON.stringify({ captured_at: new Date().toISOString(), sha256 }, null, 2));
  phase = "runtime";
  const outcome = await executeFile(binary, [input], { timeout: 30000, maxBuffer: 4 * 1024 * 1024 });
  await writeFile(join(evidence, "runtime.stdout.log"), outcome.stdout); await writeFile(join(evidence, "runtime.stderr.log"), outcome.stderr);
  assert.match(outcome.stdout, /native exact-byte dispatch/);
  await writeFile(join(evidence, "result.json"), JSON.stringify({ status: "passed", phase, provider_calls: "scripted_only" }, null, 2));
  pass("native scripted transport preserves exact bytes, schema/provenance, real lineage and receipt-bound single attempts");
} catch (error) {
  await writeFile(join(evidence, `${phase}.stdout.log`), error.stdout ?? ""); await writeFile(join(evidence, `${phase}.stderr.log`), error.stderr ?? "");
  await writeFile(join(evidence, "result.json"), JSON.stringify({ status: "failed", phase, code: error.code ?? null, signal: error.signal ?? null, message: error.message, stack: error.stack }, null, 2));
  throw error;
} finally { await rm(temporary, { recursive: true, force: true }); }
}
completion.complete();
console.log(`[analysis-ui] ${groups.length}/${expectedGroups} groups passed; provider calls were scripted or mocked`);
