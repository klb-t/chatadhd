#include "bundle_transport.h"
#include "file_graph.h"
#include "loom/runtime.h"
#include "loom/provenance.h"
#include "loom/importer.h"
#include "loom/sqlite.h"
#include "loom/util/sha256.h"
#include "loom/util/base64.h"
#include "loom/util/utf8.h"
#include <map>
#include <set>
namespace loom::text::sources {
namespace {
Error incomplete() {return Error(Errc::Conflict,"bundle_restore_incomplete");}
Result<Json> worker(Runtime& rt,const std::string& hash,const std::string& format) {
  LOOM_TRY_ASSIGN(auto output,host::prepare_payload(rt.blobs().local_path(hash),hash,format));
  return output.at("draft");
}
std::string attachment_hash(const Json& a) {
  if(!a.is_object())return {};
  const auto h=json::get_string(a,"hash",json::get_string(a,"blob_hash"));
  return BlobStore::is_valid_hash(h)?h:std::string();
}
}
Result<Json> bundle_transport(Runtime& rt,const Json& request) try {
  const auto op=json::get_string(request,"operation");
  const bool exporting=op=="bundle_export";
  if(!exporting && op!="bundle_import")return Error(Errc::InvalidArgument,"bundle_operation_invalid");
  for(auto it=request.begin();it!=request.end();++it)
    if(it.key()!="operation"&&it.key()!="allow_incomplete"&&
       !(exporting&&it.key()=="conversation_id")&&
       !(!exporting&&(it.key()=="hash"||it.key()=="expected_archive_sha256")))
      return Error(Errc::InvalidArgument,"bundle_override_refused");
  const bool partial=request.value("allow_incomplete",false);
  auto& db=rt.db();auto lock=db.lock();
  if(exporting) {
    ConversationExporter exporter(db);
    LOOM_TRY_ASSIGN(auto raw,exporter.export_conversation(json::get_string(request,"conversation_id"),"json",true));
    LOOM_TRY_ASSIGN(auto snapshot,json::parse(raw));
    Json bindings=Json::array(),originals=Json::object();std::size_t i=0,total=raw.size();
    for(const auto& m:snapshot.at("messages")) {
      std::size_t j=0;
      for(const auto& a:m.at("attachments")) {
        const auto hash=attachment_hash(a);
        Json b{{"pointer","/messages/"+std::to_string(i)+"/attachments/"+std::to_string(j++)},
               {"name",a.is_object()?json::get_string(a,"name"):""},{"mime",a.is_object()?json::get_string(a,"mime"):""}};
        if(hash.empty()) b["availability"]="unmanaged";
        else if(!rt.blobs().verify(hash)) { b["availability"]="missing";b["sha256"]=hash; }
        else {
          LOOM_TRY_ASSIGN(auto bytes,rt.blobs().read(hash));total+=bytes.size();
          if(total>16*1024*1024)return Error(Errc::InvalidArgument,"bundle_transport_input_bound");
          b["availability"]="included";b["sha256"]=hash;originals[hash]=base64::encode(bytes);
        }
        if(b.at("availability")!="included"&&!partial)return incomplete();
        bindings.push_back(std::move(b));
      }++i;
    }
    Json job{{"snapshot",base64::encode(raw)},{"bindings",bindings},{"originals",originals},{"allow_incomplete",partial}};
    LOOM_TRY_ASSIGN(auto input,rt.blobs().put(json::canonical(job),"application/json"));
    LOOM_TRY_ASSIGN(auto output,worker(rt,input.hash,"bundle_export"));
    LOOM_TRY_ASSIGN(auto archive,base64::decode(output.at("base64").get<std::string>(),true));
    LOOM_TRY_ASSIGN(auto stored,rt.blobs().put(archive,"application/zip"));
    if(stored.hash!=output.at("sha256").get<std::string>())return incomplete();
    return Json{{"schema","loom.bundle_native/1"},{"hash",stored.hash},{"manifest_sha256",output.at("manifest_sha256")},
      {"complete_assets",output.at("complete")},{"external_pin",stored.hash},{"lossless_database",false}};
  }
  const auto hash=json::get_string(request,"hash");
  if(!BlobStore::is_valid_hash(hash)||json::get_string(request,"expected_archive_sha256")!=hash)
    return Error(Errc::Conflict,"bundle_archive_pin_required");
  LOOM_TRY(rt.blobs().verify(hash));
  LOOM_TRY_ASSIGN(auto output,worker(rt,hash,"bundle_import"));
  const auto& manifest=output.at("manifest");const auto& snapshot=output.at("snapshot");
  if(!manifest.at("complete").get<bool>()&&!partial)return incomplete();
  std::map<std::string,Json> source_messages;
  std::map<std::string,std::string> id_pointers;
  for(std::size_t i=0;i<snapshot.at("messages").size();++i) {
    const auto& m=snapshot.at("messages")[i];auto role=json::get_string(m,"role");
    if((role!="user"&&role!="assistant")||!m.at("text").is_string()||utf8::is_blank(m.at("text").get<std::string>()))return incomplete();
    auto pointer="/messages/"+std::to_string(i);source_messages[pointer]=m;
    auto source_id=json::get_string(m,"id");
    if(source_id.empty()||!id_pointers.emplace(source_id,pointer).second)return incomplete();
  }
  sql::Txn txn(db.conn());LOOM_TRY(txn.begin_status());
  // Only the validated managed archive is interpreted, by the existing importer.
  ImportOptions options;options.export_mode=ExportMode::Off;options.record_provenance=true;
  options.zip_interpret_members=std::vector<std::string>{"conversation.json"};options.bundle_source_pointers=true;
  options.expected_source_hash=hash;options.title=json::get_string(snapshot.at("conversation"),"title");
  const auto cache_id="n_bundle_restore_"+Sha256::hex(hash+":"+output.at("manifest_sha256").get<std::string>());
  LOOM_TRY_ASSIGN(auto cached,db.get_node(cache_id));
  std::string cid;
  if (cached) cid=cached->metadata.at("conversation_id").get<std::string>();
  else {
    ConversationImporter importer(db,rt.bus(),&rt.blobs(),&rt.provenance());
    LOOM_TRY_ASSIGN(auto imported,importer.import_file(rt.blobs().local_path(hash),options));
    if(imported.cancelled||imported.conversations.size()!=1)return incomplete();
    cid=imported.conversations.front().id;
  }
  LOOM_TRY_ASSIGN(auto messages,db.get_msgs(cid,true));
  std::map<std::string,std::string> mapped;
  for(const auto& m:messages) {
    auto pointer=json::get_string(m.metadata,"bundle_source_pointer");auto src=source_messages.find(pointer);
    if(src==source_messages.end() || !mapped.emplace(pointer,m.id).second ||
        m.text!=src->second.at("text").get<std::string>() || m.role!=src->second.at("role").get<std::string>())return incomplete();
  }
  if(mapped.size()!=source_messages.size())return incomplete();
  std::map<std::string,Json> attachments;
  for(const auto& [pointer,m]:source_messages) { attachments[pointer]=Json::array();for(std::size_t i=0;i<m.at("attachments").size();++i)attachments[pointer].push_back(nullptr); }
  std::set<std::string> bound_slots;
  std::size_t available=0,missing=0;
  for(const auto& b:manifest.at("attachments")) {
    const auto pointer=b.at("pointer").get<std::string>();const auto split=pointer.rfind("/attachments/");
    if(split==std::string::npos)return incomplete();
    const auto message=pointer.substr(0,split);auto found=attachments.find(message);
    const auto slot=std::stoull(pointer.substr(split+13));
    if(found==attachments.end()||slot>=found->second.size()||!bound_slots.insert(pointer).second)return incomplete();
    if(b.at("availability")!="included") {++missing;continue;}
    const auto digest=b.at("sha256").get<std::string>();
    LOOM_TRY_ASSIGN(auto bytes,base64::decode(output.at("originals").at(digest).get<std::string>(),true));
    LOOM_TRY_ASSIGN(auto stored,rt.blobs().put(bytes,json::get_string(b,"mime","application/octet-stream")));
    if(stored.hash!=digest)return incomplete();
    LOOM_TRY(rt.blobs().verify(digest));
    found->second[slot]=Json{{"hash",digest},{"blob_hash",digest},{"name",json::get_string(b,"name")},{"mime",json::get_string(b,"mime")}};++available;
  }
  Json mapping=Json::object();
  for(const auto& [pointer,id]:mapped) {
    MsgPatch patch;patch.attachments=attachments.at(pointer);
    patch.parent_id.emplace(std::nullopt);
    const auto& src=source_messages.at(pointer);
    if(src.contains("parent_id")&&!src.at("parent_id").is_null()) {
      auto parent=id_pointers.find(src.at("parent_id").get<std::string>());
      if(parent==id_pointers.end())return incomplete();
      patch.parent_id=mapped.at(parent->second);
    }
    if (!cached) LOOM_TRY(db.update_msg(id,patch));
    LOOM_TRY_ASSIGN(auto persisted,db.get_msg(id));
    if(!persisted||persisted->attachments!=*patch.attachments ||
        (patch.parent_id && persisted->parent_id!=*patch.parent_id))return incomplete();
    mapping[pointer]=id;
  }
  Json receipt{{"schema","loom.bundle_native/1"},{"archive_sha256",hash},{"manifest_sha256",output.at("manifest_sha256")},
    {"conversation_id",cid},{"message_mapping",mapping},{"attachment_slots",available+missing},{"verified_slots",available},
    {"status",missing?"INCOMPLETE":"assets_verified"},{"complete_assets",missing==0},{"lossless_database",false},
    {"limits",Json::array({"legacy_normalization_not_database_restore","version_groups_status_metadata_retained_in_original_archive_not_recreated","SQL_rollback_does_not_remove_orphan_blob_bytes"})}};
  if (cached) {
    if (json::canonical(cached->metadata)!=json::canonical(receipt)) return incomplete();
    LOOM_TRY(txn.commit());return receipt;
  }
  NodeOptions cache;cache.node_id=cache_id;cache.metadata=receipt;
  LOOM_TRY(db.create_node(cache_id,"bundle_restore",cache));
  ConvPatch cp;cp.metadata=Json{{"bundle_restore",receipt}};LOOM_TRY(db.update_conv(cid,cp));
  LOOM_TRY(txn.commit());return receipt;
} catch(const Json::exception&) {return incomplete();} catch(const std::exception&) {return incomplete();}
}
