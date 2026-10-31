#include "import/image_representation.h"
#include "loom/model.h"
#include "loom/sqlite.h"
#include "loom/util/sha256.h"

namespace loom {
Result<Json> store_image_representation(Database& db, BlobStore& blobs, ProvenanceStore& prov,
                                         const std::string& source_id, const OcrResult& ocr, const Json& preset) {
  LOOM_TRY_ASSIGN(auto original, prov.get_source(source_id));
  if (!original) return Error(Errc::NotFound, "ocr_original_source_missing");
  const Json& doc = ocr.representation;
  if (json::get_string(doc, "source_sha256") != original->blob_hash)
    return Error(Errc::Conflict, "ocr_original_hash_mismatch");
  LOOM_TRY(blobs.verify(original->blob_hash));
  LOOM_TRY_ASSIGN(auto captured, blobs.put(doc.dump(), "application/json"));
  const std::string rep_id = "n_ocr_" + captured.hash;
  const std::string original_id = "n_image_" + original->blob_hash;
  auto lock = db.lock();
  sql::Txn txn(db.conn());
  LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto prior, db.get_node(rep_id));
  if (prior) {
    LOOM_TRY(txn.commit());
    return prior->metadata;
  }
  SourceRecord derived;
  derived.id = "src_ocr_" + captured.hash;
  derived.kind = preset.at("source_kinds").at("representation").get<std::string>();
  derived.blob_hash = captured.hash;
  derived.size = captured.size;
  derived.mime = "application/json";
  derived.format = "loom.ocr_regions/1";
  derived.parser = "tesseract_tsv";
  derived.parser_version = "1";
  derived.metadata = Json{{"original_source", source_id}, {"original_sha256", original->blob_hash},
                          {"method", doc.at("method")}, {"evidence_class", doc.value("evidence_class", "absent")}};
  LOOM_TRY_ASSIGN(auto derived_source, prov.add_source(std::move(derived)));
  const auto& kinds = preset.at("node_kinds");
  const auto& relations = preset.at("relations");
  NodeOptions root;
  root.node_id = original_id;
  root.metadata = Json{{"source", original->to_json()}};
  LOOM_TRY(db.create_node(original->title, kinds.at("source").get<std::string>(), root));
  Json projection{{"schema", "loom.image_representation/1"}, {"source_id", source_id},
                  {"source_sha256", original->blob_hash}, {"source_node", original_id},
                  {"representation_source", derived_source}, {"representation_blob", captured.hash},
                  {"representation_node", rep_id}, {"status", doc.at("status")},
                  {"reason", doc.at("reason")}, {"method", doc.at("method")}, {"frames", doc.at("frames")},
                  {"evidence_class", doc.value("evidence_class", "absent")}, {"confidence", nullptr}, {"region_nodes", Json::array()}};
  int ordinal = 0;
  for (const auto& region : doc.at("regions")) {
    model::Unit unit;
    unit.source = derived_source;
    unit.kind = kinds.at("region").get<std::string>();
    unit.locator.source = derived_source;
    unit.locator.json_pointer = "/regions/" + std::to_string(ordinal);
    unit.id = model::Unit::make_id(unit.source, unit.locator);
    unit.lang = ocr.language.value_or("");
    unit.attrs = Json{{"original_source", source_id}, {"original_sha256", original->blob_hash},
                       {"spatial_region", region}, {"frames", doc.at("frames")}, {"method", doc.at("method")}};
    model::Observation obs;
    obs.unit = unit.id;
    obs.kind = model::ObservationKind::Field;
    obs.text = region.at("text").get<std::string>();
    obs.locator = unit.locator;
    obs.locator.json_pointer += "/text";
    obs.id = model::Observation::make_id(unit.id, obs.locator, obs.text);
    obs.ordinal = ordinal++;
    obs.lang = unit.lang;
    // This observes an OCR output. It does not promote OCR to observed image text.
    obs.attrs = Json{{"evidence_class", "inferred"}, {"confidence", nullptr},
                     {"method", doc.at("method")}, {"original", unit.attrs}};
    NodeOptions node;
    const std::string id = "n_" + unit.id;
    node.node_id = id;
    node.content = obs.text;
    node.metadata = Json{{"unit", unit.to_json()}, {"observation", obs.to_json()}};
    LOOM_TRY(db.create_node(region.at("id").get<std::string>(), unit.kind, node));
    // Links are inserted after the representation node exists (below).
    projection["region_nodes"].push_back(id);
    ProvenanceRecord located;
    located.subject_id = id;
    located.subject_kind = "node";
    located.source_id = derived_source;
    located.locator = obs.locator.to_json();
    located.transform = "ocr.output@1"; // exact extraction from immutable output, not OCR accuracy
    LOOM_TRY(prov.add(std::move(located)));
  }
  NodeOptions rep;
  rep.node_id = rep_id;
  rep.content = ocr.text;
  rep.metadata = projection;
  LOOM_TRY(db.create_node(captured.hash, kinds.at("representation").get<std::string>(), rep));
  LOOM_TRY(db.create_link(rep_id, original_id, relations.at("representation").get<std::string>()));
  for (const auto& id : projection["region_nodes"])
    LOOM_TRY(db.create_link(id.get<std::string>(), rep_id, relations.at("region").get<std::string>()));
  LOOM_TRY(txn.commit());
  return projection;
}
}
