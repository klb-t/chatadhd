// C4 final packet -> actual native store -> actual MethodRegistry.
// DTO normalization is diagnostic only; it is not claimed as a product repair.
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "loom/runtime.h"
#include "loom/graph_packet_store.h"
#include "loom/net/http.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "context/method_registry.h"
#include "context/method_channels.h"
using namespace loom;
using namespace loom::context;
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().to_string());return std::move(r).value();}
int main(int argc,char** argv){
 Json tests=Json::array();
 auto add=[&](const char* id,const char* category,bool pass,Json evidence){tests.push_back(Json{{"id",id},{"category",category},{"status",pass?"PASS":"FAIL"},{"evidence",evidence}});};
 try{
  if(argc!=2)throw std::runtime_error("fixture_path_required");
  std::ifstream input(argv[1]);Json payload;input>>payload;auto profile=payload["profile"];auto packet=payload["packet"];
  fsutil::TempDir dir;auto transport=std::make_shared<net::ScriptedTransport>();RuntimeOptions options;options.data_dir=dir.path().string();options.start_workers=false;options.http=transport;auto rt=take(Runtime::open(options));
  Json selection=Json::object(),expected=Json::object();for(const char* col:{"entities","claims","sources"}){
   selection[col]=Json::array();expected[col]=Json::object();for(const auto& row:packet[col]){auto id=std::string_view(col)=="sources"?row["observation"]["id"]:row["id"];selection[col].push_back(id);expected[col][id.get<std::string>()]=nullptr;}}
  kb::GraphPacketStore store(rt->db());auto accepted=store.execute(Json{{"operation","accept"},{"packet",packet},{"target","audit-c4-registry"},{"selection",selection},{"expected_rows",expected},{"explicitly_accepted",true}});
  add("C4-REGISTRY-01","integration",static_cast<bool>(accepted),Json{{"actual_store_acceptance",static_cast<bool>(accepted)},{"method_execution_claimed",false}});
  MethodRegistry registry(rt->db());auto loaded=registry.load(profile);
  add("C4-REGISTRY-02-A","bug_reproduction",!loaded,Json{{"finding","A3-DISC-001"},{"raw_packet_rejected",!loaded},{"error_code",loaded?"none":errc_name(loaded.error().code)},{"error_sha256",loaded?"":Sha256::hex(loaded.error().message)}});
  add("C4-REGISTRY-02-B","acceptance",static_cast<bool>(loaded),Json{{"finding","A3-DISC-001"},{"expected","same accepted final C4 DTOs available to existing MethodRegistry without caller repairing serialization"}});
  if(accepted){
   // The existing receipt-backed loader is an independent public input path.
   // Do not confuse a working store->registry path with raw-profile acceptance.
   auto referenced=profile;for(const char* col:{"entities","claims","sources"})referenced.erase(col);
   const auto receipt_id=(*accepted)["receipt"]["id"].get<std::string>();
   auto receipt_snapshot=registry.load(referenced,{receipt_id});
   bool unavailable=false;std::size_t leaves=0;
   if(receipt_snapshot){auto resolved=take(registry.resolve(*receipt_snapshot,Json::object(),Json{{"execution",method_channel_capabilities()},{"fusion",method_fusion_capabilities()}}));leaves=resolved["leaves"].size();unavailable=true;for(const auto& leaf:resolved["leaves"])unavailable&=!leaf["available"].get<bool>();}
   add("C4-REGISTRY-05","integration",static_cast<bool>(receipt_snapshot)&&unavailable&&leaves==1,Json{{"existing_receipt_backed_loader",true},{"registry_load",static_cast<bool>(receipt_snapshot)},{"declared_method_count",leaves},{"executor_unavailable",unavailable},{"caller_dto_rewrite",false},{"raw_profile_failure_closed",false}});
  }
  auto normalized=profile;
  for(auto& row:normalized["entities"])row=take(model::Entity::from_json(row)).to_json();
  for(auto& row:normalized["claims"])row=take(model::Claim::from_json(row)).to_json();
  for(auto& row:normalized["sources"])row["observation"]=take(model::Observation::from_json(row["observation"])).to_json();
  auto normalized_snapshot=take(registry.load(normalized));
  auto plan=take(registry.resolve(normalized_snapshot,Json::object(),Json{{"execution",method_channel_capabilities()},{"fusion",method_fusion_capabilities()}}));
  bool unavailable=true;Json reasons=Json::array();for(const auto& leaf:plan["leaves"]){unavailable&=!leaf["available"].get<bool>();reasons.push_back(Json{{"capability",leaf["execution_capability"]},{"reasons",leaf["unavailable_reasons"]}});}
  add("C4-REGISTRY-03","contract",unavailable&&transport->requests().empty(),Json{{"diagnostic_actual_dto_normalization",true},{"product_fixed",false},{"leaves",plan["leaves"].size()},{"unavailable",unavailable},{"reasons",reasons},{"external_requests",transport->requests().size()},{"oracle","Importing method/evidence data neither installs an executor nor authorizes a transport"}});
  auto mutated=normalized;mutated["entities"][0]["audit_unknown_field"]="synthetic";auto rejected=registry.load(mutated);
  add("C4-REGISTRY-04","contract",!rejected,Json{{"unknown_dto_field_explicitly_rejected",!rejected},{"lossless_gate_not_disabled",true}});
 }catch(const std::exception& exc){add("C4-REGISTRY-HARNESS","harness_error",false,Json{{"error_sha256",Sha256::hex(exc.what())}});}
 int failures=0;for(const auto& row:tests)if(row["status"]=="FAIL"&&row["category"]!="bug_reproduction")++failures;
 std::cout<<Json{{"tests",tests},{"failures",failures},{"real_network_calls",0}}.dump(2)<<"\n";return failures?1:0;
}
