#include <iostream>
#include "loom/onboarding_store.h"
#include "loom/onboarding.h"
using namespace loom; using namespace loom::onboarding;
template<class T> T take(Result<T> r) { if(!r){std::cerr<<r.error().to_string()<<'\n';std::exit(2);}return std::move(r).value(); }
Json event(std::string op) {return Json{{"op",op},{"id",op+"/test"},{"time","2000-01-01T00:00:00Z"}};}
int main(){
 auto db=take(Database::open(":memory:")); OnboardingStore store(*db); auto state=take(store.open("synthetic-user"));
 auto initial=state;
 auto invalid=event("override"); invalid["target"]="layers"; invalid["key"]="onboarding.settings";invalid["value"]={{"preference_mode","invented"}};
 auto rejected=store.apply("synthetic-user",state["revision"].get<std::int64_t>(),invalid);
 std::cout<<"invalid effective settings rejected="<<!rejected<<" revision unchanged="<<(take(store.read("synthetic-user"))["revision"]==state["revision"])<<'\n';
 if(rejected)return 3;
 auto work=state["profile"]["privacy"]["rules"][0]; work["id"]="work";work["category"]="work";
 auto a=event("privacy"); a["rule"]=work; state=take(store.apply("synthetic-user",state["revision"].get<std::int64_t>(),a));
 a=event("answer");a["field"]="work.projects";a["value"]="historical-secret";state=take(store.apply("synthetic-user",state["revision"].get<std::int64_t>(),a));
 a=event("review");a["candidate"]="answer/test";a["decision"]="confirmed";state=take(store.apply("synthetic-user",state["revision"].get<std::int64_t>(),a));
 auto wildcard=state["profile"]["privacy"]["rules"][0];wildcard["retention"]["history"]="metadata";
 a=event("override");a["target"]="layers";a["key"]="onboarding.privacy";a["value"]={{"rules",Json::array({wildcard})}};
 state=take(store.apply("synthetic-user",state["revision"].get<std::int64_t>(),a));
 std::cout<<"old exact rule removed="<<(state["profile"]["privacy"]["rules"].size()==1)<<" historical raw secret remains="<<(json::dump(state["profile"]["history"]).find("historical-secret")!=std::string::npos)<<" reviewed candidate value remains="<<state["profile"]["candidates"]["answer/test"].contains("value")<<'\n';
 a=event("disable");a["target"]="layers";a["key"]="onboarding.privacy";state=take(store.apply("synthetic-user",state["revision"].get<std::int64_t>(),a));
 std::cout<<"effective privacy policy active after disable="<<take(store.policy_decision("synthetic-user",Json{{"op","store"},{"category","work"},{"field","work.projects"},{"provenance","user_stated"}}))["allowed"]<<'\n';
 a=event("reenable");a["target"]="layers";a["key"]="onboarding.privacy";state=take(store.apply("synthetic-user",state["revision"].get<std::int64_t>(),a));
 auto pack=state["pack"];pack["revision"]=2;auto second=state["scenario_definition"]["graph_method"];second.erase("default_key");second.erase("prompt_default_key");second["revision"]=2;second["parameters"]={{"synthetic_setting",1}};second["recipe"].erase("parameters");second["recipe"].erase("prompt_sha256");
 auto third=second;third["id"]="synthetic.other-method";pack["methods"]=Json::array({second,third});
 state=take(store.update_pack("synthetic-user",state["revision"].get<std::int64_t>(),pack,state["scenario_definition"]));
 auto request=take(store.model_request("synthetic-user","test"));
 std::cout<<"multiple method versions preserve intended interview="<<(request["method_profile"]["bindings"].size()==3)<<'\n';
 auto legacy=take(ProfileSession::create(take(builtin_scenario()),take(builtin_pack())["runtime_definition"]["defaults"])).snapshot();
 auto& style=legacy["fields"]["communication.style"];style["status"]="known";style["value"]="legacy user style";style["provenance"]="form";style["review"]="confirmed";style["time"]="2000-01-01T00:00:00Z";
 auto migrated=take(store.open("legacy-user",legacy));bool preserved=false;for(const auto& entry:migrated["effectiveDefaults"])if(entry["key"]=="preference.style")preserved=entry["value"]=="legacy user style";
 std::cout<<"legacy style effective overlay preserved="<<preserved<<'\n';
}
