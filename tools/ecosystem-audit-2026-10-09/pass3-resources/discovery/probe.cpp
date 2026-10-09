// Independent discovery/adaptation boundary probes. Fixtures only; all consumers
// come from the pinned native archive. No replacement registry/resolver.
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "loom/runtime.h"
#include "loom/knowledge.h"
#include "loom/providers.h"
#include "loom/importer.h"
#include "loom/graph_packet_store.h"
#include "loom/net/http.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "context/context_execution.h"
#include "context/method_registry.h"
#include "context/method_channels.h"
#include "providers/embedding.h"
using namespace loom;
using namespace loom::context;
Json results=Json::array();
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().to_string());return std::move(r).value();}
void okay(Status s){if(!s)throw std::runtime_error(s.error().to_string());}
void check(const char* id,bool pass,Json e=Json::object(),const char* category="acceptance"){
 results.push_back(Json{{"id",id},{"acceptance",pass?"PASS":"FAIL"},{"category",category},{"evidence",e}});
}
const char* now="2026-10-09T00:00:00Z";
Json vocabulary(){Json k=Json::object(),p=Json::object();
 for(const char* s:{"method","method_version","prompt_version","recipe_version","preset_version","combination_version","parameter_set_version","run","model_identity","compiler_transform","result"})k[s]="audit."+std::string(s);
 for(const char* s:{"version_of","uses_recipe","uses_prompt","uses_preset","includes_method","uses_parameter_set","uses_combination","requests_method_version","produced_in_run","produced_by_method_version","projected_by_compiler"})p[s]="audit."+std::string(s);
 return Json{{"kinds",k},{"predicates",p}};}
Json entity(std::string id,const char* kind,Json attrs=Json::object()){
 model::Entity e;e.id=id;e.canonical_key=id;e.kind=vocabulary()["kinds"][kind];e.label=id;e.evidence=model::EvidenceClass::User;e.origin=model::Origin::User;e.first_seen=now;e.last_seen=now;e.attrs=std::move(attrs);return e.to_json();}
Json settings(std::string capability,std::string pattern="quartz",bool alternate=false){
 Json params{{"limit",100},{"min_score",0}};
 if(capability=="regex")params.update(Json{{"patterns",Json::array({pattern})},{"flags",Json::array({"ECMAScript"})},{"match","search"},{"semantics","match_count"}});
 Json definition{{"execution_capability",capability},{"parameters",params},{"discovery",Json{{"source_kind","synthetic_local_description"},{"interpretation_status","hypothesis"},{"alternative_id",alternate?"B":"A"},{"validation_status","not_tested"}}}};
 auto text=json::canonical(definition);
 model::Observation o;o.id="audit_source";o.unit="audit.discovered.description";o.kind=model::ObservationKind::Field;o.text=text;o.locator.source="sha256:"+Sha256::hex(text);o.locator.byte_start=0;o.locator.byte_len=text.size();
 model::Claim c;c.subject="audit_version";c.predicate=vocabulary()["predicates"]["version_of"];c.object="audit_method";c.qualifiers.extra=Json{{"content_truth","not_established"}};c.assessment.evidence=model::EvidenceClass::Derived;c.assessment.origin=model::Origin::System;c.assessment.confidence=1;c.assessment.derivation=model::Derivation{"audit.fixture",1,"",0};c.assessment.support.push_back(model::Support{o.id,o.locator,text,"audit.fixture",1});c.id=model::Claim::make_id(c.subject,c.predicate,c.object,c.value,c.qualifiers);
 Json profile{{"vocabulary",vocabulary()},{"entities",Json::array({entity("audit_method","method"),entity("audit_version","method_version",Json{{"definition",definition},{"definition_sha256",Sha256::hex(text)}})})},{"claims",Json::array({c.to_json()})},{"sources",Json::array({Json{{"observation",o.to_json()},{"known_at",now},{"text_sha256",Sha256::hex(text)}}})},
 {"selection",Json{{"members",Json::array({Json{{"method_version_id","audit_version"}}})},{"parameter_layers",Json::array({"method","member","selection","user"})},{"fusion",Json{{"operation","sum"},{"signal","raw_score"}}}}}};
 return Json{{"enabled",true},{"profile",profile},{"graph_blend_operation","method_only"},{"diversity",Json{{"operation","identity"}}},{"run_context",Json{{"origin",Json{{"kind","system"},{"actor","synthetic-discovery-audit"},{"model",nullptr},{"recipe_sha256",nullptr},{"response_sha256",nullptr}}},{"known_at",now}}}};
}
struct Fixture{
 fsutil::TempDir dir;
 std::shared_ptr<net::ScriptedTransport> http=std::make_shared<net::ScriptedTransport>();
 std::unique_ptr<Runtime> rt;std::shared_ptr<const kb::Pack> pack;std::unique_ptr<ContextEngine> engine;ContextRequest req;
 Fixture(){RuntimeOptions options;options.data_dir=dir.path().string();options.start_workers=false;options.http=http;rt=take(Runtime::open(options));rt->config().set("semantic_analysis",false);pack=take(rt->knowledge().pack());auto& store=rt->knowledge().store();req.run=take(store.begin_run(pack->hash(),Json{{"fixture","audit.discovery"}})).id;
 std::vector<model::Entity> entities;std::vector<model::Claim> claims;
 for(auto pair:std::vector<std::pair<std::string,std::string>>{{"a","quartz quartz cobalt"},{"b","cobalt cobalt quartz"}}){model::Entity e;e.id="audit_entity_"+pair.first;e.canonical_key=e.id;e.kind="audit.fixture";e.label=e.id;e.evidence=model::EvidenceClass::User;e.origin=model::Origin::User;entities.push_back(e);model::Claim c;c.id="audit_claim_"+pair.first;c.subject=e.id;c.predicate="records";c.value=pair.second;c.assessment.evidence=model::EvidenceClass::User;c.assessment.origin=model::Origin::User;c.assessment.confidence=1;claims.push_back(c);}okay(store.put_entities(req.run,entities));okay(store.put_claims(req.run,claims));okay(store.finish_run(req.run,"done",Json::object()));req.text="inspect synthetic source structure";req.goal_type="verify_claim";req.budget_tokens=2000;req.relation_hops=0;engine=std::make_unique<ContextEngine>(*rt,store,pack);}
 Result<model::ContextSet> select(Json s){ContextExecutionScope scope(Json{{"method_registry",s}});return engine->select(req);}
};
struct CountChannel final:CandidateChannel{int calls=0;RetrievalBatch retrieve(std::string_view,const std::vector<RetrievalDocument>& corpus,const CandidateChannelRequest&)override{++calls;RetrievalBatch b;b.method="audit_injected";b.status="unavailable";b.reason="fixture_no_measurements";b.corpus_count=corpus.size();return b;}};
void method_tests(Fixture& f){
 auto a=settings("regex","quartz"),b=settings("regex","cobalt",true);auto full=take(f.select(a));int max=0;for(auto& x:full.items)max=std::max(max,x.tokens);f.req.budget_tokens=max;
 auto ra=take(f.select(a)),rb=take(f.select(b));auto& pa=ra.goal.params["method_registry"];auto& pb=rb.goal.params["method_registry"];
 check("DISC-NATIVE-01",ra.items.size()==1&&rb.items.size()==1&&ra.items[0].ref=="audit_claim_a"&&rb.items[0].ref=="audit_claim_b"&&pa["resolution"]["resolution_sha256"]!=pb["resolution"]["resolution_sha256"],Json{{"first_profile_ref",ra.items.empty()?"":ra.items[0].ref},{"second_profile_ref",rb.items.empty()?"":rb.items[0].ref},{"actual_resolution_hashes_differ",pa["resolution"]["resolution_sha256"]!=pb["resolution"]["resolution_sha256"]},{"canonical_graph_written",pa["result_graph"]["canonical_store_written"]}});
 auto invalid=a;invalid["selection"]["user_overrides"]["unsupported_adapter_option"]="synthetic";auto rejected=f.select(invalid);check("DISC-NATIVE-02",!rejected,Json{{"error_code",rejected?"none":errc_name(rejected.error().code)},{"error_sha256",rejected?"":Sha256::hex(rejected.error().message)}});
 auto unavailable=settings("audit_nonexistent_adapter");unavailable["capabilities"]["execution"]["audit_nonexistent_adapter"]["available"]=true;
 auto missing=take(f.select(unavailable));auto plan=missing.goal.params["method_registry"]["resolution"];check("DISC-NATIVE-03",!plan["leaves"][0]["available"].get<bool>()&&f.http->requests().empty(),Json{{"declared_capability","audit_nonexistent_adapter"},{"runtime_available",plan["leaves"][0]["available"]},{"request_metadata_did_not_install_code",true},{"reasons",plan["leaves"][0]["unavailable_reasons"]}});
 auto injected=std::make_shared<CountChannel>();f.engine->set_candidate_channel("audit_injected",injected);auto selected=settings("audit_injected");auto no_binding=take(f.select(selected));check("DISC-NATIVE-04",injected->calls==0&&!no_binding.goal.params["method_registry"]["resolution"]["leaves"][0]["available"].get<bool>(),Json{{"injected_calls",injected->calls},{"binding_absent",true}});
 selected["capability_bindings"]["audit_injected"]["actual_parameters"]=Json::object();auto bound=take(f.select(selected));check("DISC-NATIVE-05",injected->calls==1,Json{{"injected_calls",injected->calls},{"actual_parameters_explicit",true},{"authorization_boundary","trusted native caller, not discovered descriptor"}});
 MethodRegistry registry(f.rt->db());auto snap=take(registry.load(a["profile"]));bool preserved=snap["entities"]["audit_version"]["attrs"]["definition"]["discovery"]["interpretation_status"]=="hypothesis";check("DISC-NATIVE-06",preserved,Json{{"hypothesis_metadata_preserved",preserved},{"automatic_validation_or_activation_claimed",false}});
 auto reordered=Json::parse(json::canonical(a["profile"]));auto reordered_load=registry.load(reordered);
 check("DISC-NATIVE-07",static_cast<bool>(reordered_load),Json{{"canonical_bytes_equal",json::canonical(a["profile"])==json::canonical(reordered)},{"only_object_key_order_changed",true},{"error_code",reordered_load?"none":errc_name(reordered_load.error().code)},{"error_sha256",reordered_load?"":Sha256::hex(reordered_load.error().message)},{"reproduction",reordered_load?"FAIL":"PASS"}});
 for(const auto& collection:std::vector<std::string>{"entities","claims"}){
  auto permutation=a["profile"];permutation[collection][0]=Json::parse(json::canonical(permutation[collection][0]));auto result=registry.load(permutation);
  check(collection=="entities"?"DISC-NATIVE-09":"DISC-NATIVE-10",static_cast<bool>(result),Json{{"only_collection_reordered",collection},{"canonical_bytes_equal",json::canonical(permutation)==json::canonical(a["profile"])},{"error_code",result?"none":errc_name(result.error().code)},{"error_sha256",result?"":Sha256::hex(result.error().message)},{"reproduction",result?"FAIL":"PASS"}});
 }
 auto unknown=a["profile"];unknown["entities"][0]["unsupported_top_level_field"]="synthetic";auto unknown_result=registry.load(unknown);check("DISC-NATIVE-08",!unknown_result,Json{{"unknown_dto_field_explicitly_rejected",!unknown_result}});
 check("DISC-NETWORK-00",f.http->requests().empty(),Json{{"transport_calls",f.http->requests().size()},{"live_network_possible",false}});
}
void provider_tests(Fixture& f){
 auto& reg=f.rt->providers();Json manifest{{"id","audit_external"},{"display_name","Synthetic external capability"},{"base_url","https://synthetic.invalid/v1"},{"auth_secret",""},{"auth_scheme","none"},{"capabilities",Json::array({Json{{"resource","embedding"},{"name","embed"},{"constraints",Json{{"models",Json::array({"fixture/model"})},{"modalities",Json::array({"text"})}}}}})},{"metadata",Json{{"embedding_adapter","not_implemented"},{"discovery",Json{{"evidence","not_tested"},{"permission","not_granted"}}}}}};
 okay(reg.load(Json::array({manifest})));providers::EmbeddingRequest req;req.provider_id="audit_external";req.model="fixture/model";
 auto cap=providers::embedding_capability(reg,f.rt->secrets(),req);bool attempted=false;auto noimpl=providers::provider_embed(reg,f.rt->secrets(),*f.http,req,{"synthetic public input"},&attempted);
 check("DISC-PROVIDER-01",reg.can("embedding","embed")&&!cap["available"].get<bool>()&&!noimpl&&!attempted,Json{{"manifest_can",reg.can("embedding","embed")},{"adapter_available",cap["available"]},{"adapter_error_code",noimpl?"none":errc_name(noimpl.error().code)},{"request_attempted",attempted},{"meaning","ProviderRegistry.can is a declaration+credential match, not implementation or authorization."}});
 manifest["metadata"]["embedding_adapter"]="openai";manifest["metadata"]["embedding_path"]="/test-embeddings";okay(reg.load(Json::array({manifest})));cap=providers::embedding_capability(reg,f.rt->secrets(),req);auto before=f.http->requests().size();auto denied=providers::provider_embed(reg,f.rt->secrets(),*f.http,req,{"synthetic public input"},&attempted);
 check("DISC-PROVIDER-02",cap["available"].get<bool>()&&!cap["calls_authorized"].get<bool>()&&!denied&&!attempted&&f.http->requests().size()==before,Json{{"readiness",cap["readiness"]},{"live_verification",cap["live_verification"]},{"calls_authorized",cap["calls_authorized"]},{"request_attempted",attempted}});
 req.calls_authorized=true;f.http->expect("POST","https://synthetic.invalid/v1/test-embeddings",net::ScriptedTransport::Reply::json(200,Json{{"model","fixture/model"},{"data",Json::array({Json{{"index",0},{"embedding",Json::array({0.25,0.75})}}})}}));auto sent=providers::provider_embed(reg,f.rt->secrets(),*f.http,req,{"synthetic public input"},&attempted);
 auto calls=f.http->requests();check("DISC-PROVIDER-03",static_cast<bool>(sent)&&attempted&&calls.size()==before+1&&calls.back().url=="https://synthetic.invalid/v1/test-embeddings",Json{{"scripted_transport_calls",calls.size()-before},{"actual_manifest_endpoint_used",calls.back().url=="https://synthetic.invalid/v1/test-embeddings"},{"real_network_calls",0},{"authorization_supplied_by_harness",true}});
 auto status=reg.status();Json declaration_status=nullptr;for(auto& x:status["providers"])if(x["id"]=="audit_external")declaration_status=x;
 results.push_back(Json{{"id","DISC-PROVIDER-04"},{"acceptance","BLOCKED"},{"category","unbound_new_requirement"},{"evidence",Json{{"legacy_status",declaration_status},{"actual_embedding_capability",cap},{"full_manifest_metadata_retained",reg.get("audit_external")->metadata.contains("discovery")},{"reason","No integrated discovery consumer contract bound for this probe; do not prescribe new field names or treat legacy can as permission."},{"oracle","Declaration, implementation, correctness evidence and permission must be independently observable through approved contract fields or equivalent explainable facets. Component distinctions are tested separately."}}}});
}
void research_test(Fixture& f,const char* path){std::ifstream input(path);Json payload;input>>payload;Json profile=payload["profile"];const auto& packet=payload["packet"];
 Json selection=Json::object(),expected=Json::object();for(const char* col:{"entities","claims","sources"}){selection[col]=Json::array();expected[col]=Json::object();for(const auto& row:packet[col]){auto id=std::string_view(col)=="sources"?row["observation"]["id"]:row["id"];selection[col].push_back(id);expected[col][id.get<std::string>()]=nullptr;}}
 kb::GraphPacketStore store(f.rt->db());auto accepted=store.execute(Json{{"operation","accept"},{"packet",packet},{"target","audit-discovery-c2"},{"selection",selection},{"expected_rows",expected},{"explicitly_accepted",true}});
 check("DISC-C2-00",static_cast<bool>(accepted),Json{{"actual_native_lossless_store_acceptance",static_cast<bool>(accepted)},{"establishes_method_execution",false}});
 MethodRegistry reg(f.rt->db());auto snap=reg.load(profile);
 check("DISC-C2-01",static_cast<bool>(snap),Json{{"research_packet_registry_load",static_cast<bool>(snap)},{"error_code",snap?"none":errc_name(snap.error().code)},{"error_sha256",snap?"":Sha256::hex(snap.error().message)},{"reproduction",snap?"FAIL":"PASS"}});
 // Diagnostic only: use actual DTO decoders to change object ordering. This
 // fixture normalization does not repair the target or replace its registry.
 Json normalized=profile;
 for(auto& row:normalized["entities"])row=take(model::Entity::from_json(row)).to_json();
 for(auto& row:normalized["claims"])row=take(model::Claim::from_json(row)).to_json();
 for(auto& row:normalized["sources"])row["observation"]=take(model::Observation::from_json(row["observation"])).to_json();
 const bool same=json::canonical(normalized)==json::canonical(profile);
 const bool semantic_same=nlohmann::json::parse(profile.dump())==nlohmann::json::parse(normalized.dump());
 Json changes=Json::array();auto prior=profile.flatten(),later=normalized.flatten();for(auto it=prior.begin();it!=prior.end();++it){if(later.contains(it.key())&&it.value().dump()!=later[it.key()].dump())changes.push_back(Json{{"path",it.key()},{"before_type",it.value().is_number_float()?"float":it.value().is_number_unsigned()?"unsigned":it.value().type_name()},{"after_type",later[it.key()].is_number_float()?"float":later[it.key()].is_number_unsigned()?"unsigned":later[it.key()].type_name()},{"before_numeric",it.value().is_number()?it.value():Json(nullptr)},{"after_numeric",later[it.key()].is_number()?later[it.key()]:Json(nullptr)}});}

 auto normalized_snapshot=reg.load(normalized);
 if(!normalized_snapshot){check("DISC-C2-02",false,Json{{"canonical_semantics_preserved",same},{"load_error",errc_name(normalized_snapshot.error().code)},{"error_sha256",Sha256::hex(normalized_snapshot.error().message)}});return;}
 auto plan=take(reg.resolve(*normalized_snapshot,Json::object(),Json{{"execution",method_channel_capabilities()},{"fusion",method_fusion_capabilities()}}));
 bool unavailable=true;for(auto& leaf:plan["leaves"])unavailable&=!leaf["available"].get<bool>();check("DISC-C2-02",static_cast<bool>(accepted)&&semantic_same&&unavailable,Json{{"actual_dto_roundtrip",true},{"canonical_bytes_equal",same},{"semantic_values_equal",semantic_same},{"scalar_representation_changes",changes},{"native_store_lossless_gate_also_passes",static_cast<bool>(accepted)},{"research_packet_registry_load",true},{"leaves",plan["leaves"].size()},{"unsupported_execution_remains_unavailable",unavailable},{"capability_name",plan["leaves"][0]["execution_capability"]},{"hypotheses_and_evidence_import_is_not_code_install",true}});
}
void ambiguity_tests(Fixture& f,const char* first,const char* second){
 auto before=f.http->requests().size();auto one=take(f.rt->importer().import_file(first));auto two=take(f.rt->importer().import_file(second));
 if(one.conversations.size()!=1||two.conversations.size()!=1){check("DISC-IMPORT-00",false,Json{{"first_conversations",one.conversations.size()},{"second_conversations",two.conversations.size()}});return;}
 auto a=one.conversations[0].metadata["export"],b=two.conversations[0].metadata["export"];
 auto messages_a=take(f.rt->db().get_msgs(one.conversations[0].id,true));auto messages_b=take(f.rt->db().get_msgs(two.conversations[0].id,true));
 const bool different=messages_a.size()==1&&messages_b.size()==1&&messages_a[0].text!=messages_b[0].text;
 bool alternatives=a.contains("interpretation_alternatives")&&b.contains("interpretation_alternatives");
 check("DISC-IMPORT-01",!different||alternatives,Json{{"source_object_key_order_only_changed",true},{"first_selected_array",a.value("message_array",Json(nullptr))},{"second_selected_array",b.value("message_array",Json(nullptr))},{"effective_messages_differ",different},{"explicit_alternative_interpretations",alternatives},{"reproduction",different&&!alternatives?"PASS":"FAIL"},{"both_conversations_written",true}});
 check("DISC-IMPORT-02",a.value("inferred",false)&&b.value("inferred",false)&&a["fields"].contains("beta")&&b["fields"].contains("alpha")&&f.http->requests().size()==before,Json{{"inference_marked",a.value("inferred",false)&&b.value("inferred",false)},{"unselected_array_raw_bytes_structurally_retained",a["fields"].contains("beta")&&b["fields"].contains("alpha")},{"external_calls",0},{"boundary","Raw retention and inferred flag do not represent selectable alternative mappings."}});
}

int main(int argc,char**argv){try{Fixture fixture;method_tests(fixture);provider_tests(fixture);if(argc>1)research_test(fixture,argv[1]);if(argc>3)ambiguity_tests(fixture,argv[2],argv[3]);}catch(const std::exception& e){check("DISC-HARNESS",false,Json{{"error_sha256",Sha256::hex(e.what())},{"error_class","std_exception"}});}int failures=0;for(auto& r:results)failures+=r["acceptance"]=="FAIL";std::cout<<Json{{"tests",results},{"failures",failures},{"real_network_calls",0}}.dump(2)<<"\n";return failures?1:0;}
