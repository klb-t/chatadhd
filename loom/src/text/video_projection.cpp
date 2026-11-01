#include "video_projection.h"
#include "file_graph.h"
#include "import/video_source_plan/source_plan.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/base64.h"
#include "loom/util/sha256.h"

namespace loom::text::sources::video_host {
namespace {
Error invalid() { return Error(Errc::Conflict,"video_host_binding_mismatch"); }
struct Prepared { video::SourcePlan plan; Json runtime; };
Result<Prepared> prepare(Runtime& rt,const std::string& hash,const std::string& key,const std::string& name) {
  LOOM_TRY(rt.blobs().verify(hash));
  LOOM_TRY_ASSIGN(auto raw,rt.blobs().read(hash));
  LOOM_TRY_ASSIGN(auto payload,host::prepare_payload(rt.blobs().local_path(hash),hash,"video"));
  const auto& draft=payload.at("draft");
  LOOM_TRY_ASSIGN(auto receipt,base64::decode(draft.at("receipt_base64").get<std::string>(),true));
  std::vector<std::string> bytes,names;std::vector<video::FrameBytes> frames;
  for(const auto& f:draft.at("frames")) {
    LOOM_TRY_ASSIGN(auto png,base64::decode(f.get<std::string>(),true));
    bytes.push_back(std::move(png));names.push_back("frame-"+std::to_string(names.size())+".png");
  }
  for(std::size_t i=0;i<bytes.size();++i) frames.push_back({bytes[i],names[i]});
  auto policy=draft.at("policy");policy["host_runtime"]=payload.at("profile").at("runtime");policy["source_key"]=key;
  const auto pin=draft.at("receipt_sha256").get<std::string>();
  LOOM_TRY_ASSIGN(auto plan,video::make_source_plan({raw,name,"video/octet-stream",frames,receipt,pin},policy));
  return Prepared{std::move(plan),payload.at("profile").at("runtime")};
}
Status put_source(Runtime& rt,const video::SourceSnapshot& s) {
  LOOM_TRY_ASSIGN(auto blob,rt.blobs().put(s.bytes,s.source.mime));
  if(blob.hash!=s.source.blob_hash || Sha256::hex(s.bytes)!=blob.hash) return invalid();
  LOOM_TRY_ASSIGN(auto old,rt.provenance().get_source(s.source.id));
  if(old) {
    if(old->blob_hash!=s.source.blob_hash || old->size!=s.source.size || old->metadata!=s.source.metadata || old->format!=s.source.format) return invalid();
  } else LOOM_TRY(rt.provenance().add_source(s.source));
  return {};
}
Status put_node(Database& db,const std::string& id,const std::string& kind,const std::string& content,const Json& meta) {
  LOOM_TRY_ASSIGN(auto old,db.get_node(id));
  if(old) { if(old->kind!=kind || old->content!=content || json::canonical(old->metadata)!=json::canonical(meta))return invalid();return {}; }
  NodeOptions o;o.node_id=id;o.content=content;o.metadata=meta;LOOM_TRY(db.create_node(id,kind,o));return {};
}
Status verify_source(Runtime& rt,const video::SourceSnapshot& s) {
  LOOM_TRY_ASSIGN(auto stored,rt.provenance().get_source(s.source.id));
  if(!stored || stored->blob_hash!=s.source.blob_hash || stored->size!=s.source.size || stored->metadata!=s.source.metadata) return invalid();
  LOOM_TRY(rt.blobs().verify(stored->blob_hash));return {};
}
Json frame_meta(const Prepared& p,std::size_t i,const std::string& key,const std::string& name) {
  const auto& f=p.plan.frames[i];
  return Json{{"schema","loom.video_occurrence/1"},{"source_key",key},{"name",name},{"index",i},
    {"original_sha256",p.plan.original.source.blob_hash},{"unit",f.unit.to_json()},{"observation",f.observation.to_json()},
    {"pixels_source",f.pixels.source.id},{"frame_sha256",f.pixels.source.blob_hash},
    {"receipt_source",p.plan.receipt.source.id},{"receipt_sha256",p.plan.receipt.source.blob_hash},
    {"method",p.plan.method},{"runtime",p.runtime},{"has_ocr",f.image.has_ocr},{"evidence_class","derived"}};
}
}
Result<Json> materialize(Runtime& rt,const Json& request) try {
  for(auto it=request.begin();it!=request.end();++it)
    if(it.key()!="hash"&&it.key()!="source_key"&&it.key()!="name"&&it.key()!="format")
      return Error(Errc::InvalidArgument,"video_request_override_refused");
  const auto hash=json::get_string(request,"hash"),key=json::get_string(request,"source_key"),name=json::get_string(request,"name");
  if(!BlobStore::is_valid_hash(hash)||key.empty()||name.empty())return invalid();
  LOOM_TRY_ASSIGN(auto p,prepare(rt,hash,key,name)); // All external bytes admitted before mutations.
  const auto& plan=p.plan;
  auto& db=rt.db();auto lock=db.lock();sql::Txn txn(db.conn());LOOM_TRY(txn.begin_status());
  auto projection="n_video_projection_"+Sha256::hex(json::canonical(Json::array({key,plan.method})));
  const auto root="n_"+plan.original.source.id,receipt_node="n_"+plan.receipt.source.id;
  Json result{{"schema","loom.video_materialization/1"},{"source_node",root},{"source_id",plan.original.source.id},
    {"original_sha256",hash},{"source_key",key},{"projection_node",projection},{"receipt_source",plan.receipt.source.id},
    {"receipt_sha256",plan.receipt.source.blob_hash},{"method",plan.method},{"runtime",p.runtime},
    {"occurrence_nodes",Json::array()},{"status","partial"},{"whole_video_coverage",false},{"has_ocr",false},{"visual_interpretation",false}};
  LOOM_TRY_ASSIGN(auto cached,db.get_node(projection));
  if(cached) {
    Json expected=result;for(const auto& frame:plan.frames)expected["occurrence_nodes"].push_back("n_"+frame.unit.id);
    if(json::canonical(cached->metadata)!=json::canonical(expected))return invalid();
    LOOM_TRY(verify_source(rt,plan.original));LOOM_TRY(verify_source(rt,plan.receipt));
    for(std::size_t i=0;i<plan.frames.size();++i) {
      LOOM_TRY(verify_source(rt,plan.frames[i].pixels));
      LOOM_TRY_ASSIGN(auto n,db.get_node("n_"+plan.frames[i].unit.id));
      if(!n || json::canonical(n->metadata)!=json::canonical(frame_meta(p,i,key,name)))return invalid();
    }
    LOOM_TRY(txn.commit());return cached->metadata;
  }
  LOOM_TRY(put_source(rt,plan.original));LOOM_TRY(put_source(rt,plan.receipt));
  LOOM_TRY(rt.provenance().add(plan.receipt_provenance));
  LOOM_TRY(put_node(db,root,plan.original_unit.kind,"",Json{{"source_id",plan.original.source.id},{"unit",plan.original_unit.to_json()},{"sha256",hash}}));
  LOOM_TRY(put_node(db,receipt_node,plan.receipt_unit.kind,"",Json{{"source_id",plan.receipt.source.id},{"unit",plan.receipt_unit.to_json()},{"sha256",plan.receipt.source.blob_hash}}));
  LOOM_TRY(db.create_link(receipt_node,root,"source"));
  auto pack=builtin_pack();
  LOOM_TRY_ASSIGN(auto seg,Segmenter::from_pack(pack,json::get_string(pack,"default_language")));
  LOOM_TRY_ASSIGN(auto options,genesis::Options::from_pack(pack));
  for(std::size_t i=0;i<plan.frames.size();++i) {
    const auto& f=plan.frames[i];LOOM_TRY(put_source(rt,f.pixels));
    for(const auto& pv:f.provenance) LOOM_TRY(rt.provenance().add(pv));
    const auto id="n_"+f.unit.id,pixels="n_"+f.pixels.source.id;
    LOOM_TRY(put_node(db,pixels,"derived_image","",Json{{"source_id",f.pixels.source.id},{"sha256",f.pixels.source.blob_hash},{"has_ocr",false}}));
    LOOM_TRY(put_node(db,id,f.unit.kind,"",frame_meta(p,i,key,name)));
    LOOM_TRY(put_node(db,"n_"+f.observation.id,"field",f.observation.text,f.observation.to_json()));
    LOOM_TRY(db.create_link(id,receipt_node,"part_of"));LOOM_TRY(db.create_link(id,pixels,"source"));
    LOOM_TRY(db.create_link("n_"+f.observation.id,id,"part_of"));
    LOOM_TRY_ASSIGN(auto chain,genesis::compute_chain(seg,{f.genesis},options));
    LOOM_TRY(genesis::materialize(db,chain,options));
    result["occurrence_nodes"].push_back(id);
  }
  LOOM_TRY(put_node(db,projection,"video_projection","",result));
  LOOM_TRY(db.create_link(projection,receipt_node,"source"));
  LOOM_TRY(txn.commit());return result;
} catch(const Json::exception&) {return invalid();}
Result<Json> read(Runtime& rt,const Json& request) try {
  for(auto it=request.begin();it!=request.end();++it)if(it.key()!="node_id")return Error(Errc::InvalidArgument,"video_read_override_refused");
  LOOM_TRY_ASSIGN(auto n,rt.db().get_node(json::get_string(request,"node_id")));
  if(!n || json::get_string(n->metadata,"schema")!="loom.video_occurrence/1")return invalid();
  const auto& m=n->metadata;const auto key=m.at("source_key").get<std::string>(),name=m.at("name").get<std::string>();
  LOOM_TRY_ASSIGN(auto p,prepare(rt,m.at("original_sha256").get<std::string>(),key,name));
  const auto i=m.at("index").get<std::size_t>();
  if(i>=p.plan.frames.size() || n->id!="n_"+p.plan.frames[i].unit.id || json::canonical(m)!=json::canonical(frame_meta(p,i,key,name)))return invalid();
  LOOM_TRY(verify_source(rt,p.plan.original));LOOM_TRY(verify_source(rt,p.plan.receipt));LOOM_TRY(verify_source(rt,p.plan.frames[i].pixels));
  return Json{{"node_id",n->id},{"provenance",m},{"frame_sha256",p.plan.frames[i].pixels.source.blob_hash},
              {"original_sha256",p.plan.original.source.blob_hash},{"has_ocr",false},{"mime","image/png"}};
} catch(const Json::exception&) {return invalid();}
}
