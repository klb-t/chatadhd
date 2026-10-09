#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include "loom/db.h"
#include "loom/knowledge_store.h"
#include "loom/onboarding.h"
#include "loom/onboarding_layers.h"
#include "loom/onboarding_store.h"
using namespace loom;
using namespace loom::onboarding;
template<class T> T take(Result<T> r) { if (!r) throw std::runtime_error(r.error().message); return std::move(r).value(); }
Json rows=Json::array();
void check(const char* id,const char* phase,bool pass,const Json& evidence=Json::object()) {
 rows.push_back({{"id",id},{"phase",phase},{"status",pass?"PASS":"FAIL"},{"evidence",evidence}});
}
Json apply(OnboardingStore& store,const std::string& user,const Json& state,const Json& action) {
 return take(store.apply(user,state.at("revision").get<std::int64_t>(),action));
}
Json catalog(const Json& s) {return s.at("presentation").at("value");}
Json override_catalog(const Json& value) {return {{"target","layers"},{"op","override"},{"key","presentation.onboarding"},{"value",value}};}
Json settings(const char* mode="automatic") {return {{"target","layers"},{"op","override"},{"key","onboarding.settings"},{"value",{{"preference_mode",mode}}}};}
Json project(const Json& state,const std::string& user) {return take(project_graph(state.at("pack"),state.at("scenario_definition"),state.at("profile"),state.at("layers"),user));}
Json method_without_labels(Json g) {g=g.at("method_profile");for(auto& e:g["entities"])e.erase("label");return g;}
Json persisted(Database& db,const Json& state) {
 kb::KnowledgeStore store(db);Json result=Json::array();
 for(const auto& e:take(store.query_entities(state.at("graph_run_id").get<std::string>(),{})))result.push_back(e.to_json());
 return result;
}
const std::vector<std::string> keys={"prompt","parameters","recipe","preset","version","builtin","user","types"};
Json marker_catalog(Json c) {
 for(auto& [locale,messages]:c["locales"].items())for(const auto& k:keys)
  messages["graph."+k]="AUDIT_GRAPH_"+k+((k=="builtin"||k=="user"||k=="types")?"":"/{{id}}");
 return c;
}
std::map<std::string,int> counts(const Json& entities) {
 std::map<std::string,int> count;for(const auto& k:keys)count[k]=0;
 for(const auto& e:entities)for(const auto& k:keys)if(e.at("label").get<std::string>().starts_with("AUDIT_GRAPH_"+k))++count[k];
 return count;
}
bool all_seen(const std::map<std::string,int>& counts) {for(const auto& [k,n]:counts)if(k!="preset"&&!n)return false;return true;}
int main(int argc,char** argv) {
 try {
  if(argc!=3)throw std::runtime_error("usage: consumer seed|verify sqlite-path");
  const auto path=std::filesystem::path(argv[2]);std::filesystem::create_directories(path.parent_path());
  DbOptions opts;opts.enable_fts=false;auto db=take(Database::open(path,opts));OnboardingStore store(*db);
  if(std::string(argv[1])=="seed") {
   auto plain=take(store.open("old/plain"));plain=apply(store,"old/plain",plain,settings());auto custom=take(store.open("old/custom"));auto c=catalog(custom);c["locales"]["en"]["layer.user"]="Synthetic legacy customization";
   custom=apply(store,"old/custom",custom,override_catalog(c));auto excluded=take(store.open("old/excluded"));
   excluded=apply(store,"old/excluded",excluded,{{"target","layers"},{"op","exclude"},{"key","presentation.onboarding"}});
   check("B2.SEED.legacy-native-store","fixture",plain["pack"]["revision"]==3&&plain["profile"]["settings"]["preference_mode"]=="automatic"&&custom["pack"]["revision"]==3&&excluded["presentation"]["status"]=="excluded",{{"pack_revision",plain["pack"]["revision"]},{"settings_action_succeeded_before_upgrade",true}});
  } else {
   auto pack=take(builtin_pack());auto scenario=take(builtin_scenario());auto state=take(store.open("new/current"));const auto base=project(state,"new/current");
   state=apply(store,"new/current",state,override_catalog(marker_catalog(catalog(state))));const auto custom=project(state,"new/current");
   auto seen=counts(custom["entities"]);check("B2-LABELS-001.seven-instantiated-consumers","acceptance",pack["revision"]==4&&all_seen(seen),seen);
   auto preset_scenario=state["scenario_definition"];preset_scenario["graph_method"]["preset"]={{"audit_fixture","explicit-preset"}};
   auto preset_graph=take(project_graph(state["pack"],preset_scenario,state["profile"],state["layers"],"new/current"));
   check("B2-LABELS-001b.preset-consumer","acceptance",counts(preset_graph["entities"])["preset"]==1,{{"scope","explicit synthetic preset passed to actual project_graph; builtin scenario contains no preset"}});
   check("B2-LABELS-002.method-identity","acceptance",method_without_labels(base)==method_without_labels(custom));
   auto custom_persisted=persisted(*db,state);check("B2-LABELS-003.native-write-read","acceptance",all_seen(counts(custom_persisted)));
   const auto before=state;auto broken=catalog(state);broken["locales"]["en"].erase("graph.parameters");
   auto rejected=store.apply("new/current",state["revision"],override_catalog(broken));
   check("B2-LABELS-004.missing-key-atomic","acceptance",!rejected&&take(store.read("new/current"))==before,{{"error",rejected?"unexpected-success":rejected.error().message}});
   broken=catalog(state);broken["locales"]["en"]["graph.prompt"]="{{missing_parameter}}";
   auto syntax=store.apply("new/current",state["revision"],override_catalog(broken));
   check("B2-LABELS-005.invalid-template-atomic","acceptance",!syntax&&take(store.read("new/current"))==before);
   auto old=take(store.open("old/plain"));const auto old_before=old;
   auto old_write=store.apply("old/plain",old["revision"],settings("ask"));
   check("B2-MIGRATION-001.old-default-write-blocked","reproduction",!old_write&&take(store.read("old/plain"))==old_before,{{"retained_pack_revision",old["pack"]["revision"]},{"error",old_write?"unexpected-success":old_write.error().message}});
   const std::string diagnostic=old_write?"":old_write.error().message;
   const bool explicit_migration=diagnostic.find("pack")!=std::string::npos&&(diagnostic.find("upgrad")!=std::string::npos||diagnostic.find("migrat")!=std::string::npos||diagnostic.find("update_pack")!=std::string::npos);
   check("B2-MIGRATION-002.old-default-compatibility-or-guidance","acceptance",bool(old_write)||explicit_migration,{{"criterion","A previously valid settings action works, or the existing native diagnostic identifies a required pack migration/upgrade; automatic migration is not required."},{"native_diagnostic",diagnostic},{"oracle_boundary","This probe uses the existing English native diagnostic contract. A future structured/localized migration contract needs its own adapter, not the same words."}});
   if(old_write)old=old_write.value();
   old=take(store.update_pack("old/plain",old["revision"],pack,scenario));old=apply(store,"old/plain",old,settings());
   check("B2-MIGRATION-003.explicit-pack-upgrade-repairs","acceptance",old["profile"]["settings"]["preference_mode"]=="automatic"&&old["pack"]["revision"]==4);
   auto legacy=take(store.open("old/custom"));const auto legacy_before=legacy;
   auto failed_upgrade=store.update_pack("old/custom",legacy["revision"],pack,scenario);
   check("B2-MIGRATION-004.legacy-overlay-fails-atomically","acceptance",!failed_upgrade&&take(store.read("old/custom"))==legacy_before,{{"error",failed_upgrade?"unexpected-success":failed_upgrade.error().message}});
   auto repaired=catalog(legacy);auto fresh=marker_catalog(catalog(state));for(auto& [locale,messages]:repaired["locales"].items())for(const auto& k:keys)messages["graph."+k]=fresh["locales"][locale]["graph."+k];
   legacy=apply(store,"old/custom",legacy,override_catalog(repaired));legacy=take(store.update_pack("old/custom",legacy["revision"],pack,scenario));
   check("B2-MIGRATION-005.explicit-overlay-repair","acceptance",catalog(legacy)["locales"]["en"]["layer.user"]=="Synthetic legacy customization"&&all_seen(counts(persisted(*db,legacy))));
   auto ex=take(store.open("old/excluded"));ex=apply(store,"old/excluded",ex,settings());ex=take(store.update_pack("old/excluded",ex["revision"],pack,scenario));
   std::vector<std::string> target_ids;for(const auto& e:custom_persisted)if(e["label"].get<std::string>().starts_with("AUDIT_GRAPH_"))target_ids.push_back(e["id"]);
   state=apply(store,"new/current",state,{{"target","layers"},{"op","exclude"},{"key","presentation.onboarding"}});
   auto next=pack;next["revision"]=5;for(auto& entry:next["entries"])if(entry["key"]=="presentation.onboarding")entry["revision"]=4;
   state=take(store.update_pack("new/current",state["revision"],next,scenario));
   db.reset();auto reopened=take(Database::open(path,opts));OnboardingStore again(*reopened);state=take(again.open("new/current"));
   int empty=0;for(const auto& e:persisted(*reopened,state))for(const auto& id:target_ids)if(e["id"]==id&&e["label"]=="")++empty;
   check("B2-LABELS-006.exclusion-restart-pack-upgrade","acceptance",state["presentation"]["status"]=="excluded"&&empty==static_cast<int>(target_ids.size())&&!target_ids.empty(),{{"target_entities",target_ids.size()},{"empty_labels",empty}});
   check("B2-MIGRATION-006.old-exclusion-survives-upgrade","acceptance",take(again.open("old/excluded"))["presentation"]["status"]=="excluded");
  }
  std::cout<<Json{{"cases",rows}}.dump(2)<<std::endl;return 0;
 }catch(const std::exception& e){std::cout<<Json{{"fixture_error",e.what()},{"cases",rows}}.dump(2)<<std::endl;return 2;}
}
