#include "projection_upgrade.h"
#include "file_graph.h"
#include "loom/text_source_parts.h"
#include "loom/runtime.h"
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/sqlite.h"
#include "loom/util/sha256.h"
#include <set>
#include <algorithm>

namespace loom::text::sources {
namespace {
Error invalid() { return Error(Errc::Conflict, "projection_saved_binding_invalid"); }
Status blob(Runtime& rt, const Json& hash) {
  if (!hash.is_string() || !BlobStore::is_valid_hash(hash.get<std::string>())) return invalid();
  return rt.blobs().verify(hash.get<std::string>());
}
// Integrity of the retained snapshot, NOT an assertion that an old AST/reader is
// trusted by the current installation. Only the current host may parse again.
Result<SourceRecord> snapshot(Runtime& rt, const Node& projection) {
  const auto& p = projection.metadata;
  if (p.at("schema") != "loom.source_text_parts/1" ||
      p.at("projection_node") != projection.id ||
      Sha256::hex(json::canonical(p.at("method"))) != p.at("method_sha256").get<std::string>() ||
      projection.id != "n_text_projection_" + Sha256::hex(p.at("source_id").get<std::string>() + ":" + p.at("method_sha256").get<std::string>())) return invalid();
  LOOM_TRY_ASSIGN(auto source, rt.provenance().get_source(p.at("source_id").get<std::string>()));
  if (!source || source->blob_hash != p.at("source_sha256").get<std::string>() || source->format != p.at("method").at("format").get<std::string>()) return invalid();
  LOOM_TRY(rt.blobs().verify(source->blob_hash));
  LOOM_TRY(blob(rt, p.at("method").at("text_pack_blob")));
  LOOM_TRY(blob(rt, p.at("method").at("kb_pack_blob")));
  if (p.at("method").contains("host_reader")) LOOM_TRY(blob(rt, p.at("method").at("host_reader").at("receipt_blob")));
  if (p.contains("derivation")) {
    const auto& d = p.at("derivation");
    if (json::canonical(d) != json::canonical(source->metadata.at("derivation")) ||
        json::canonical(d) != json::canonical(p.at("method").at("derivation")) ||
        Sha256::hex(json::canonical(d.at("transform"))) != d.at("transform_sha256").get<std::string>()) return invalid();
    LOOM_TRY(blob(rt, d.at("original_sha256")));
    LOOM_TRY(blob(rt, d.at("extraction_blob")));
    if (d.at("transform").contains("reader_script_blob")) LOOM_TRY(blob(rt, d.at("transform").at("reader_script_blob")));
    LOOM_TRY_ASSIGN(auto original, rt.provenance().get_source(d.at("original_source_id").get<std::string>()));
    if (!original || original->blob_hash != d.at("original_sha256").get<std::string>() || original->size != d.at("original_size")) return invalid();
  }
  LOOM_TRY_ASSIGN(auto bytes, rt.blobs().read(source->blob_hash));
  std::set<std::string> seen;
  for (const auto& id : p.at("occurrence_nodes")) {
    if (!seen.insert(id.get<std::string>()).second) return invalid();
    LOOM_TRY_ASSIGN(auto n, rt.db().get_node(id.get<std::string>()));
    if (!n || n->metadata.at("source_id") != source->id ||
        json::canonical(n->metadata.at("method")) != json::canonical(p.at("method"))) return invalid();
    LOOM_TRY(validate_occurrence(rt.db(), *n));
    const auto start = n->metadata.at("byte_range")[0].get<std::size_t>();
    const auto stop = n->metadata.at("byte_range")[1].get<std::size_t>();
    if (start > stop || stop > bytes.size() || bytes.substr(start, stop-start) != n->content) return invalid();
    LOOM_TRY_ASSIGN(auto records, rt.provenance().for_subject(n->id));
    bool bound = false;
    for (const auto& record : records) if (record.id == "pv_" + Sha256::hex(n->id) && record.source_id == source->id &&
        record.locator == n->metadata.at("unit").at("locator") &&
        record.transform == "loom.source_parts@1:" + p.at("method_sha256").get<std::string>()) bound = true;
    if (!bound) return invalid();
  }
  return *source;
}
}
Result<Json> projection_upgrade(Runtime& rt, const Json& request) try {
  const auto operation = json::get_string(request, "operation");
  if (operation != "projection_check" && operation != "projection_reproject")
    return Error(Errc::InvalidArgument, "projection_operation_invalid");
  for (auto it=request.begin(); it!=request.end(); ++it)
    if (it.key()!="operation" && it.key()!="projection_node" && it.key()!="expected_receipt_sha256" && it.key()!="selected_nodes")
      return Error(Errc::InvalidArgument, "projection_override_refused");
  auto& db = rt.db(); auto lock = db.lock();
  LOOM_TRY_ASSIGN(auto old, db.get_node(json::get_string(request, "projection_node")));
  if (!old) return Error(Errc::NotFound, "projection_missing");
  // Archive member versions require a separate explicit tree migration; never
  // flatten or replace them through this text-projection operation.
  if (json::get_string(old->metadata.value("method", Json::object()), "format") == "zip") {
    if (operation == "projection_reproject")
      return Error(Errc::Unsupported, "archive_projection_upgrade_unsupported");
    // Archive occurrences are owned by child projections, not the root receipt.
    const auto selected = request.value("selected_nodes", Json::array());
    if (!selected.is_array() || !selected.empty())
      return Error(Errc::InvalidArgument, "projection_selection_not_owned");
    return host::preview_archive(rt, old->metadata);
  }
  LOOM_TRY_ASSIGN(auto source, snapshot(rt, *old));
  const auto& p=old->metadata;
  const auto digest=Sha256::hex(json::canonical(p));
  if (operation=="projection_reproject" && json::get_string(request,"expected_receipt_sha256")!=digest)
    return Error(Errc::Conflict,"projection_confirmation_stale");
  Json selected=request.value("selected_nodes",Json::array());
  if (!selected.is_array()) return invalid();
  std::set<std::string> selection;
  for (const auto& id:selected) {
    if (!id.is_string() || !selection.insert(id.get<std::string>()).second ||
        std::find(p.at("occurrence_nodes").begin(),p.at("occurrence_nodes").end(),id)==p.at("occurrence_nodes").end())
      return Error(Errc::InvalidArgument,"projection_selection_not_owned");
  }
  bool compatible=true; Json failures=Json::array();
  for (const auto& id:p.at("occurrence_nodes")) {
    const auto r=read(rt,Json{{"node_id",id}});
    if (!r) { compatible=false; failures.push_back(Json{{"node_id",id},{"code",errc_name(r.error().code)}}); }
  }
  Json status{{"schema","loom.projection_upgrade/1"},{"old_projection",old->id},{"old_receipt_sha256",digest},
      {"saved_integrity","verified"},{"current_replay",p.at("occurrence_nodes").empty()?"not_exercised":(compatible?"compatible":"refused")},
      {"refusals",failures},{"selection_scope","account_global"},{"selection_changed",false}};
  if (operation=="projection_check") return status;
  // Original bytes and identity come only from the checked local snapshot.
  Json fresh{{"hash",p.contains("derivation")?p.at("derivation").at("original_sha256"):p.at("source_sha256")},
      {"source_key",source.metadata.at("source_key")},{"name",source.title},{"format",source.format}};
  sql::Txn txn(db.conn()); LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto made,materialize(rt,fresh));
  status["new_projection"]=made.at("projection_node");
  status["new_receipt_sha256"]=Sha256::hex(json::canonical(made));
  status["new_source"]=made.at("source_id");
  status["mapping"]=Json::array();
  for (const auto& id:selected) {
    LOOM_TRY_ASSIGN(auto prior,db.get_node(id.get<std::string>()));
    Json candidates=Json::array();
    for (const auto& next:made.at("occurrence_nodes")) {
      LOOM_TRY_ASSIGN(auto n,db.get_node(next.get<std::string>()));
      if (!n || !prior) return invalid();
      // Equal text alone never establishes occurrence identity. Preserve the
      // same coordinate/role, or leave an explicit user decision unresolved.
      if (n->content==prior->content && n->metadata.at("byte_range")==prior->metadata.at("byte_range") &&
          n->metadata.at("unit").at("kind")==prior->metadata.at("unit").at("kind") &&
          n->metadata.at("unit").value("attrs",Json::object())==prior->metadata.at("unit").value("attrs",Json::object()))
        candidates.push_back(next);
    }
    status["mapping"].push_back(Json{{"old_node",id},{"candidates",candidates},
        {"status",candidates.size()==1?"exact_coordinate_proposal":"decision_required"}});
  }
  status["activation"]="explicit_selector_update_required";
  status["rollback"]="old_projection_retained_restore_precopy_for_old_runtime_if_replay_refused";
  const auto receipt=Sha256::hex(json::canonical(status));
  const auto node_id="n_projection_upgrade_"+receipt;
  LOOM_TRY_ASSIGN(auto existing,db.get_node(node_id));
  if (!existing) {
    NodeOptions options;options.node_id=node_id;options.metadata=status;
    LOOM_TRY(db.create_node(node_id,"projection_upgrade",options));
    LOOM_TRY(db.create_link(node_id,old->id,"source"));
    LOOM_TRY(db.create_link(node_id,made.at("projection_node").get<std::string>(),"version"));
  } else if (json::canonical(existing->metadata)!=json::canonical(status)) return invalid();
  LOOM_TRY(txn.commit());
  status["upgrade_node"]=node_id;return status;
} catch (const Json::exception&) { return invalid(); }
}
