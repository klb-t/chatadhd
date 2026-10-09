// Independent audit: actual Runtime, database, store, chat, graph and policy.
// ScriptedTransport is the existing native HTTP seam. No live network stack.
#include <iostream>
#include "loom/runtime.h"
#include "loom/db.h"
#include "loom/chat_engine.h"
#include "loom/graph_engine.h"
#include "loom/memory_engine.h"
#include "loom/onboarding_store.h"
#include "loom/net/http.h"
#include "loom/usage_policy.h"
using namespace loom;
template<class T>T take(Result<T> r) {if(!r)throw std::runtime_error(r.error().to_string());return std::move(r).value();}
int main(int argc,char**argv) {
 try {
  if(argc!=2)throw std::runtime_error("usage: native_consumers DATA_DIRECTORY");
  auto http=std::make_shared<net::ScriptedTransport>();
  http->set_fallback(net::ScriptedTransport::Reply::json(200,Json{{"choices",Json::array({Json{{"message",{{"content","synthetic answer"}}}}})}}));
  RuntimeOptions options; options.data_dir=argv[1];options.http=http;options.start_workers=false;options.enable_fts=false;
  auto rt=take(Runtime::open(options));rt->graph().stop();rt->secrets().set("api_key","synthetic-offline-key");
  rt->config().set("auto_title",false);rt->config().set("semantic_analysis",false);rt->config().set("stream",false);
  onboarding::OnboardingStore store(rt->db());const std::string user="synthetic/audit-user";
  auto state=take(store.open(user));
  auto act=[&](Json action){action["id"]="audit/"+std::to_string(state["revision"].get<int>());action["time"]="2026-10-09T00:00:00Z";action["source_refs"]=Json::array({"audit/synthetic"});state=take(store.apply(user,state["revision"].get<std::int64_t>(),action));};
  const std::string candidate="audit/"+std::to_string(state["revision"].get<int>());
  act(Json{{"op","answer"},{"field","identity.description"},{"value","synthetic profile marker"},{"provenance","form"}});
  act(Json{{"op","review"},{"candidate",candidate},{"decision","confirmed"}});
  // The policy defaults deny external providers; an explicit empty provider set remains restrictive.
  Json privacy=state["profile"]["privacy"];privacy["rules"][0]["providers"]=Json::array();
  act(Json{{"target","layers"},{"op","override"},{"key","onboarding.privacy"},{"value",privacy}});
  auto scoped=take(store.model_request(user,"openrouter"));
  take(rt->memory().add_node("synthetic profile marker",std::nullopt,std::nullopt,Json{{"profile_field","identity.description"},{"user_id",user}}));
  auto cfg=usage_policy_defaults();cfg["initial_baselines"]["audit/ordinary-chat"]=Json{{"requests",0}};rt->config().set("loom_usage_policy",cfg);
  auto policy=take(UsagePolicy::open(rt->paths().root/"usage-policy.sqlite",cfg));
  auto decision=take(policy->preview(Json{{"operation_id","audit/chat-1"},{"baseline_key","audit/ordinary-chat"},{"resources",{{"requests",1}}}}));
  ChatOptions opts;opts.model="synthetic/model";opts.stream=false;opts.include_graph_memory=false;opts.include_history=false;
  auto sent=rt->chat().send("synthetic user question",opts);
  auto calls=http->requests();Json observations;
  observations["chat"]={{"send_ok",bool(sent)},{"transport_calls",calls.size()},{"scoped_calls_authorized",scoped.value("calls_authorized",false)},
    {"scoped_context",scoped.value("context",Json())},{"body_contains_profile_marker",!calls.empty()&&calls.back().body.find("synthetic profile marker")!=std::string::npos},
    {"policy_preview_status",decision["status"]},{"ledger",take(policy->inspect("audit/ordinary-chat"))}};
  if(!sent) observations["chat"]["error"]=sent.error().to_string();
  Json parameter_runs=Json::array();
  for(const auto& c:std::vector<std::pair<double,int>>{{0.2,123},{0.8,456}}){rt->config().set("temperature",c.first);rt->config().set("max_tokens",c.second);auto s=rt->chat().send("synthetic config probe",opts);auto req=http->requests();auto body=take(json::parse(req.back().body));parameter_runs.push_back(Json{{"ok",bool(s)},{"requested_temperature",c.first},{"actual_temperature",body.value("temperature",Json())},{"requested_max_tokens",c.second},{"actual_max_tokens",body.value("max_tokens",Json())}});}
  observations["config_consumption"]=parameter_runs;
  auto conv=take(rt->db().create_conv("synthetic graph"));NewMessage m;m.conv_id=conv.id;m.role="user";m.text="synthetic inference input";auto mid=take(rt->db().create_msg(m));
  Json analysis{{"source","llm"},{"entities",Json::array({Json{{"name","Audit Alpha"},{"relevance",0.9}},Json{{"name","Audit Beta"},{"relevance",0.9}}})},{"relations",Json::array({Json{{"subject","Audit Alpha"},{"object","Audit Beta"},{"predicate","synthetic_infers"}}})}};
  auto ingested=rt->graph().ingest_analysis_checked(mid,conv.id,analysis);
  auto links=take(rt->db().get_links(std::nullopt,"synthetic_infers"));Json stored=Json::array();for(const auto& l:links)stored.push_back(l.to_json());
  observations["graph"]={{"ingestion_ok",bool(ingested)},{"links",stored},{"explicit_admission_supplied",false}};
  const auto before_invalid=http->requests().size();bool invalid_rejected=false;
  try {rt->config().set("temperature","synthetic-invalid-number");auto saved=rt->config().save();if(!saved)throw std::runtime_error(saved.error().message);rt->config().reload();auto invalid=rt->chat().send("synthetic malformed configuration",opts);invalid_rejected=!invalid;}catch(const std::exception&){invalid_rejected=true;}
  const auto after_invalid=http->requests();Json invalid_temperature=nullptr;if(after_invalid.size()>before_invalid)invalid_temperature=take(json::parse(after_invalid.back().body)).value("temperature",Json());
  observations["invalid_config"]={{"rejected",invalid_rejected},{"transport_calls",after_invalid.size()-before_invalid},{"actual_temperature",invalid_temperature},{"persisted_then_reloaded",true}};
  std::cout<<observations.dump()<<'\n'; return 0;
 }catch(const std::exception&e){std::cout<<Json{{"fixture_error",e.what()}}.dump()<<'\n';return 2;}
}
