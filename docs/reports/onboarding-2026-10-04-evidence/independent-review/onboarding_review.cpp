#include <iostream>
#include <fstream>
#include "loom/onboarding.h"
#include "loom/onboarding_layers.h"
using namespace loom;
using namespace loom::onboarding;
template<class T> T take(Result<T> r) { if (!r) { std::cerr << r.error().to_string() << '\n'; std::exit(2); } return std::move(r).value(); }
int main() {
  std::ifstream in("/workspace/scratch/4db3ee41b13b/chatadhd/loom/data/onboarding/scenario.pack");
  Json scenario; in >> scenario;
  Json rule{{"id","test"},{"category","*"},{"store",true},{"infer",true},{"explicit_only",false},{"providers",Json::array({"test"})},{"max_detail",nullptr},{"max_sensitivity",nullptr},{"retention",{{"history","full"},{"max_events",nullptr}}}};
  Json defaults{{"privacy",{{"rules",Json::array({rule})}}},{"settings",{{"preference_mode","ask"}}}};
  auto session=take(ProfileSession::create(scenario,defaults));
  Json reply{{"section","identity"},{"summary","secret-in-summary"},{"questions",Json::array()},{"candidates",Json::array({Json{{"id","candidate"},{"field","work.projects"},{"value","secret-value"},{"time","2000-01-01"},{"source_refs",Json::array()}}})}};
  auto request=take(session.model_request("test")); reply["provider"]="test"; reply["request_token"]=request["request_token"];
  take(session.ingest_model_reply(reply));
  take(session.dispatch(Json{{"op","review"},{"candidate","candidate"},{"decision","confirmed"},{"id","confirm"},{"time","2000-01-02"}}));
  take(session.dispatch(Json{{"op","delete"},{"field","work.projects"},{"purge_history",true},{"id","delete"},{"time","2000-01-03"}}));
  std::cout << "deleted secret-value retained=" << (json::dump(session.snapshot()).find("secret-value")!=std::string::npos) << '\n';
  std::cout << "deleted secret summary retained=" << (json::dump(session.snapshot()).find("secret-in-summary")!=std::string::npos) << '\n';
  take(session.dispatch(Json{{"op","status"},{"field","work.projects"},{"status","never"},{"id","never"},{"time","2000-01-04"}}));
  reply["candidates"]=Json::array();
  std::cout << "stale-summary accepted=" << bool(session.ingest_model_reply(reply)) << '\n';
  take(session.dispatch(Json{{"op","repeat"},{"section","privacy"},{"id","nav"},{"time","2000-01-05"}}));
  auto after=take(session.dispatch(Json{{"op","skip"},{"section","privacy"},{"id","skip"},{"time","2000-01-06"}}));
  std::cout << "skip privacy=" << after["session"]["status"] << " communication=" << after["session"]["sections"]["communication"]["status"] << '\n';
}
