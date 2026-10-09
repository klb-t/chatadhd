// Audit transport only. All semantics below call the checkout's native engine.
#include <iostream>
#include <sstream>
#include "loom/kb.h"
#include "loom/db.h"
#include "loom/knowledge_store.h"
#include "loom/onboarding.h"
#include "loom/onboarding_layers.h"
#include "loom/onboarding_store.h"
#include "loom/graph_packet_store.h"
#include "loom/util/json.h"
using namespace loom;
Json envelope(const Result<Json>& r) {
  if (r) return Json{{"ok",true},{"value",r.value()}};
  return Json{{"ok",false},{"error",{{"code",errc_name(r.error().code)},{"message",r.error().message}}}};
}
Result<Json> run(const Json& r) {
  const auto op=r.at("op").get<std::string>();
  if(op=="pack_validate") {
    std::vector<kb::PackIssue> issues;
    const bool valid=kb::validate_document("lexicons/stemming.json",r.at("schema").get<std::string>(),r.at("document"),Json(),issues);
    Json rows=Json::array(); for(const auto& issue:issues) rows.push_back(issue.to_json());
    return Json{{"valid",valid},{"issues",rows}};
  }
  if(op=="pack_load") {
    auto p=r.value("overlay",false) ? kb::Pack::load_with_overlay(r.at("directory").get<std::string>()) : kb::Pack::load_dir(r.at("directory").get<std::string>());
    if(!p) return p.error();
    Json out{{"manifest",p.value()->manifest()}};
    auto norm=kb::Normalizer::create(*p.value());
    if(!norm) return norm.error();
    out["samples"]=Json::array();
    for(const auto& sample:r.value("samples",Json::array())) {auto s=sample.get<std::string>(); out["samples"].push_back(Json{{"text",s},{"key",norm->phrase_key(s)},{"language",kb::to_string(norm->guess_lang(s))}});}
    return out;
  }
  if(op=="layers") {
    auto layers=onboarding::DefaultLayers::create(r.at("pack"),r.value("state",Json::object()));
    if(!layers) return layers.error();
    Json state=layers->snapshot();
    if(r.contains("action")) {auto next=layers->dispatch(r.at("action"));if(!next)return next.error();state=next.value();}
    auto current=onboarding::DefaultLayers::create(r.at("pack"),state);if(!current)return current.error();
    Json out{{"state",current->snapshot()},{"resolutions",Json::object()}};
    for(const auto& key:r.value("keys",Json::array())) {auto resolved=current->resolve(key.get<std::string>()); if(!resolved)return resolved.error();out["resolutions"][key.get<std::string>()]=resolved.value();}
    return out;
  }
  DbOptions options;options.enable_fts=false;
  auto opened=Database::open(r.at("database").get<std::string>(),options);if(!opened)return opened.error();
  auto db=std::move(opened).value();onboarding::OnboardingStore store(*db);
  const std::string user=r.value("user","synthetic/native-audit");
  if(op=="open")return store.open(user,r.value("legacy",Json::object()));
  if(op=="read")return store.read(user);
  if(op=="apply")return store.apply(user,r.at("revision").get<std::int64_t>(),r.at("action"));
  if(op=="update_pack")return store.update_pack(user,r.at("revision").get<std::int64_t>(),r.at("pack"),r.at("scenario"));
  if(op=="request")return store.model_request(user,r.value("provider","synthetic/offline"));
  if(op=="policy")return store.policy_decision(user,r.at("request"));
  if(op=="packet") {kb::GraphPacketStore packets(*db);return packets.execute(r.at("request"));}
  if(op=="graph") {
    auto state=store.read(user);if(!state)return state.error();
    kb::KnowledgeStore knowledge(*db);const auto id=state->at("graph_run_id").get<std::string>();
    auto es=knowledge.query_entities(id,{});if(!es)return es.error();
    auto cs=knowledge.query_claims(id,{});if(!cs)return cs.error();
    Json out{{"run",id},{"entities",Json::array()},{"claims",Json::array()}};
    for(const auto& e:*es)out["entities"].push_back(e.to_json());for(const auto& c:*cs)out["claims"].push_back(c.to_json());return out;
  }
  return Error(Errc::InvalidArgument,"audit driver: unknown operation");
}
int main() {
  std::ostringstream input;input<<std::cin.rdbuf();
  try {auto parsed=json::parse(input.str());if(!parsed){std::cout<<json::dump(envelope(parsed))<<'\n';return 2;}
    std::cout<<json::dump(envelope(run(parsed.value())))<<'\n';return 0;
  }catch(const std::exception& e){std::cout<<json::dump(Json{{"ok",false},{"exception",e.what()}})<<'\n';return 2;}
}
