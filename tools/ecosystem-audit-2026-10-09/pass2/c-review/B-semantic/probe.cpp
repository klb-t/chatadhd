// Independent runtime consumer probes; all policy values below are test fixtures.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/db.h"
#include "loom/graph_engine.h"
#include "loom/semantic_analyzer.h"
#include "loom/semantic_worker.h"
#include "loom/semantic_llm.h"
#include "loom/net/http.h"
#include "loom/config.h"
using namespace loom;
namespace fs = std::filesystem;
Json checks=Json::array();
template<class T> T value(Result<T> v) { if(!v)throw std::runtime_error(v.error().to_string());return std::move(v).value(); }
void okay(Status s) {if(!s)throw std::runtime_error(s.error().to_string());}
void write(const fs::path& p,const std::string& s) {fs::create_directories(p.parent_path());std::ofstream f(p);f<<s; if(!f)throw std::runtime_error("fixture_write_failed");}
void check(const std::string& id,bool pass,const Json& evidence){checks.push_back(Json{{"id",id},{"acceptance",pass?"PASS":"FAIL"},{"evidence",evidence}});}
std::string overlay(const fs::path& d,const std::string& marker){
 Json rule{{"entity_type","concept"},{"pattern",marker},{"flags",Json::array()},{"confidence",1.0}};
 Json j{{"schema","loom.runtime_profile_overlay/1"},{"domain","semantic_analyzer"},{"overrides",{{"rules",{{"entity_patterns",Json::array({rule})}}}}}};
 write(d/"profiles/semantic_analyzer.pack",j.dump());return value(RuntimeProfile::load("semantic_analyzer",d)).hash();
}
std::shared_ptr<net::ScriptedTransport> make_transport(){auto t=std::make_shared<net::ScriptedTransport>();t->set_fallback(net::ScriptedTransport::Reply::fail(Errc::Timeout,"synthetic intercepted timeout"));return t;}
RuntimeOptions options(const fs::path& d,std::shared_ptr<net::ScriptedTransport> t){RuntimeOptions o;o.data_dir=d.string();o.start_workers=false;o.http=t;o.log_level=log::Level::Error;return o;}
std::string pending(Runtime& r,const std::string& conv,const std::string& marker){NewMessage m;m.conv_id=conv;m.role="user";m.text="Offline audit fixture contains "+marker+" only.";return value(r.db().create_msg(m));}
Json process(Runtime& r,const std::string& conv,const std::string& marker,bool live){
 auto id=pending(r,conv,marker);bool success=false;
 if(live){auto m=value(r.db().get_msg(id));success=bool(r.graph().on_message_checked(Json{{"id",id},{"conv_id",conv},{"text",m->text}}));}
 else success=bool(r.worker().drain_once());
 auto msg=*value(r.db().get_msg(id));auto node=value(r.db().find_node(marker,"concept"));
 bool linked=false;if(node){for(const auto& link:value(r.db().get_links(id,"mentions")))if(link.dst==node->id)linked=true;}
 return Json{{"operation_success",success},{"semantic_status",msg.semantic_status},{"entity_count",msg.metadata.value("entity_count",-1)},{"metadata_hash",msg.metadata.value("analyzer_profile_hash",Json())},{"live_metadata_hash",msg.metadata.value("semantic",Json::object()).value("analyzer_profile_hash",Json())},{"marker_linked",linked}};
}
void configure(Runtime& r,bool llm){r.config().set("semantic_analysis",llm);if(llm){r.config().set("semantic_model","audit/offline-model");r.secrets().set("api_key","audit-synthetic-credential");}}
void profiles(const fs::path& d,bool live,bool llm){
 const std::string mode=std::string(live?"live":"worker")+(llm?"-fallback":"-regex");
 auto initial=overlay(d,"AUDIT_STARTMARK");auto t=make_transport();auto rt=value(Runtime::open(options(d,t)));configure(*rt,llm);
 if(live&&!llm)check("CH003-startup-profile",rt->analyzer().profile_hash()==initial,Json{{"expected_hash",initial},{"constructor_hash",rt->analyzer().profile_hash()}});
 auto conv=value(rt->db().create_conv("Offline synthetic audit"));
 for(const auto& marker:{"AUDIT_STARTMARK","AUDIT_CHANGEDMARK"}){auto expected=overlay(d,marker);auto obs=process(*rt,conv.id,marker,live);obs["expected_hash"]=expected;
 bool fingerprint=obs["metadata_hash"]==expected||(live&&obs["live_metadata_hash"]==expected);
 bool ok=obs["operation_success"]==true&&obs["semantic_status"]=="done"&&obs["marker_linked"]==true&&fingerprint;
 check("CH003-two-profiles-"+mode+"-"+marker,ok,obs);}
 check("CH003-transport-"+mode,t->requests().size()==(llm?2u:0u),Json{{"intercepted_calls",t->requests().size()},{"external_calls",0}});
}
void invalid(const fs::path& d){
 overlay(d,"AUDIT_INVALIDMARK");auto t=make_transport();auto opts=options(d,t);auto rt=value(Runtime::open(opts));configure(*rt,true);auto conv=value(rt->db().create_conv("Offline corrupt pack"));
 auto id=pending(*rt,conv.id,"AUDIT_INVALIDMARK");write(d/"profiles/semantic_analyzer.pack","{ intentionally invalid fixture");
 auto worker=rt->worker().drain_once();auto msg=*value(rt->db().get_msg(id));auto live=rt->graph().on_message_checked(Json{{"id",id},{"conv_id",conv.id},{"text",msg.text}});
 check("CH003-invalid-preserves-pending",!worker&&!live&&msg.semantic_status=="pending"&&t->requests().empty(),Json{{"worker_error",!worker},{"live_error",!live},{"semantic_status",msg.semantic_status},{"intercepted_calls",t->requests().size()}});
 rt->shutdown();rt.reset();auto reopen=Runtime::open(opts);bool rejected=!reopen;if(reopen){reopen.value()->shutdown();reopen.value().reset();}
 check("CH003-invalid-startup",rejected,Json{{"explicit_startup_error",rejected}});
 auto expected=overlay(d,"AUDIT_INVALIDMARK");rt=value(Runtime::open(opts));auto drained=rt->worker().drain_once();msg=*value(rt->db().get_msg(id));
 check("CH003-valid-recovery",drained&&msg.semantic_status=="done"&&msg.metadata.value("analyzer_profile_hash",Json())==expected,Json{{"drain_success",bool(drained)},{"semantic_status",msg.semantic_status},{"expected_hash",expected},{"actual_hash",msg.metadata.value("analyzer_profile_hash",Json())}});
}
void returned_builtin(const fs::path& d,bool live,bool llm,bool empty){
 std::string mode=std::string(live?"live":"worker")+(llm?"-fallback":"-regex")+(empty?"-empty-overlay":"-removed-overlay");
 auto old=overlay(d,"AUDIT_STARTMARK");auto t=make_transport();auto opts=options(d,t);auto rt=value(Runtime::open(opts));configure(*rt,llm);auto conv=value(rt->db().create_conv("Offline return to builtin"));
 if(empty)write(d/"profiles/semantic_analyzer.pack",Json{{"schema","loom.runtime_profile_overlay/1"},{"domain","semantic_analyzer"},{"overrides",Json::object()}}.dump());else fs::remove(d/"profiles/semantic_analyzer.pack");
 auto current=value(RuntimeProfile::load("semantic_analyzer",d));auto fresh=value(SemanticAnalyzer::create_from_data_dir(d));
 auto reference=fresh->extract_entities("Offline audit fixture contains AUDIT_STARTMARK only.");
 auto before=process(*rt,conv.id,"AUDIT_STARTMARK",live);before["resolved_is_builtin"]=current.is_builtin();before["resolved_hash"]=current.hash();before["startup_hash"]=old;before["fresh_analyzer_entities"]=reference.size();
 bool ok=before["operation_success"]==true&&before["marker_linked"]==false&&before["entity_count"]==0&&reference.empty();
 check("CH003-reset-"+mode,ok,before);
 rt->shutdown();rt.reset();rt=value(Runtime::open(opts));configure(*rt,llm);auto reopened=process(*rt,conv.id,"AUDIT_STARTMARK",live);
 // Previously materialized nodes may remain but the NEW message must not link them.
 check("CH003-reset-restart-"+mode,reopened["operation_success"]==true&&reopened["marker_linked"]==false&&reopened["entity_count"]==0,reopened);
}
int main(int argc,char** argv){if(argc!=2)return 2;fs::path base=argv[1];try{
 for(bool live:{false,true})for(bool llm:{false,true})profiles(base/(std::string(live?"live":"worker")+(llm?"-llm":"-regex")),live,llm);
 invalid(base/"invalid");
 for(bool live:{false,true})for(bool llm:{false,true})for(bool empty:{false,true})returned_builtin(base/(std::string(live?"live":"worker")+(llm?"-llm":"-regex")+(empty?"-empty":"-removed")),live,llm,empty);
 std::cout<<Json{{"cases",checks},{"external_calls",0},{"transport","actual ScriptedTransport injected into Runtime"}}.dump()<<"\n";return 0;
}catch(const std::exception& e){std::cout<<Json{{"cases",checks},{"setup_error",e.what()}}.dump()<<"\n";return 2;}}
