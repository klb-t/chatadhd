// Audit-only transport. Parsing/resolution/import stays in the real engine.
#include <iostream>
#include "loom/runtime.h"
#include "loom/catalog.h"
#include "loom/knowledge.h"
#include "loom/knowledge_store.h"
#include "loom/extract.h"
#include "loom/db.h"
#include "loom/importer.h"
#include "loom/provenance.h"
#include "loom/runtime_profile.h"
#include "loom/net/http.h"
using namespace loom;
template<class T> T take(Result<T> r) { if (!r) throw std::runtime_error(r.error().to_string()); return std::move(*r); }
template<class T> Json result(Result<T> r) {if(!r)return Json{{"ok",false},{"code",errc_name(r.error().code)},{"error",r.error().message}};return Json{{"ok",true},{"value",*r}};}
int main(){
 try{
  Json req;std::cin>>req;const auto op=req.at("op").get<std::string>();
  if(op=="profile"){
   auto p=RuntimeProfile::load("net",req.at("data_dir").get<std::string>());
   if(!p){std::cout<<Json{{"ok",false},{"code",errc_name(p.error().code)},{"error",p.error().message}};return 0;}
   auto request=take(net::HttpRequest::from_json_with_profile(Json{{"method","GET"},{"url","https://synthetic.invalid/offline"}},*p));
   auto policy=take(net::HttpTransportPolicy::for_request(request,*p,[](std::string_view)->std::optional<std::string>{return std::nullopt;}));
   Json value{{"inspection",p->inspection()},{"request",request.to_json()},{"policy",Json{{"timeout_ms",policy.timeout_ms},{"follow_redirects",policy.follow_redirects},{"profile_hash",policy.profile_hash}}}};
   std::cout<<Json{{"ok",true},{"value",value}};return 0;
  }
  auto http=std::make_shared<net::ScriptedTransport>();http->set_fallback(net::ScriptedTransport::Reply::fail(Errc::Unavailable,"audit blocks every outbound request"));
  RuntimeOptions ro;ro.data_dir=req.at("data_dir").get<std::string>();ro.start_workers=false;ro.http=http;
  auto rt=take(Runtime::open(ro));catalog::Catalog cat(*rt,take(rt->knowledge().pack()));Json out;
  if(op=="scan")out=result(cat.scan(take(catalog::ScanConfig::from_json(req.at("config")))));
  else if(op=="query") {Json rows=Json::array();catalog::UnitQuery q;q.limit=10000;q.sort="id";for(const auto& row:take(cat.query(q)))rows.push_back(row.to_json());out={{"ok",true},{"value",rows}};}
  else if(op=="read")out=result(cat.read_unit(req.at("id").get<std::string>()));
  else if(op=="preview")out=result(cat.preview(req.at("id").get<std::string>()));
  else if(op=="extract") {
   auto pack=take(rt->knowledge().pack());kb::Normalizer normalizer(*pack);auto& store=rt->knowledge().store();
   knowledge::KnowledgeConfig config;config.llm="off";config.priors=false;
   auto run=take(store.begin_run(pack->hash(),Json{{"audit_resource_units",req.at("units")}}));
   knowledge::StageContext ctx{*rt,store,pack,normalizer,config,run.id,"extract"};ctx.input=Json{{"units",req.at("units")}};
   auto extracted=extract::run_stage(ctx);out=result(std::move(extracted));Json observations=Json::array();
   for(const auto& id:req.at("units"))for(const auto& o:take(store.observations_of_unit(run.id,id.get<std::string>())))observations.push_back(o.to_json());
   out["run"]=run.id;out["persisted_observations"]=observations;
  }
  else if(op=="catalog_import")out=result(cat.import_selected(take(catalog::ImportOptions::from_json(req.at("options")))));
  else if(op=="direct_import") {auto r=rt->importer().import_file(req.at("path").get<std::string>());out=r?Json{{"ok",true},{"value",r->to_json()}}:Json{{"ok",false},{"code",errc_name(r.error().code)},{"error",r.error().message}};}
  else if(op=="state"){
   Json convs=Json::array();for(const auto& c:take(rt->db().list_convs(10000))){Json messages=Json::array();for(const auto& m:take(rt->db().get_msgs(c.id,true)))messages.push_back(m.to_json());convs.push_back(Json{{"conversation",c.to_json()},{"messages",messages}});}
   Json sources=Json::array();for(const auto& s:take(rt->provenance().list_sources(10000)))sources.push_back(s.to_json());out={{"ok",true},{"value",Json{{"conversations",convs},{"sources",sources},{"catalog",take(cat.status())}}}};
  }else throw std::runtime_error("unsupported audit operation");
  out["blocked_transport_attempts"]=http->requests().size();rt->shutdown();std::cout<<out;
 }catch(const std::exception& e){std::cout<<Json{{"ok",false},{"audit_exception",e.what()}};return 2;}
}
