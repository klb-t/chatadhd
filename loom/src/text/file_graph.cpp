#include "file_graph.h"
#include <map>
#include <algorithm>
#include <cctype>
#include "chat/docx_basic/adapter.h"
#include "chat/docx_basic/package.h"
#include "loom/runtime.h"
#include "loom/model.h"
#include "loom/provenance.h"
#include "loom/sqlite.h"
#include "loom/text_source_parts.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"
#include "loom/util/base64.h"
namespace loom::text::sources::host {
namespace {
Error invalid() { return Error(Errc::Conflict,"file_graph_invalid_receipt"); }
Result<Json> configuration() {
  LOOM_TRY_ASSIGN(auto root,chat::packaged_reader_directory("file_graph"));
  LOOM_TRY_ASSIGN(auto raw,fsutil::read_file(root/"runtime.json"));
  LOOM_TRY_ASSIGN(auto manifest,json::parse(raw));
  if(json::get_string(manifest,"schema")!="loom.file_graph.runtime/1") return invalid();
  for(auto it=manifest.at("files").begin();it!=manifest.at("files").end();++it) {
    std::filesystem::path rel(it.key());
    if(rel.is_absolute() || it.key().find("..")!=std::string::npos) return invalid();
    LOOM_TRY_ASSIGN(auto bytes,fsutil::read_file(root/rel));
    if(Sha256::hex(bytes)!=it.value().get<std::string>()) return invalid();
  }
  if (manifest.contains("external_tools")) for (auto it=manifest.at("external_tools").begin();it!=manifest.at("external_tools").end();++it) {
    if (it.key()!="/usr/bin/xls2csv" && it.key()!="/usr/bin/setpriv" && it.key()!="/usr/bin/ffmpeg" && it.key()!="/usr/bin/ffprobe") return invalid();
    LOOM_TRY_ASSIGN(auto tool,fsutil::read_file(it.key()));
    if (Sha256::hex(tool)!=it.value().get<std::string>()) return invalid();
  }
  auto python=manifest.at("python").get<std::string>();
  if(!std::filesystem::path(python).is_absolute() || python.find('\0')!=std::string::npos) return invalid();
  LOOM_TRY_ASSIGN(auto executable,fsutil::read_file(python));
  if(Sha256::hex(executable)!=manifest.at("python_sha256").get<std::string>()) return invalid();
  LOOM_TRY_ASSIGN(auto profile_raw,fsutil::read_file(root/"profile.json"));
  LOOM_TRY_ASSIGN(auto profile,json::parse(profile_raw));
  for(const auto& v:{profile.at("timeout_ms"),profile.at("poll_ms"),profile.at("max_output_bytes"),profile.at("max_nodes"),profile.at("archive_depth"),profile.at("archive_members"),profile.at("archive_total_bytes"),profile.at("limits").at("max_input_bytes"),profile.at("limits").at("cpu_seconds"),profile.at("limits").at("memory_bytes")})
    if(!v.is_number_integer() || v<=0) return invalid();
  profile["private_workspace"]=true;profile["python"]=python;profile["script"]=(root/"worker.py").string();profile["runtime"]=manifest;
  return profile;
}
Result<Json> parse(const std::filesystem::path& path,const std::string& hash,const std::string& format) {
  LOOM_TRY_ASSIGN(auto cfg,configuration());std::error_code ec;auto size=std::filesystem::file_size(path,ec);
  if(ec || size>cfg.at("limits").at("max_input_bytes").get<std::uint64_t>()) return invalid();
  LOOM_TRY_ASSIGN(auto before,fsutil::read_file(path));if(Sha256::hex(before)!=hash)return invalid();
  auto transport=cfg;
  if(format=="video") { transport["limits"]=cfg.at("video_transport").at("limits");transport["timeout_ms"]=cfg.at("video_transport").at("timeout_ms"); }
  LOOM_TRY_ASSIGN(auto out,chat::read_local_json(path,transport,Json{{"format",format},{"profile",cfg}}));
  LOOM_TRY_ASSIGN(auto after,fsutil::read_file(path));
  LOOM_TRY_ASSIGN(auto again,configuration());
  if(before!=after || cfg!=again || out.at("schema")!="loom.file_graph.drafts/1" || out.at("format")!=format || out.at("original_sha256")!=hash || out.at("original_size")!=size) return invalid();
  return Json{{"draft",out},{"profile",cfg}};
}
Status source(Runtime& rt,const SourceRecord& record) {
  LOOM_TRY_ASSIGN(auto old,rt.provenance().get_source(record.id));
  if(old) { if(old->blob_hash!=record.blob_hash || old->size!=record.size || old->metadata!=record.metadata)return invalid();return {}; }
  LOOM_TRY(rt.provenance().add_source(record));return {};
}
Status node(Database& db,const std::string& id,const std::string& title,const std::string& kind,const Json& meta) {
  LOOM_TRY_ASSIGN(auto old,db.get_node(id));if(old){if(old->kind!=kind || old->metadata!=meta)return invalid();return {};}
  NodeOptions o;o.node_id=id;o.metadata=meta;LOOM_TRY(db.create_node(title,kind,o));return {};
}
}
Result<Json> prepare_payload(const std::filesystem::path& path,const std::string& hash,const std::string& format) { return parse(path,hash,format); }
bool file_format(std::string_view f){return f=="xls"||f=="xlsx"||f=="odt"||f=="epub";}
Result<Prepared> prepare_file_graph(const std::filesystem::path& path,const std::string& hash,const std::string& format,const std::string& key,const std::string& name) try {
  LOOM_TRY_ASSIGN(auto parsed,parse(path,hash,format));auto raw=parsed.at("draft");auto cfg=parsed.at("profile");
  auto bytes=raw.at("text").get<std::string>();if(utf8::repair(bytes)!=bytes)return invalid();
  auto blob=Sha256::hex(json::canonical(raw));auto text_hash=Sha256::hex(bytes);
  Json transform{{"runtime",cfg.at("runtime")},{"descriptor_sha256",raw.at("descriptor_sha256")}};
  Json d{{"schema","loom.host_source_derivation/1"},{"original_source_id","src_file_"+hash},{"original_sha256",hash},{"original_size",raw.at("original_size")},{"text_sha256",text_hash},{"extraction_blob",blob},{"transform",transform},{"transform_sha256",Sha256::hex(json::canonical(transform))}};
  auto sid="src_text_"+Sha256::hex(json::canonical(Json::array({key,text_hash,d})));
  Json parts=Json::array(),observed=Json::array();std::size_t index=0;
  for(const auto& draft:raw.at("parts")){
    auto start=draft.at("range")[0].get<std::int64_t>(),stop=draft.at("range")[1].get<std::int64_t>();
    if(start<0||stop<start||static_cast<std::uint64_t>(stop)>bytes.size()||draft.at("structure").get<std::size_t>()>=raw.at("structures").size())return invalid();
    model::Unit u;u.source=sid;u.kind=draft.at("kind");u.title=u.kind;u.locator.source=sid;u.locator.byte_start=start;u.locator.byte_len=stop-start;u.bytes=stop-start;u.id=model::Unit::make_id(sid,u.locator);u.attrs=draft.at("attrs");u.attrs["structure_index"]=draft.at("structure");
    model::Observation ob;ob.unit=u.id;ob.locator=u.locator;ob.text=bytes.substr(start,stop-start);ob.kind=model::ObservationKind::Field;ob.ordinal=static_cast<int>(index++);ob.artifact_type=format;ob.attrs=u.attrs;
    ob.attrs["recognized_kind"]=u.kind;ob.attrs["original_locator"]=json::canonical(raw.at("structures")[draft.at("structure").get<std::size_t>()].at("attrs"));ob.attrs["coordinate"]="derived_utf8_bytes";ob.attrs["evidence_class"]="derived";
    ob.id=model::Observation::make_id(u.id,ob.locator,ob.text);observed.push_back(ob.to_json());parts.push_back(Json{{"unit",u.to_json()},{"parent",nullptr},{"observations",Json::array({ob.id})}});
  }
  Json out{{"schema","loom.host_structure/1"},{"text",bytes},{"extraction",raw},{"derivation",d},{"parts",parts},
      {"receipt",Json{{"original_sha256",hash},{"source",Json{{"id",sid},{"uri",key},{"title",name},{"size",bytes.size()}}},{"observations",observed},{"warnings",raw.at("warnings")}}}};
  Json binding{{"schema","loom.host_reader_binding/1"},{"format",format},{"source_key",key},{"name",name},{"input_sha256",hash},{"runtime",cfg.at("runtime")},{"derived_sha256",text_hash},{"receipt_blob",Sha256::hex(json::canonical(out))},{"derivation",d}};
  return Prepared{out,binding};
} catch(const Json::exception&){return invalid();}
Status store_file_sources(Runtime& rt,const Prepared& p,const Json& recipe) try {
  const auto& d=p.output.at("derivation");auto name=p.binding.at("name").get<std::string>();
  auto original_hash=d.at("original_sha256").get<std::string>();LOOM_TRY(rt.blobs().verify(original_hash));
  LOOM_TRY(rt.blobs().put(p.output.at("text").get<std::string>(),"text/plain"));
  LOOM_TRY(rt.blobs().put(json::canonical(p.output.at("extraction")),"application/json"));
  for(bool original:{true,false}){
    SourceRecord s;s.id=original?d.at("original_source_id").get<std::string>():"src_structure_"+d.at("extraction_blob").get<std::string>();
    s.blob_hash=original?original_hash:d.at("extraction_blob").get<std::string>();s.size=original?d.at("original_size").get<std::int64_t>():static_cast<std::int64_t>(json::canonical(p.output.at("extraction")).size());
    s.kind=recipe.at("source_kind");s.uri="blob:"+s.blob_hash;s.title=name;s.format=original?p.binding.at("format").get<std::string>():"json";s.mime=original?"application/octet-stream":"application/json";s.parser="loom.file_graph";s.metadata=Json{{"coordinate",original?"original_bytes":"json_pointer"}};
    LOOM_TRY(source(rt,s));model::Unit u;u.source=s.id;u.locator.source=s.id;u.bytes=s.size;u.id=model::Unit::make_id(s.id,u.locator);
    LOOM_TRY(node(rt.db(),"n_"+s.id,name,recipe.at("nodes").at("source").get<std::string>(),Json{{"source_id",s.id},{"sha256",s.blob_hash},{"unit",u.to_json()},{"format",s.format}}));
  }return {};
} catch(const Json::exception&){return invalid();}
Result<Json> store_file_structure(Runtime& rt,const Prepared& p,const Json& result,const Json& recipe) try {
  const auto& d=p.output.at("derivation");const auto& raw=p.output.at("extraction");auto sid="src_structure_"+d.at("extraction_blob").get<std::string>();auto source_node=result.at("source_node").get<std::string>();
  std::vector<std::string> ids;Json rows=Json::array();auto& db=rt.db();
  LOOM_TRY(db.create_link(source_node,"n_"+d.at("original_source_id").get<std::string>(),recipe.at("edges").at("source").get<std::string>(),1.0,d));
  LOOM_TRY(db.create_link(source_node,"n_"+sid,recipe.at("edges").at("source").get<std::string>()));
  for(std::size_t i=0;i<raw.at("structures").size();++i){
    const auto& draft=raw.at("structures")[i];model::Unit u;u.source=sid;u.kind=draft.at("kind");u.title=draft.at("title");u.locator.source=sid;u.locator.json_pointer="/structures/"+std::to_string(i);u.bytes=json::canonical(draft).size();u.id=model::Unit::make_id(sid,u.locator);u.attrs=draft.at("attrs");
    auto id="n_file_unit_"+Sha256::hex(u.id+":"+result.at("method_sha256").get<std::string>());std::string parent=source_node;
    if(!draft.at("parent").is_null()){auto n=draft.at("parent").get<std::size_t>();if(n>=i)return invalid();parent=ids[n];}
    LOOM_TRY(node(db,id,u.title,u.kind,Json{{"unit",u.to_json()},{"original_sha256",d.at("original_sha256")},{"extraction_blob",d.at("extraction_blob")},{"parent",parent},{"evidence_class","derived"}}));
    LOOM_TRY(db.create_link(id,parent,recipe.at("edges").at("part").get<std::string>()));ProvenanceRecord pv;pv.id="pv_"+Sha256::hex(id);pv.subject_id=id;pv.subject_kind="node";pv.source_id=sid;pv.locator=u.locator.to_json();pv.transform=d.at("transform_sha256");LOOM_TRY(rt.provenance().add(pv));
    ids.push_back(id);rows.push_back(Json{{"node_id",id},{"parent",parent},{"kind",u.kind},{"title",u.title},{"locator",u.locator.to_json()},{"attrs",u.attrs}});
  }
  for(std::size_t i=0;i<raw.at("parts").size();++i){auto index=raw.at("parts")[i].at("structure").get<std::size_t>();LOOM_TRY(db.create_link(result.at("occurrence_nodes")[i].get<std::string>(),ids.at(index),recipe.at("edges").at("part").get<std::string>()));}
  ProvenanceRecord pv;pv.id="pv_"+Sha256::hex(result.at("source_id").get<std::string>());pv.subject_id=result.at("source_id");pv.subject_kind="source";pv.source_id=d.at("original_source_id");pv.locator=Json{{"source",pv.source_id}};pv.transform=d.at("transform_sha256");LOOM_TRY(rt.provenance().add(pv));
  return rows;
} catch(const Json::exception&){return invalid();}
namespace {
Result<Json> archive(Runtime& rt,const Json& request,std::size_t depth,std::size_t& used,std::size_t& count) {
  if (request.contains("previous_source_id")) return Error(Errc::Unsupported,"archive_previous_source_id_unsupported");
  const auto hash=json::get_string(request,"hash"),key=json::get_string(request,"source_key"),name=json::get_string(request,"name");
  if(!BlobStore::is_valid_hash(hash)||key.empty()||name.empty())return invalid();
  for(auto it=request.begin();it!=request.end();++it)if(it.key()!="hash"&&it.key()!="source_key"&&it.key()!="name"&&it.key()!="format")return Error(Errc::InvalidArgument,"archive_request_override_refused");
  LOOM_TRY_ASSIGN(auto prepared,parse(rt.blobs().local_path(hash),hash,"zip"));const auto& cfg=prepared.at("profile");const auto& draft=prepared.at("draft");
  if(depth>=cfg.at("archive_depth").get<std::size_t>())return Error(Errc::InvalidArgument,"archive_depth_limit");
  const auto& tree=draft.at("extraction");std::vector<std::string> bytes;
  for(const auto& member:tree.at("members")){
    LOOM_TRY_ASSIGN(auto raw,base64::decode(member.at("base64").get<std::string>(),true));
    used+=raw.size();++count;
    if(used>cfg.at("archive_total_bytes").get<std::size_t>()||count>cfg.at("archive_members").get<std::size_t>()||Sha256::hex(raw)!=member.at("sha256").get<std::string>()||raw.size()!=member.at("size"))return invalid();
    bytes.push_back(std::move(raw));
  }
  auto& db=rt.db();auto lock=db.lock();sql::Txn txn(db.conn());LOOM_TRY(txn.begin_status());
  auto sid="src_archive_"+Sha256::hex(json::canonical(Json::array({key,hash})));auto root="n_"+sid;
  auto projection="n_file_projection_"+Sha256::hex(json::canonical(Json::array({sid,cfg.at("runtime")})));
  LOOM_TRY_ASSIGN(auto prior,db.get_node(projection));
  // Reparse the archive before reusing a receipt. Do not claim subgraph repair.
  if(prior){LOOM_TRY(txn.commit());return prior->metadata;}
  SourceRecord s;s.id=sid;s.kind="file";s.uri=key;s.title=name;s.blob_hash=hash;s.size=draft.at("original_size");s.format="zip";s.mime="application/zip";s.parser="loom.source_parts";s.metadata=Json{{"source_key",key},{"coordinate","archive_members"}};LOOM_TRY(source(rt,s));
  model::Unit u;u.source=sid;u.kind="archive";u.locator.source=sid;u.bytes=s.size;u.id=model::Unit::make_id(sid,u.locator);
  LOOM_TRY(node(db,root,name,"archive",Json{{"source_id",sid},{"sha256",hash},{"unit",u.to_json()}}));
  Json rows=Json::array(),children=Json::array();std::map<std::string,std::string> folders{{"",root}};
  for(const auto& value:tree.at("directories")){
    auto member=value.get<std::string>();auto parentpath=std::filesystem::path(member).parent_path().generic_string();auto found=folders.find(parentpath);if(found==folders.end())return invalid();
    model::Unit part;part.source=sid;part.kind="folder";part.locator.source=sid;part.locator.member=member;part.id=model::Unit::make_id(sid,part.locator);auto id="n_archive_unit_"+part.id;
    LOOM_TRY(node(db,id,member,"folder",Json{{"unit",part.to_json()},{"parent",found->second},{"original_sha256",hash}}));LOOM_TRY(db.create_link(id,found->second,"part_of"));folders[member]=id;
    rows.push_back(Json{{"node_id",id},{"parent",found->second},{"kind","folder"},{"title",member},{"locator",part.locator.to_json()}});
  }
  std::size_t index=0;
  for(const auto& member:tree.at("members")){
    const auto path=member.at("name").get<std::string>(),digest=member.at("sha256").get<std::string>();auto parent=folders.find(std::filesystem::path(path).parent_path().generic_string());if(parent==folders.end())return invalid();
    LOOM_TRY_ASSIGN(auto blob,rt.blobs().put(bytes.at(index++),"application/octet-stream"));if(blob.hash!=digest)return invalid();
    auto memberkey=key+"#zip:"+hash+":"+path;auto msid="src_member_"+Sha256::hex(json::canonical(Json::array({sid,path,digest})));
    SourceRecord m;m.id=msid;m.kind="file";m.uri=memberkey;m.title=path;m.blob_hash=digest;m.size=member.at("size");m.parser="loom.file_graph";m.format="file";m.metadata=Json{{"source_key",memberkey},{"archive_source",sid},{"member",path}};LOOM_TRY(source(rt,m));
    model::Unit part;part.source=sid;part.kind="file";part.locator.source=sid;part.locator.member=path;part.bytes=m.size;part.id=model::Unit::make_id(sid,part.locator);auto id="n_"+msid;
    LOOM_TRY(node(db,id,path,"file",Json{{"source_id",msid},{"sha256",digest},{"unit",part.to_json()},{"parent",parent->second},{"original_sha256",hash}}));LOOM_TRY(db.create_link(id,parent->second,"part_of"));
    ProvenanceRecord pv;pv.id="pv_"+Sha256::hex(msid);pv.subject_id=msid;pv.subject_kind="source";pv.source_id=sid;pv.locator=part.locator.to_json();pv.transform=draft.at("descriptor_sha256");LOOM_TRY(rt.provenance().add(pv));
    rows.push_back(Json{{"node_id",id},{"parent",parent->second},{"kind","file"},{"title",path},{"sha256",digest},{"locator",part.locator.to_json()}});
    auto ext=std::filesystem::path(path).extension().string();if(!ext.empty())ext.erase(0,1);std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(cfg.at("extensions").contains(ext)){
      auto format=cfg.at("extensions").at(ext).get<std::string>();Json child{{"hash",digest},{"source_key",memberkey},{"name",path},{"format",format}};
      LOOM_TRY_ASSIGN(auto made,format=="zip"?archive(rt,child,depth+1,used,count):sources::materialize(rt,child));
      LOOM_TRY(db.create_link(made.at("source_node").get<std::string>(),id,"part_of",1.0,Json{{"archive_source",sid},{"member",path}}));children.push_back(made);
    }else children.push_back(Json{{"source_id",msid},{"status","retained_original"},{"reason","format_not_admitted"},{"member",path}});
  }
  Json result{{"schema","loom.source_text_parts/1"},{"source_id",sid},{"source_node",root},{"source_sha256",hash},{"projection_node",projection},{"version_node",root},{"occurrence_nodes",Json::array()},{"coordinate","archive_members"},{"status","partial"},{"reason","archive_supported_members_only"},{"method",Json{{"format","zip"},{"runtime",cfg.at("runtime")},{"descriptor",draft.at("descriptor")}}},{"warnings",draft.at("warnings")},{"structure_nodes",rows},{"children",children}};
  LOOM_TRY(node(db,projection,sid,"text_source_projection",result));LOOM_TRY(db.create_link(projection,root,"source"));LOOM_TRY(txn.commit());return result;
}
}
Result<Json> materialize_archive(Runtime& rt,const Json& request) try {std::size_t bytes=0,count=0;return archive(rt,request,0,bytes,count);}catch(const Json::exception&){return invalid();}
} // namespace loom::text::sources::host
