#include "loom/text_source_parts.h"

#include <algorithm>
#include <limits>
#include <optional>

#include "docx_projection.h"
#include "chat/docx_basic/source.h"

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
  std::optional<docx::Prepared> document;
  std::string bytes;
  if (format == "docx") {
    LOOM_TRY_ASSIGN(auto prepared, docx::prepare(rt, hash));
    document = std::move(prepared);
    bytes = document->extraction.at("text").get<std::string>();
    hash = Sha256::hex(bytes);  // ranges below address this derived blob only
  } else {
    LOOM_TRY(rt.blobs().verify(hash));
    LOOM_TRY_ASSIGN(bytes, rt.blobs().read(hash));
  }
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
  Json method{{"id", "loom.source_parts"}, {"version", 1}, {"format", format},
                    {"level", opt.level}, {"language", seg.language()},
                    {"text_pack_sha256", Sha256::hex(text_rules)},
                    {"text_pack_blob", Sha256::hex(text_rules)},
                    {"kb_pack_blob", Sha256::hex(adapter_rules)},
                    {"kb_pack_sha256", kb->hash()}};
  if (document) method["derivation"] = document->derivation;
  const auto method_hash = Sha256::hex(json::canonical(method));
  Json identity = Json::array({key, hash});
  if (document) identity.push_back(document->derivation);
  const auto source_id = "src_text_" + Sha256::hex(json::canonical(identity));
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
    if (document && !parent->metadata.contains("derivation"))
      return Error(Errc::InvalidArgument, "docx_previous_source_mismatch");
    if (old && json::get_string(old->metadata, "previous_source_id") != previous)
      return Error(Errc::Conflict, "text_source_previous_conflict");
  }
  if (old && document && old->metadata.value("derivation", Json()) != document->derivation)
    return Error(Errc::Conflict, "docx_source_derivation_mismatch");
  if (document) {
    LOOM_TRY_ASSIGN(auto original, rt.provenance().get_source(document->derivation.at("original_source_id").get<std::string>()));
    if (original && (original->blob_hash != document->derivation.at("original_sha256").get<std::string>() ||
        original->size != document->derivation.at("original_size") || original->format != "docx"))
      return Error(Errc::Conflict, "docx_source_mismatch");
    if (old && !original) return Error(Errc::Conflict, "docx_source_mismatch");
  }
  LOOM_TRY_ASSIGN(auto cached, db.get_node(projection_node));
  if (cached) {
    // A cached projection is an idempotent receipt, not a subgraph integrity
    // audit or repair operation. Reads validate the selected occurrence.
    // DOCX must still have its distinct replay artifacts, never a stale source.
    if (document) {
      LOOM_TRY(rt.blobs().verify(hash));
      LOOM_TRY(rt.blobs().verify(document->derivation.at("extraction_blob").get<std::string>()));
      LOOM_TRY(rt.blobs().verify(document->derivation.at("transform").at("reader_script_blob").get<std::string>()));
    }
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
  if (document) {
    LOOM_TRY(rt.blobs().put(bytes, source.mime));
    LOOM_TRY(rt.blobs().put(json::canonical(document->extraction), "application/json"));
    LOOM_TRY(rt.blobs().put(document->script, "text/x-python"));
    source.metadata["derivation"] = document->derivation;
    source.metadata["coordinate"] = "derived_utf8_bytes";
    source.metadata["warnings"] = document->extraction.at("warnings");
    LOOM_TRY_ASSIGN(auto original_id, chat::record_docx_source(db,
        "blob:" + document->derivation.at("original_sha256").get<std::string>(),
        name, document->extraction, document->reader));
    if (original_id != document->derivation.at("original_source_id").get<std::string>())
      return Error(Errc::Conflict, "docx_source_mismatch");
  }
  if (!old) { LOOM_TRY(rt.provenance().add_source(source)); }
  else source = *old;

  // Later views can choose another parser or display name without rewriting
  // the first source descriptor. Their method/projection identity is separate.
  auto content = extract::text_unit(source.title, bytes, "", source_id);
  content.unit.kind = source.metadata.at("unit_kind").get<std::string>();
  // Language belongs to a method projection, not to the immutable source unit.
  content.unit.lang.clear();
  // Ranges are in this source blob: original UTF-8 or the DOCX derived UTF-8.
  content.unit.locator.member.clear();
  content.unit.locator.byte_start = 0;
  content.unit.locator.byte_len = source.size;
  content.unit.id = model::Unit::make_id(source_id, content.unit.locator);
  model::ArtifactType type;
  type.header.id = format;
  type.parse = input.at("parse");
  extract::Extractor extractor(kb);
  LOOM_TRY_ASSIGN(auto observations, extractor.segment(type, content));
  if (document) {
    // Reader values are observations of a derived representation. The OOXML
    // body-child locator is separate; it is never an XML/ZIP byte offset or page.
    observations.clear();
    if (document->extraction.at("blocks").size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
      return Error(Errc::InvalidArgument, "docx_observation_ordinal_overflow");
    for (std::size_t i = 0; i < document->extraction.at("blocks").size(); ++i) {
      const auto& block = document->extraction.at("blocks")[i];
      const auto& range = document->extraction.at("projection").at("ranges")[i].at("text_range");
      model::Observation observation;
      observation.unit = content.unit.id;
      observation.locator.source = source_id;
      observation.locator.byte_start = range[0].get<std::int64_t>();
      observation.locator.byte_len = range[1].get<std::int64_t>() - *observation.locator.byte_start;
      observation.text = block.at("text").get<std::string>();
      LOOM_TRY_ASSIGN(observation.kind, model::parse<model::ObservationKind>(
          input.at("block_kinds").at(block.at("kind").get<std::string>()).at("observation").get<std::string>()));
      observation.ordinal = static_cast<int>(i);
      observation.lang = seg.language();
      observation.artifact_type = format;
      observation.attrs = Json{{"evidence_class", "derived"}, {"recognized_kind", block.at("kind")},
          {"original_source_id", document->derivation.at("original_source_id")},
          {"original_locator", block.at("locator")}, {"coordinate", "derived_utf8_bytes"},
          {"transform_sha256", document->derivation.at("transform_sha256")}};
      observation.id = model::Observation::make_id(observation.unit, observation.locator, observation.text);
      observations.push_back(std::move(observation));
    }
  }
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
  auto chain = [&]() -> Result<genesis::Chain> {
    if (!document || opt.level != "recognized_block") return genesis::compute_chain(seg, {{ref, bytes}}, opt);
    genesis::Chain result;
    result.level = opt.level;
    result.language = seg.language();
    result.advisories = seg.advisories();
    genesis::VersionFragments version;
    version.source = ref;
    for (const auto& range : document->extraction.at("projection").at("ranges")) {
      const auto begin = range.at("byte_range")[0].get<std::size_t>();
      const auto end = range.at("byte_range")[1].get<std::size_t>();
      const auto value = bytes.substr(begin, end - begin);
      version.fragments.push_back({seg.fingerprint(value), version.fragments.size(), begin, end, value});
    }
    // Honor the existing genesis event policy for this adapter-supplied cut.
    // These are initial occurrences only, never inferred cross-version edits.
    const auto event_kind = opt.event_kind("insert");
    if (opt.record_initial_additions && !event_kind.empty()) {
      for (const auto& part : version.fragments) {
        genesis::Event event;
        event.kind = event_kind;
        event.op_kind = "insert";
        event.to = ref;
        event.b_index = part.index;
        event.fragment = part.fingerprint;
        event.text_after = part.text;
        // Existing genesis event-key serialization (absent a-side is "-").
        event.key = event_kind + "||" + ref.id + "|-|" + std::to_string(part.index) +
                    "@" + result.level + "/" + result.language;
        result.events.push_back(std::move(event));
      }
    }
    result.versions.push_back(std::move(version));
    return result;
  }();
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
  if (document) {
    const auto original_id = document->derivation.at("original_source_id").get<std::string>();
    model::Unit original;
    original.source = original_id;
    original.kind = recipe.at("unit_kind").get<std::string>();
    original.locator.source = original_id;
    original.bytes = document->derivation.at("original_size").get<std::int64_t>();
    original.id = model::Unit::make_id(original_id, original.locator);
    LOOM_TRY_ASSIGN(auto record, rt.provenance().get_source(original_id));
    if (!record || record->blob_hash != document->derivation.at("original_sha256").get<std::string>() || record->size != original.bytes)
      return Error(Errc::Conflict, "docx_source_mismatch");
    LOOM_TRY(put_node(db, "n_" + original_id, record->title, kinds.at("source").get<std::string>(), "",
        Json{{"source_id", original_id}, {"sha256", record->blob_hash}, {"unit", original.to_json()}, {"format", "docx"}}));
    LOOM_TRY(db.create_link(source_node, "n_" + original_id, edges.at("source").get<std::string>(), 1.0, document->derivation));
    LOOM_TRY(db.create_link(source_node, method_node, edges.at("method").get<std::string>()));
    ProvenanceRecord derived;
    derived.id = "pv_" + Sha256::hex(source_id);
    derived.subject_id = source_id;
    derived.subject_kind = "source";
    derived.source_id = original_id;
    derived.locator = original.locator.to_json();
    derived.transform = document->derivation.at("transform_sha256").get<std::string>();
    LOOM_TRY_ASSIGN(auto prior, rt.provenance().for_subject(source_id));
    if (prior.empty()) LOOM_TRY(rt.provenance().add(derived));
    else if (prior.size() != 1 || prior.front().source_id != original_id || prior.front().transform != derived.transform)
      return Error(Errc::Conflict, "docx_source_provenance_mismatch");
  }
  if (!previous.empty()) LOOM_TRY(db.create_link(source_node, "n_" + previous, edges.at("version").get<std::string>()));
  Json result{{"schema", "loom.source_text_parts/1"}, {"source_id", source_id}, {"source_node", source_node},
              {"source_sha256", hash}, {"unit_id", content.unit.id}, {"version_node", version->id},
              {"projection_node", projection_node}, {"method", method}, {"method_sha256", method_hash},
              {"method_node", method_node},
              {"status", "partial"}, {"reason", input.at("reason")},
              {"coordinate", "source_utf8_bytes"}, {"occurrence_nodes", Json::array()},
              {"observations", observed}, {"advisories", chain->advisories}};
  if (document) {
    result["coordinate"] = "derived_utf8_bytes";
    result["derivation"] = document->derivation;
    result["original_source_node"] = "n_" + document->derivation.at("original_source_id").get<std::string>();
    result["warnings"] = document->extraction.at("warnings");
  }
  std::size_t observation_index = 0;
  for (const auto& part : chain->versions.front().fragments) {
    if (part.begin > part.end || part.end > bytes.size()) return Error(Errc::Conflict, "text_source_range_invalid");
    const auto text = bytes.substr(part.begin, part.end - part.begin);
    model::Unit unit;
    unit.source = source_id;
    unit.kind = input.at("part_kind").get<std::string>();
    if (document && opt.level == "recognized_block") {
      const auto& block = document->extraction.at("blocks")[part.index];
      unit.kind = input.at("block_kinds").at(block.at("kind").get<std::string>()).at("unit").get<std::string>();
      // Preserve recognized block identity separately from its fingerprint.
      unit.attrs = Json{{"recognized_kind", block.at("kind")}, {"original_locator", block.at("locator")}};
    }
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
    if (document) {
      meta["coordinate"] = "derived_utf8_bytes";
      meta["derivation"] = document->derivation;
      meta["evidence_class"] = "derived";
    }
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
    if (document && opt.level == "recognized_block")
      meta["observation_refs"] = Json::array({observations[part.index].id});
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
  const bool derived = source->metadata.contains("derivation");
  if (derived) {
    const auto& d = source->metadata.at("derivation");
    const auto original_id = json::get_string(d, "original_source_id");
    LOOM_TRY_ASSIGN(auto original, store.get_source(original_id));
    if (json::get_string(d, "schema") != "loom.docx_source_derivation/1" ||
        !original || original->id != "src_docx_" + original->blob_hash ||
        original->blob_hash != json::get_string(d, "original_sha256") ||
        original->size != d.at("original_size") || original->format != "docx" ||
        source->blob_hash != json::get_string(d, "text_sha256") ||
        d != meta.at("derivation") || d != meta.at("method").at("derivation") ||
        Sha256::hex(json::canonical(d.at("transform"))) != json::get_string(d, "transform_sha256") ||
        Sha256::hex(json::canonical(meta.at("method"))) != json::get_string(meta, "method_sha256") ||
        source->id != "src_text_" + Sha256::hex(json::canonical(Json::array({
            source->metadata.at("source_key"), source->blob_hash, d}))))
      return Error(Errc::Conflict, "docx_source_derivation_mismatch");
  } else if (meta.contains("derivation")) return Error(Errc::Conflict, "docx_source_derivation_mismatch");
  LOOM_TRY_ASSIGN(auto unit, model::Unit::from_json(meta.at("unit")));
  if (derived && (unit.id != model::Unit::make_id(source->id, unit.locator) ||
      node.id != "n_text_occ_" + Sha256::hex(unit.id + ":" + json::get_string(meta, "method_sha256"))))
    return Error(Errc::Conflict, "docx_source_derivation_mismatch");
  const auto start = unit.locator.byte_start.value_or(-1), len = unit.locator.byte_len.value_or(-1);
  if (unit.source != source->id || unit.locator.source != source->id ||
      json::get_string(meta, "coordinate") != (derived ? "derived_utf8_bytes" : "source_utf8_bytes") ||
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
  const bool derived = source->metadata.contains("derivation");
  if (derived) {
    const auto& d = source->metadata.at("derivation");
    const auto original_hash = d.at("original_sha256").get<std::string>();
    const auto extraction_hash = d.at("extraction_blob").get<std::string>();
    LOOM_TRY(rt.blobs().verify(original_hash));
    LOOM_TRY(rt.blobs().verify(extraction_hash));
    LOOM_TRY_ASSIGN(auto raw, rt.blobs().read(extraction_hash));
    LOOM_TRY_ASSIGN(auto extraction, json::parse(raw));
    LOOM_TRY(docx::validate(extraction, original_hash, d.at("original_size").get<std::int64_t>()));
    if (extraction.at("text") != bytes)
      return Error(Errc::Conflict, "docx_projection_text_mismatch");
  }
  LOOM_TRY_ASSIGN(auto unit, model::Unit::from_json(meta.at("unit")));
  const auto start = unit.locator.byte_start.value_or(-1), len = unit.locator.byte_len.value_or(-1);
  if (unit.source != source->id || unit.locator.source != source->id ||
      json::get_string(meta, "coordinate") != (derived ? "derived_utf8_bytes" : "source_utf8_bytes"))
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
