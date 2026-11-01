#include "native.h"
#include "contract.h"
#include "loom/model.h"
#include "loom/util/sha256.h"
#include <limits>
#include <stdexcept>

namespace loom::context::image_region {
namespace {
std::int64_t integer(const Json& j) {
  if (!j.is_number_integer() || (j.is_number_unsigned() &&
      j.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))
    throw std::invalid_argument("ocr_region_integer");
  return j.get<std::int64_t>();
}
std::array<std::int64_t, 2> pair(const Json& j) {
  if (!j.is_array() || j.size() != 2) throw std::invalid_argument("ocr_region_dimension");
  return {integer(j.at(0)), integer(j.at(1))};
}
std::array<std::string, 2> strings(const Json& j) {
  if (!j.is_array() || j.size() != 2) throw std::invalid_argument("ocr_region_axes");
  return {j.at(0).get<std::string>(), j.at(1).get<std::string>()};
}
bool only_pointer(const model::Locator& loc) {
  return loc.member.empty() && !loc.byte_start && !loc.byte_len &&
      !loc.time_start && !loc.time_end && !loc.line;
}
bool equal(const Json& a, const Json& b) { return json::canonical(a) == json::canonical(b); }
Error conflict(const char* reason) { return Error(Errc::Conflict, reason); }
}

bool is_region(const Node& node) {
  const auto* unit = json::find(node.metadata, "unit");
  if (!unit) return false;
  const auto* attrs = json::find(*unit, "attrs");
  if (attrs && attrs->contains("spatial_region")) return true;
  const auto* loc = json::find(*unit, "locator");
  return loc && json::get_string(*loc, "json_pointer").rfind("/regions/", 0) == 0;
}

Result<Json> candidate(const Node& node, const Access& access,
    std::size_t detail_chars, std::size_t relation_hops) try {
  if (!access.source || !access.read_blob || !access.verify_blob)
    return Error(Errc::Unavailable, "ocr_region_source_access_unavailable");
  if (!is_region(node)) return Error(Errc::InvalidArgument, "ocr_region_not_selected");
  const auto& metadata = node.metadata;
  LOOM_TRY_ASSIGN(auto unit, model::Unit::from_json(metadata.at("unit")));
  LOOM_TRY_ASSIGN(auto obs, model::Observation::from_json(metadata.at("observation")));
  const auto ordinal = integer(metadata.at("observation").at("ordinal"));
  if (ordinal != obs.ordinal || !only_pointer(unit.locator) || !only_pointer(obs.locator) ||
      unit.id != model::Unit::make_id(unit.source, unit.locator) || node.id != "n_" + unit.id ||
      obs.unit != unit.id || obs.id != model::Observation::make_id(unit.id, obs.locator, obs.text) ||
      obs.kind != model::ObservationKind::Field || node.kind != unit.kind)
    return conflict("ocr_region_identity_mismatch");
  const auto original_id = unit.attrs.at("original_source").get<std::string>();
  LOOM_TRY_ASSIGN(auto rep, access.source(unit.source));
  LOOM_TRY_ASSIGN(auto original, access.source(original_id));
  if (!rep || !original) return Error(Errc::NotFound, "ocr_region_source_missing");
  if (rep->id != unit.source || original->id != original_id ||
      rep->format != "loom.ocr_regions/1" || rep->mime != "application/json" ||
      !BlobStore::is_valid_hash(rep->blob_hash) || !BlobStore::is_valid_hash(original->blob_hash) ||
      rep->id != "src_ocr_" + rep->blob_hash ||
      rep->metadata.at("original_source") != original->id ||
      rep->metadata.at("original_sha256") != original->blob_hash)
    return conflict("ocr_region_source_mismatch");
  // Check access to BOTH sources before any blob content is read. Preserve
  // BlobStore placement, corruption and I/O errors through the injected access.
  LOOM_TRY(access.verify_blob(original->blob_hash));
  LOOM_TRY_ASSIGN(auto bytes, access.read_blob(rep->blob_hash));
  if (Sha256::hex(bytes) != rep->blob_hash || rep->size < 0 ||
      static_cast<std::uint64_t>(rep->size) != bytes.size())
    return conflict("ocr_region_representation_integrity");
  LOOM_TRY_ASSIGN(auto doc, json::parse(bytes));
  const auto& method = doc.at("method");
  if (!method.is_object() || json::get_string(method, "id").empty() ||
      !equal(method, unit.attrs.at("method")) || !equal(method, obs.attrs.at("method")) ||
      !equal(method, rep->metadata.at("method")) ||
      !equal(obs.attrs.at("original"), unit.attrs) ||
      !equal(unit.attrs.at("frames"), doc.at("frames")) ||
      rep->metadata.at("evidence_class") != "inferred")
    return conflict("ocr_region_derivation_mismatch");
  Document document;
  document.schema = doc.at("schema").get<std::string>();
  document.status = doc.at("status").get<std::string>();
  document.original_hash = doc.at("source_sha256").get<std::string>();
  document.evidence = doc.at("evidence_class").get<std::string>();
  document.confidence_is_null = doc.at("confidence").is_null();
  if (!doc.at("regions").is_array() || !doc.at("frames").is_array())
    return conflict("ocr_region_document_shape");
  for (const auto& r : doc.at("regions"))
    document.regions.push_back(Region{r.at("id").get<std::string>(), r.at("frame").get<std::string>(),
        r.at("text").get<std::string>(), pair(r.at("origin")), pair(r.at("size"))});
  for (const auto& f : doc.at("frames"))
    document.frames.push_back(Frame{f.at("id").get<std::string>(), pair(f.at("origin")),
        pair(f.at("extent")), strings(f.at("axes")), strings(f.at("units")), f.at("orientation").get<std::string>()});
  Selection selection{node.content, unit.source, unit.locator.source, unit.locator.json_pointer,
      obs.locator.source, obs.locator.json_pointer, obs.text, ordinal,
      original_id, unit.attrs.at("original_sha256").get<std::string>(),
      obs.attrs.at("evidence_class").get<std::string>(), obs.attrs.at("confidence").is_null()};
  LOOM_TRY_ASSIGN(auto validated, validate(selection,
      Source{rep->id, rep->blob_hash, rep->size}, Source{original->id, original->blob_hash, original->size},
      document, bytes, detail_chars));
  const auto& region = doc.at("regions").at(static_cast<std::size_t>(ordinal));
  if (!equal(unit.attrs.at("spatial_region"), region)) return conflict("ocr_region_spatial_mismatch");
  const auto& frame = validated.frame;
  Json provenance{{"origin", "derived"}, {"source_kind", "ocr_output"},
      {"observation_scope", "stored_ocr_output"}, {"evidence", "inferred"}, {"evidence_class", "inferred"},
      {"confidence", nullptr}, {"truth_status", "not_assessed"}, {"method", method},
      {"source_ref", rep->id}, {"representation_blob", rep->blob_hash},
      {"original_source", original->id}, {"original_sha256", original->blob_hash},
      {"unit_ref", unit.id}, {"observation_ref", obs.id}, {"locator", obs.locator.to_json()},
      {"region_id", validated.region.id},
      {"frame", Json{{"id", frame.id}, {"origin", frame.origin}, {"extent", frame.extent},
          {"axes", frame.axes}, {"units", frame.units}, {"orientation", frame.orientation}}},
      {"bounds", Json{{"origin", validated.region.origin}, {"size", validated.region.size}}},
      {"text_sha256", Sha256::hex(validated.region.text)},
      {"byte_span", Json::array({0, validated.text.size()})},
      {"codepoint_span", Json::array({0, validated.codepoints})}, {"span_coordinate", "ocr_region_utf8_bytes"}};
  // Serialized contract fields make the evidence visible in the prompt, too.
  // No confidence is synthesized from OCR engine_score, even when present.
  const Json heading{{"source_kind", "ocr_output"}, {"evidence", "inferred"}, {"confidence", nullptr}};
  return Json{{"ref_kind", "node"}, {"ref", node.id}, {"band", "goal"},
      {"text", validated.text}, {"source_text", validated.region.text}, {"source_heading", heading.dump()},
      {"evidence", "inferred"}, {"confidence", nullptr},
      {"resolution", validated.text == validated.region.text ? "raw" : "raw_excerpt"},
      {"relation_hops", relation_hops}, {"provenance", provenance}};
} catch (const Json::exception&) {
  return Error(Errc::Parse, "ocr_region_invalid_metadata");
} catch (const std::invalid_argument&) {
  return Error(Errc::Parse, "ocr_region_invalid_coordinates");
}

Result<Json> candidate(Database& db, BlobStore& blobs, const Node& node,
    std::size_t detail_chars, std::size_t relation_hops) {
  ProvenanceStore provenance(db);
  const Access access{
      [&](std::string_view id) { return provenance.get_source(id); },
      [&](std::string_view hash) { return blobs.read(hash); },
      [&](std::string_view hash) { return blobs.verify(hash); }};
  return candidate(node, access, detail_chars, relation_hops);
}
} // namespace loom::context::image_region
