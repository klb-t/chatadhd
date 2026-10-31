#include "loom/text_source_parts.h"

#include <algorithm>
#include <limits>

#include "loom/extract.h"
#include "loom/knowledge.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/text_genesis.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::text::sources {
namespace {
Result<std::string> required(const Json& j, std::string_view key) {
  const auto value = json::get_string(j, key);
  if (value.empty()) return Error(Errc::InvalidArgument, "text_source_missing: " + std::string(key));
  return value;
}

Status put_node(Database& db, const std::string& id, const std::string& label,
                const std::string& kind, const std::string& text, const Json& meta) {
  LOOM_TRY_ASSIGN(auto prior, db.get_node(id));
  if (prior) {
    if (prior->kind != kind || prior->content != text || prior->metadata != meta)
      return Error(Errc::Conflict, "text_source_node_conflict");
    return {};
  }
  NodeOptions options;
  options.node_id = id;
  options.content = text;
  options.metadata = meta;
  LOOM_TRY(db.create_node(label, kind, options));
  return {};
}
}  // namespace

Result<Json> materialize(Runtime& rt, const Json& request) try {
  LOOM_TRY_ASSIGN(auto hash, required(request, "hash"));
  LOOM_TRY_ASSIGN(auto key, required(request, "source_key"));
  LOOM_TRY_ASSIGN(auto name, required(request, "name"));
  LOOM_TRY_ASSIGN(auto format, required(request, "format"));
  if (!BlobStore::is_valid_hash(hash)) return Error(Errc::InvalidArgument, "text_source_invalid_hash");
  LOOM_TRY(rt.blobs().verify(hash));
  LOOM_TRY_ASSIGN(auto bytes, rt.blobs().read(hash));
  // Raw byte coordinates are valid only for byte-identical UTF-8. A decoder that
  // repairs bytes needs its own derived representation and coordinate system.
  if (utf8::repair(bytes) != bytes) return Error(Errc::Unsupported, "text_source_invalid_utf8");
  if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
    return Error(Errc::InvalidArgument, "text_source_size_overflow");
  Json pack = builtin_pack();
  if (const auto* overlay = json::find(request, "pack")) {
    if (!overlay->is_object()) return Error(Errc::InvalidArgument, "text_source_pack_invalid");
    pack = merge_pack(pack, *overlay);
  }
  const auto& recipe = pack.at("source_parts");
  if (!recipe.at("formats").contains(format)) return Error(Errc::Unsupported, "text_source_format_unavailable");
  const auto& input = recipe.at("formats").at(format);
  const auto language = json::get_string(request, "language", json::get_string(pack, "default_language"));
  LOOM_TRY_ASSIGN(auto requested_seg, Segmenter::from_pack(pack, language));
  // Normalize this new projection to the actual rules without changing legacy
  // Segmenter/genesis language identities used by existing stored graphs.
  LOOM_TRY_ASSIGN(auto seg, Segmenter::from_pack(pack, requested_seg.effective_language()));
  LOOM_TRY_ASSIGN(auto opt, genesis::Options::from_pack(pack));
  opt.level = json::get_string(request, "level", json::get_string(input, "level"));
  opt.language = seg.language();
  // Pin every rule used by this projection. Changing a method creates another
  // projection; it cannot reuse a receipt cut with different normalization.
  LOOM_TRY_ASSIGN(auto kb, rt.knowledge().pack());
  Json kb_documents = Json::object();
  for (const auto& file : kb->files()) kb_documents[file] = kb->file(file);
  const auto text_rules = json::canonical(pack);
  const auto adapter_rules = json::canonical(kb_documents);
  const Json method{{"id", "loom.source_parts"}, {"version", 1}, {"format", format},
                    {"level", opt.level}, {"language", seg.language()},
                    {"text_pack_sha256", Sha256::hex(text_rules)},
                    {"text_pack_blob", Sha256::hex(text_rules)},
                    {"kb_pack_blob", Sha256::hex(adapter_rules)},
                    {"kb_pack_sha256", kb->hash()}};
  const auto method_hash = Sha256::hex(json::canonical(method));
  const auto source_id = "src_text_" + Sha256::hex(json::canonical(Json::array({key, hash})));
  const auto source_node = "n_" + source_id;
  const auto version_id = source_id + ":" + method_hash;
  const auto projection_node = "n_text_projection_" + Sha256::hex(version_id);
  const auto method_node = "n_text_method_" + method_hash;
  const auto previous = json::get_string(request, "previous_source_id");

  auto& db = rt.db();
  auto lock = db.lock();
  sql::Txn txn(db.conn());
  LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto old, rt.provenance().get_source(source_id));
  if (old && (old->blob_hash != hash || json::get_string(old->metadata, "source_key") != key))
    return Error(Errc::Conflict, "text_source_descriptor_conflict");
  if (!previous.empty()) {
    LOOM_TRY_ASSIGN(auto parent, rt.provenance().get_source(previous));
    if (!parent || previous == source_id || json::get_string(parent->metadata, "source_key") != key)
      return Error(Errc::InvalidArgument, "text_source_previous_mismatch");
    if (old && json::get_string(old->metadata, "previous_source_id") != previous)
      return Error(Errc::Conflict, "text_source_previous_conflict");
  }
  LOOM_TRY_ASSIGN(auto cached, db.get_node(projection_node));
  if (cached) {
    // A cached projection is an idempotent receipt, not a subgraph integrity
    // audit or repair operation. Reads validate the selected occurrence.
    if (!old || json::get_string(cached->metadata, "source_id") != source_id ||
        json::get_string(cached->metadata, "method_sha256") != method_hash)
      return Error(Errc::Conflict, "text_source_projection_conflict");
    LOOM_TRY(txn.commit());
    return cached->metadata;
  }

  // Keep the actual rules once per hash, not merely a hash of an overlay that
  // can disappear. No per-occurrence copies of packs; originals stay separate.
  LOOM_TRY(rt.blobs().put(text_rules, "application/json"));
  LOOM_TRY(rt.blobs().put(adapter_rules, "application/json"));

  SourceRecord source;
  source.id = source_id;
  source.kind = recipe.at("source_kind").get<std::string>();
  source.uri = key;
  source.title = name;
  source.blob_hash = hash;
  source.size = static_cast<std::int64_t>(bytes.size());
  source.mime = input.at("mime").get<std::string>();
  source.format = format;
  source.parser = "loom.source_parts";
  source.parser_version = "1";
  source.metadata = Json{{"source_key", key}, {"previous_source_id", previous},
                         {"unit_kind", recipe.at("unit_kind")}, {"node_kind", recipe.at("nodes").at("source")}};
  if (!old) { LOOM_TRY(rt.provenance().add_source(source)); }
  else source = *old;

  // Later views can choose another parser or display name without rewriting
  // the first source descriptor. Their method/projection identity is separate.
  auto content = extract::text_unit(source.title, bytes, "", source_id);
  content.unit.kind = source.metadata.at("unit_kind").get<std::string>();
  // Language belongs to a method projection, not to the immutable source unit.
  content.unit.lang.clear();
  // Plain file: ranges are in original blob bytes, not in an archive member.
  content.unit.locator.member.clear();
  content.unit.locator.byte_start = 0;
  content.unit.locator.byte_len = source.size;
  content.unit.id = model::Unit::make_id(source_id, content.unit.locator);
  model::ArtifactType type;
  type.header.id = format;
  type.parse = input.at("parse");
  extract::Extractor extractor(kb);
  LOOM_TRY_ASSIGN(auto observations, extractor.segment(type, content));
  std::stable_sort(observations.begin(), observations.end(), [](const auto& a, const auto& b) {
    return a.locator.byte_start < b.locator.byte_start;
  });
  Json observed = Json::array();
  for (const auto& observation : observations) {
    const auto start = observation.locator.byte_start.value_or(-1);
    const auto len = observation.locator.byte_len.value_or(-1);
    if (start < 0 || len < 0 || start > source.size || len > source.size - start ||
        bytes.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(len)) != observation.text)
      return Error(Errc::Conflict, "text_source_adapter_range_mismatch");
    observed.push_back(observation.to_json());
  }

  genesis::SourceRef ref;
  ref.id = version_id;
  ref.kind = source.kind;
  ref.group = key;
  auto chain = genesis::compute_chain(seg, {{ref, bytes}}, opt);
  if (!chain) return chain.error();
  // This is a cut of one immutable version. Cross-version source links below
  // record caller lineage without pretending to have aligned all old versions.
  LOOM_TRY(genesis::materialize(db, *chain, opt));
  LOOM_TRY_ASSIGN(auto version, db.find_node(version_id + "@" + chain->level + "/" + chain->language,
                                             opt.node_kind("version")));
  if (!version) return Error(Errc::Internal, "text_source_genesis_version_missing");
  const auto& kinds = recipe.at("nodes");
  const auto& edges = recipe.at("edges");
  LOOM_TRY(put_node(db, method_node, method_hash, kinds.at("method").get<std::string>(), "", method));
  LOOM_TRY(put_node(db, source_node, source.title, source.metadata.at("node_kind").get<std::string>(), "",
                    Json{{"source_id", source_id}, {"source_key", key}, {"sha256", hash},
                         {"unit", content.unit.to_json()}, {"format", source.format}}));
  LOOM_TRY(db.create_link(version->id, source_node, edges.at("source").get<std::string>(), 1.0, Json{{"method", method}}));
  LOOM_TRY(db.create_link(version->id, method_node, edges.at("method").get<std::string>()));
  if (!previous.empty()) LOOM_TRY(db.create_link(source_node, "n_" + previous, edges.at("version").get<std::string>()));
  Json result{{"schema", "loom.source_text_parts/1"}, {"source_id", source_id}, {"source_node", source_node},
              {"source_sha256", hash}, {"unit_id", content.unit.id}, {"version_node", version->id},
              {"projection_node", projection_node}, {"method", method}, {"method_sha256", method_hash},
              {"method_node", method_node},
              {"status", "partial"}, {"reason", input.at("reason")},
              {"coordinate", "source_utf8_bytes"}, {"occurrence_nodes", Json::array()},
              {"observations", observed}, {"advisories", chain->advisories}};
  std::size_t observation_index = 0;
  for (const auto& part : chain->versions.front().fragments) {
    if (part.begin > part.end || part.end > bytes.size()) return Error(Errc::Conflict, "text_source_range_invalid");
    const auto text = bytes.substr(part.begin, part.end - part.begin);
    model::Unit unit;
    unit.source = source_id;
    unit.kind = input.at("part_kind").get<std::string>();
    unit.locator.source = source_id;
    unit.locator.byte_start = static_cast<std::int64_t>(part.begin);
    unit.locator.byte_len = static_cast<std::int64_t>(part.end - part.begin);
    unit.id = model::Unit::make_id(source_id, unit.locator);
    unit.bytes = *unit.locator.byte_len;
    unit.lang = seg.language();
    const auto id = "n_text_occ_" + Sha256::hex(unit.id + ":" + method_hash);
    Json meta{{"schema", kOccurrenceSchema}, {"source_id", source_id}, {"source_sha256", hash},
              {"source_node", source_node}, {"unit", unit.to_json()}, {"version_node", version->id},
              {"method", method}, {"method_sha256", method_hash}, {"index", part.index},
              {"context_heading", recipe.at("context_heading")},
              {"fingerprint", part.fingerprint}, {"text_sha256", Sha256::hex(text)},
              {"coordinate", "source_utf8_bytes"}, {"byte_range", Json::array({part.begin, part.end})},
              {"observation_refs", Json::array()}};
    while (observation_index < observations.size() &&
           *observations[observation_index].locator.byte_start + *observations[observation_index].locator.byte_len <= static_cast<std::int64_t>(part.begin))
      ++observation_index;
    for (std::size_t oi = observation_index; oi < observations.size(); ++oi) {
      const auto& observation = observations[oi];
      const auto start = *observation.locator.byte_start;
      const auto end = start + *observation.locator.byte_len;
      if (start >= static_cast<std::int64_t>(part.end)) break;
      if (end > static_cast<std::int64_t>(part.begin))
        meta["observation_refs"].push_back(observation.id);
    }
    LOOM_TRY(put_node(db, id, unit.id, unit.kind, text, meta));
    LOOM_TRY(db.create_link(id, version->id, edges.at("part").get<std::string>()));
    LOOM_TRY_ASSIGN(auto fragment, db.find_node(part.fingerprint + "@" + chain->level + "/" + chain->language,
                                               opt.node_kind("fragment")));
    if (!fragment) return Error(Errc::Internal, "text_source_genesis_fragment_missing");
    LOOM_TRY(db.create_link(id, fragment->id, edges.at("fingerprint").get<std::string>()));
    ProvenanceRecord located;
    located.id = "pv_" + Sha256::hex(id);
    located.subject_id = id;
    located.subject_kind = "node";
    located.source_id = source_id;
    located.locator = unit.locator.to_json();
    located.transform = "loom.source_parts@1:" + method_hash;
    LOOM_TRY_ASSIGN(auto recorded, rt.provenance().for_subject(id));
    bool exists = false;
    for (const auto& record : recorded) {
      if (record.id != located.id) continue;
      if (record.source_id != source_id || record.locator != located.locator || record.transform != located.transform)
        return Error(Errc::Conflict, "text_source_provenance_conflict");
      exists = true;
    }
    if (!exists) { LOOM_TRY(rt.provenance().add(located)); }
    result["occurrence_nodes"].push_back(id);
  }
  LOOM_TRY(put_node(db, projection_node, version_id, kinds.at("projection").get<std::string>(), "", result));
  LOOM_TRY(db.create_link(projection_node, version->id, edges.at("source").get<std::string>()));
  LOOM_TRY(txn.commit());
  return result;
} catch (const Json::exception& e) {
  return Error(Errc::InvalidArgument, std::string("text_source_invalid_data: ") + e.what());
}

Status validate_occurrence(Database& db, const Node& node) try {
  const auto& meta = node.metadata;
  if (json::get_string(meta, "schema") != kOccurrenceSchema)
    return Error(Errc::InvalidArgument, "text_source_occurrence_missing");
  ProvenanceStore store(db);
  LOOM_TRY_ASSIGN(auto source, store.get_source(json::get_string(meta, "source_id")));
  if (!source || source->blob_hash != json::get_string(meta, "source_sha256"))
    return Error(Errc::Conflict, "text_source_original_mismatch");
  LOOM_TRY_ASSIGN(auto unit, model::Unit::from_json(meta.at("unit")));
  const auto start = unit.locator.byte_start.value_or(-1), len = unit.locator.byte_len.value_or(-1);
  if (unit.source != source->id || unit.locator.source != source->id ||
      json::get_string(meta, "coordinate") != "source_utf8_bytes" ||
      start < 0 || len < 0 || start > source->size || len > source->size - start ||
      static_cast<std::uint64_t>(len) != node.content.size() ||
      meta.at("byte_range") != Json::array({start, start + len}) ||
      Sha256::hex(node.content) != json::get_string(meta, "text_sha256"))
    return Error(Errc::Conflict, "text_source_occurrence_mismatch");
  return {};
} catch (const Json::exception& e) {
  return Error(Errc::Conflict, std::string("text_source_invalid_record: ") + e.what());
}

Result<Json> read(Runtime& rt, const Json& request) try {
  LOOM_TRY_ASSIGN(auto id, required(request, "node_id"));
  LOOM_TRY_ASSIGN(auto node, rt.db().get_node(id));
  if (!node || json::get_string(node->metadata, "schema") != kOccurrenceSchema)
    return Error(Errc::NotFound, "text_source_occurrence_missing");
  LOOM_TRY(validate_occurrence(rt.db(), *node));
  const auto& meta = node->metadata;
  LOOM_TRY_ASSIGN(auto source, rt.provenance().get_source(json::get_string(meta, "source_id")));
  if (!source || source->blob_hash != json::get_string(meta, "source_sha256"))
    return Error(Errc::Conflict, "text_source_original_mismatch");
  LOOM_TRY(rt.blobs().verify(source->blob_hash));
  LOOM_TRY_ASSIGN(auto bytes, rt.blobs().read(source->blob_hash));
  LOOM_TRY_ASSIGN(auto unit, model::Unit::from_json(meta.at("unit")));
  const auto start = unit.locator.byte_start.value_or(-1), len = unit.locator.byte_len.value_or(-1);
  if (unit.source != source->id || unit.locator.source != source->id ||
      json::get_string(meta, "coordinate") != "source_utf8_bytes")
    return Error(Errc::Conflict, "text_source_locator_mismatch");
  if (start < 0 || len < 0 || static_cast<std::uint64_t>(start) > bytes.size() ||
      static_cast<std::uint64_t>(len) > bytes.size() - static_cast<std::size_t>(start))
    return Error(Errc::Conflict, "text_source_range_invalid");
  const auto text = bytes.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(len));
  if (meta.at("byte_range") != Json::array({start, start + len}))
    return Error(Errc::Conflict, "text_source_locator_mismatch");
  if (text != node->content || Sha256::hex(text) != json::get_string(meta, "text_sha256"))
    return Error(Errc::Conflict, "text_source_occurrence_mismatch");
  return Json{{"node_id", id}, {"text", text}, {"source", source->to_json()}, {"provenance", meta}};
} catch (const Json::exception& e) {
  return Error(Errc::Conflict, std::string("text_source_invalid_record: ") + e.what());
}
}  // namespace loom::text::sources
