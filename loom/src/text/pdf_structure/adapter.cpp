#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "adapter.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>
#include <memory>
#include "chat/document_text.h"
#include "chat/docx_basic/adapter.h"
#include "chat/docx_basic/package.h"
#include "loom/db.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::text::sources::pdf {
namespace {
Error invalid() { return Error(Errc::Conflict, "pdf_structure_invalid_receipt"); }
Result<std::int64_t> quantity(const Json& value) {
  if (!value.is_number_integer() || value < 0 || value > (std::numeric_limits<std::int64_t>::max)())
    return invalid();
  return value.get<std::int64_t>();
}
Result<Json> reader_for() {
  LOOM_TRY_ASSIGN(auto root, chat::packaged_reader_directory("pdf_structure"));
  LOOM_TRY_ASSIGN(auto raw, fsutil::read_file(root / "runtime.json"));
  LOOM_TRY_ASSIGN(auto manifest, json::parse(raw));
  if (json::get_string(manifest, "schema") != "loom.pdf_structure.runtime/1") return invalid();
  for (const auto* file : {"worker.py", "projection.py", "profile.json"}) {
    LOOM_TRY_ASSIGN(auto bytes, fsutil::read_file(root / file));
    if (Sha256::hex(bytes) != manifest.at("files").at(file).get<std::string>())
      return Error(Errc::Conflict, "pdf_structure_package_changed");
  }
  const auto python = manifest.at("python").get<std::string>();
  if (!std::filesystem::path(python).is_absolute() || python.find('\0') != std::string::npos) return invalid();
  LOOM_TRY_ASSIGN(auto binary, fsutil::read_file(python));
  if (Sha256::hex(binary) != manifest.at("python_sha256").get<std::string>())
    return Error(Errc::Conflict, "pdf_structure_interpreter_changed");
  LOOM_TRY_ASSIGN(auto profile_raw, fsutil::read_file(root / "profile.json"));
  LOOM_TRY_ASSIGN(auto reader, json::parse(profile_raw));
  auto& config = reader["structure"];
  for (const auto& value : {config.at("timeout_ms"), config.at("poll_ms"), config.at("max_output_bytes"),
      config.at("max_original_bytes"), config.at("limits").at("max_input_bytes"),
      config.at("limits").at("cpu_seconds"), config.at("limits").at("memory_bytes")}) {
    LOOM_TRY_ASSIGN(auto n, quantity(value));
    if (n == 0) return Error(Errc::InvalidArgument, "pdf_structure_positive_limit_required");
  }
  config["python"] = python;
  config["script"] = (root / "worker.py").string();
  reader["host_package"] = manifest;
  return reader;
}
Result<Json> run_helper(const Json& input, const Json& config) {
  return chat::read_local_json_bytes(input.dump(), config, config.at("limits"));
}
Json input_for(const Json& e) {
  return Json{{"schema", "loom.pdf_structure.input/1"}, {"source", e.at("source")},
      {"reader", e.at("reader")}, {"options", e.at("options")},
      {"document", Json{{"text", e.at("text")}, {"pages", e.at("page_count")},
                        {"pages_without_text", e.at("pages_without_text")}}}};
}
}  // namespace

Status validate(const Json& e, const std::string& hash, std::int64_t size) try {
  if (json::get_string(e, "schema") != "loom.pdf_structure.result/1" ||
      json::get_string(e, "extractor") != "loom.pdf_structure/1" ||
      !BlobStore::is_valid_hash(hash) || size < 0 || e.at("source").at("sha256") != hash ||
      e.at("source").at("size") != size || e.at("partial") != true ||
      !e.at("blocks").is_array() || e.at("blocks").empty() || !e.at("pages").is_array()) return invalid();
  const auto text = e.at("text").get<std::string>();
  const auto& p = e.at("projection");
  if (utf8::repair(text) != text || json::get_string(p, "schema") != "loom.pdf_structure.projection/1" ||
      json::get_string(p, "coordinate") != "derived_utf8_bytes" || p.at("size") != text.size() ||
      p.at("sha256") != Sha256::hex(text) || p.at("ranges").size() != e.at("blocks").size()) return invalid();
  std::int64_t cursor = 0;
  for (std::size_t i = 0; i < e.at("blocks").size(); ++i) {
    const auto& range = p.at("ranges")[i];
    const auto& block = e.at("blocks")[i];
    if (range.at("byte_range").size() != 2 || range.at("text_range").size() != 2) return invalid();
    LOOM_TRY_ASSIGN(auto begin, quantity(range.at("byte_range")[0]));
    LOOM_TRY_ASSIGN(auto end, quantity(range.at("byte_range")[1]));
    LOOM_TRY_ASSIGN(auto start, quantity(range.at("text_range")[0]));
    LOOM_TRY_ASSIGN(auto stop, quantity(range.at("text_range")[1]));
    if (begin != cursor || begin > start || start > stop || stop > end ||
        static_cast<std::uint64_t>(end) > text.size() ||
        text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(stop - start)) != block.at("text").get<std::string>() ||
        block.at("structure_evidence") != "inferred" || block.at("quote_evidence") != "derived") return invalid();
    cursor = end;
  }
  if (static_cast<std::uint64_t>(cursor) != text.size()) return invalid();
  LOOM_TRY_ASSIGN(auto count, quantity(e.at("page_count")));
  if (!count || static_cast<std::uint64_t>(count) != e.at("pages").size()) return invalid();
  Json missing = Json::array();
  cursor = 0;
  for (std::size_t i = 0; i < e.at("pages").size(); ++i) {
    const auto& page = e.at("pages")[i];
    if (page.at("page") != i + 1 || page.at("byte_range").size() != 2) return invalid();
    LOOM_TRY_ASSIGN(auto begin, quantity(page.at("byte_range")[0]));
    LOOM_TRY_ASSIGN(auto end, quantity(page.at("byte_range")[1]));
    if (begin != cursor || end < begin || static_cast<std::uint64_t>(end) > text.size()) return invalid();
    const auto page_text = text.substr(static_cast<std::size_t>(begin), static_cast<std::size_t>(end - begin));
    if (page_text.find('\f') != std::string::npos) return invalid();
    const bool has_text = page_text.find_first_not_of(" \t\r\n\v") != std::string::npos;
    if (page.at("has_text") != has_text) return invalid();
    if (!has_text) missing.push_back(i + 1);
    cursor = end;
    if (i + 1 < e.at("pages").size()) {
      if (static_cast<std::uint64_t>(end) >= text.size() || text[static_cast<std::size_t>(end)] != '\f') return invalid();
      ++cursor;
    }
  }
  if (static_cast<std::uint64_t>(cursor) != text.size() || missing != e.at("pages_without_text") ||
      missing.size() == e.at("pages").size()) return invalid();
  std::vector<std::size_t> ordinals(e.at("pages").size(), 0), headings;
  for (std::size_t i = 0; i < e.at("blocks").size(); ++i) {
    const auto& block = e.at("blocks")[i];
    const auto& range = p.at("ranges")[i];
    const auto& loc = block.at("locator");
    LOOM_TRY_ASSIGN(auto page, quantity(loc.at("page")));
    if (!page || page > count) return invalid();
    const auto pi = static_cast<std::size_t>(page - 1);
    const auto page_begin = e.at("pages")[pi].at("byte_range")[0].get<std::int64_t>();
    const auto page_end = e.at("pages")[pi].at("byte_range")[1].get<std::int64_t>();
    const auto start = range.at("text_range")[0].get<std::int64_t>();
    const auto stop = range.at("text_range")[1].get<std::int64_t>();
    if (loc.at("block") != ordinals[pi]++ || start < page_begin || stop > page_end ||
        range.at("byte_range")[0] < page_begin || range.at("byte_range")[1] > page_end + (page < count ? 1 : 0) ||
        loc.at("page_text_range") != Json::array({start - page_begin, stop - page_begin})) return invalid();
    const auto kind = json::get_string(block, "kind");
    const auto quote = block.at("text").get<std::string>();
    if (kind == "heading") {
      if (quote.empty() || quote.find_first_of("\r\n") != std::string::npos) return invalid();
      headings.push_back(i);
    } else if (kind == "page_gap") {
      if (!quote.empty()) return invalid();
    } else if (kind != "paragraph" || quote.empty()) return invalid();
  }
  if (!e.at("chapters").is_array() || e.at("chapters").size() != headings.size()) return invalid();
  for (std::size_t i = 0; i < headings.size(); ++i) {
    const auto& chapter = e.at("chapters")[i];
    const auto start = headings[i];
    const auto stop = i + 1 < headings.size() ? headings[i + 1] : e.at("blocks").size();
    if (chapter.at("heading_block") != start || chapter.at("block_range") != Json::array({start, stop}) ||
        chapter.at("byte_range") != Json::array({p.at("ranges")[start].at("byte_range")[0], p.at("ranges")[stop - 1].at("byte_range")[1]}) ||
        chapter.at("title") != e.at("blocks")[start].at("text") ||
        chapter.at("evidence_class") != "inferred" || chapter.at("method") != "prefix") return invalid();
  }
  return {};
} catch (const Json::exception&) { return invalid(); }

Result<Prepared> prepare_file(const std::filesystem::path& original, const std::string& hash) try {
  LOOM_TRY_ASSIGN(auto reader, reader_for());
  const auto& config = reader.at("structure");
  const auto script = config.at("script").get<std::string>();
  std::error_code cap_ec;
  const auto original_size = std::filesystem::file_size(original, cap_ec);
  if (cap_ec || original_size > config.at("max_original_bytes").get<std::uint64_t>())
    return Error(Errc::InvalidArgument, "pdf_structure_input_limit");
  LOOM_TRY_ASSIGN(auto original_bytes, fsutil::read_file(original));
  if (Sha256::hex(original_bytes) != hash) return invalid();
  LOOM_TRY_ASSIGN(auto script_bytes, fsutil::read_file(script));
  LOOM_TRY_ASSIGN(auto document, chat::read_document_text(original, reader));
  std::error_code ec;
  const auto size = std::filesystem::file_size(original, ec);
  if (ec || size > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) return invalid();
  Json input{{"schema", "loom.pdf_structure.input/1"}, {"source", Json{{"sha256", hash}, {"size", size}}},
      {"reader", Json{{"adapter", reader.at("adapter")}, {"library", reader.at("library")}}},
      {"options", config.at("options")},
      {"document", Json{{"text", document.text}, {"pages", document.pages},
                        {"pages_without_text", document.pages_without_text}}}};
  LOOM_TRY_ASSIGN(auto extraction, run_helper(input, config));
  LOOM_TRY_ASSIGN(auto after, fsutil::read_file(script));
  LOOM_TRY_ASSIGN(auto after_reader, reader_for());
  LOOM_TRY_ASSIGN(auto after_original, fsutil::read_file(original));
  if (after != script_bytes || reader != after_reader || original_bytes != after_original)
    return Error(Errc::Conflict, "pdf_structure_helper_changed");
  LOOM_TRY(validate(extraction, hash, static_cast<std::int64_t>(size)));
  if (json::canonical(input_for(extraction)) != json::canonical(input)) return invalid();
  Json transform{{"schema", "loom.pdf_source_transform/1"}, {"extractor", extraction.at("extractor")},
      {"projection", extraction.at("projection").at("schema")}, {"reader", extraction.at("reader")},
      { "options", extraction.at("options")}, {"reader_script_blob", Sha256::hex(script_bytes)},
      {"host_package", reader.at("host_package")}};
  Json derivation{{"schema", "loom.pdf_source_derivation/1"}, {"original_source_id", "src_pdf_" + hash},
      {"original_sha256", hash}, {"original_size", size}, {"transform", transform},
      {"transform_sha256", Sha256::hex(json::canonical(transform))},
      {"extraction_blob", Sha256::hex(json::canonical(extraction))},
      {"text_sha256", extraction.at("projection").at("sha256")}};
  return Prepared{std::move(extraction), std::move(reader), std::move(derivation), std::move(script_bytes)};
} catch (const Json::exception&) { return invalid(); }

Result<std::string> record_source(Database& db, const std::string& title, const Prepared& d) try {
  const auto hash = d.derivation.at("original_sha256").get<std::string>();
  LOOM_TRY(validate(d.extraction, hash, d.derivation.at("original_size").get<std::int64_t>()));
  SourceRecord record;
  record.id = "src_pdf_" + hash;
  record.kind = d.reader.at("source_kind").get<std::string>();
  record.uri = "blob:" + hash;
  record.title = title;
  record.blob_hash = hash;
  record.size = d.derivation.at("original_size").get<std::int64_t>();
  record.format = "pdf";
  record.mime = d.reader.at("mime").get<std::string>();
  record.parser = "poppler_glib";
  // No unmeasured Poppler version or invented confidence.
  record.metadata = Json{{"coordinate", "original_pdf_bytes"}};
  ProvenanceStore store(db);
  LOOM_TRY_ASSIGN(auto prior, store.get_source(record.id));
  if (prior) {
    if (prior->blob_hash != hash || prior->size != record.size || prior->format != "pdf") return invalid();
    return record.id;
  }
  return store.add_source(record);
} catch (const Json::exception&) { return invalid(); }

Status validate_derivation(Database& db, const SourceRecord& source, const Json& meta) try {
  const auto& d = source.metadata.at("derivation");
  ProvenanceStore store(db);
  LOOM_TRY_ASSIGN(auto original, store.get_source(d.at("original_source_id").get<std::string>()));
  if (!original || json::get_string(d, "schema") != "loom.pdf_source_derivation/1" ||
      original->id != "src_pdf_" + original->blob_hash || original->format != "pdf" ||
      original->blob_hash != json::get_string(d, "original_sha256") || original->size != d.at("original_size") ||
      source.blob_hash != json::get_string(d, "text_sha256") || d != meta.at("derivation") ||
      d != meta.at("method").at("derivation") ||
      Sha256::hex(json::canonical(d.at("transform"))) != json::get_string(d, "transform_sha256") ||
      Sha256::hex(json::canonical(meta.at("method"))) != json::get_string(meta, "method_sha256") ||
      source.id != "src_text_" + Sha256::hex(json::canonical(Json::array({source.metadata.at("source_key"), source.blob_hash, d}))))
    return invalid();
  return {};
} catch (const Json::exception&) { return invalid(); }

Result<Prepared> prepare(Runtime& rt, const std::string& hash) {
  LOOM_TRY(rt.blobs().verify(hash));
  return prepare_file(rt.blobs().local_path(hash), hash);
}
Status verify_node(const Node& node, const std::filesystem::path& original) try {
  const auto& d = node.metadata.at("derivation");
  LOOM_TRY_ASSIGN(auto fresh, prepare_file(original, d.at("original_sha256").get<std::string>()));
  if (json::canonical(fresh.derivation) != json::canonical(d)) return invalid();
  const auto& range = node.metadata.at("byte_range");
  const auto text = fresh.extraction.at("text").get<std::string>();
  LOOM_TRY_ASSIGN(auto a, quantity(range.at(0)));
  LOOM_TRY_ASSIGN(auto b, quantity(range.at(1)));
  if (b < a || static_cast<std::uint64_t>(b) > text.size() || text.substr(a,b-a) != node.content) return invalid();
  return {};
} catch (const Json::exception&) { return invalid(); }
Status replay(Runtime& rt, const Json& e, const Json& d) try {
  LOOM_TRY_ASSIGN(auto fresh, prepare(rt, d.at("original_sha256").get<std::string>()));
  if (json::canonical(fresh.derivation) != json::canonical(d) ||
      json::canonical(fresh.extraction) != json::canonical(e)) return invalid();
  return {};
} catch (const Json::exception&) { return invalid(); }

Result<Json> materialize_structure(Runtime& rt, const Json& result, const Prepared& d,
                                   const Json& recipe, bool recognized) try {
  auto& db = rt.db();
  const auto source = result.at("source_id").get<std::string>();
  const auto method = result.at("method_sha256").get<std::string>();
  const auto& e = d.extraction;
  const auto& kinds = recipe.at("formats").at("pdf").at("structure_units");
  const auto edge = recipe.at("edges").at("part").get<std::string>();
  Json nodes{{"pages", Json::array()}, {"chapters", Json::array()}};
  auto add = [&](const Json& item, bool chapter, std::size_t index) -> Result<std::string> {
    const auto& range = item.at("byte_range");
    model::Unit unit;
    unit.source = source;
    unit.kind = kinds.at(chapter ? "chapter" : "page").get<std::string>();
    unit.locator.source = source;
    unit.locator.byte_start = range[0].get<std::int64_t>();
    unit.locator.byte_len = range[1].get<std::int64_t>() - *unit.locator.byte_start;
    // A PDF page has an ordinal locator, never a guessed byte range in the PDF.
    // A chapter candidate is a span in the explicitly derived UTF-8 source.
    const auto pointer = std::string(chapter ? "/chapters/" : "/pages/") + std::to_string(index);
    unit.attrs = Json{{"receipt_pointer", pointer}, {"receipt_blob", d.derivation.at("extraction_blob")},
        {"coordinate", chapter ? "derived_utf8_bytes" : "pdf_page_ordinal"},
        {"evidence_class", chapter ? "inferred" : "derived"},
        {"source_bytes_known", chapter},
        {"derived_source_id", source}, {"derived_byte_range", range},
        {"original_source_id", d.derivation.at("original_source_id")}};
    if (chapter) unit.attrs["structure"] = item;
    else {
      unit.source = d.derivation.at("original_source_id").get<std::string>();
      unit.locator.source = unit.source;
      unit.locator.member = "page:" + std::to_string(item.at("page").get<std::size_t>());
      unit.locator.byte_start.reset();
      unit.locator.byte_len.reset();
      unit.attrs["member_semantics"] = "pdf_page_ordinal";
      unit.attrs["original_locator"] = Json{{"source", unit.source}, {"page", item.at("page")}};
    }
    // bytes counts only known bytes in the located source; PDF page byte size is unknown.
    unit.bytes = unit.locator.byte_len.value_or(0);
    unit.id = model::Unit::make_id(unit.source, unit.locator);
    const auto id = "n_pdf_structure_" + Sha256::hex(unit.id + ":" + method);
    Json metadata{{"schema", "loom.pdf_structure.unit/1"}, {"unit", unit.to_json()},
        {"method_sha256", method}, {"derivation", d.derivation}, {"coordinate", unit.attrs.at("coordinate")}};
    LOOM_TRY_ASSIGN(auto prior, db.get_node(id));
    if (prior) {
      if (prior->metadata != metadata || prior->kind != unit.kind || !prior->content.empty()) return invalid();
    } else {
      NodeOptions options;
      options.node_id = id;
      options.metadata = metadata;
      LOOM_TRY(db.create_node(chapter ? item.at("title").get<std::string>() : unit.id, unit.kind, options));
    }
    LOOM_TRY(db.create_link(id, result.at("source_node").get<std::string>(), edge, 1.0,
        Json{{"evidence_class", chapter ? "inferred" : "derived"}, {"method_sha256", method}}));
    ProvenanceRecord provenance;
    provenance.id = "pv_" + Sha256::hex(id);
    provenance.subject_id = id;
    provenance.subject_kind = "node";
    provenance.source_id = unit.source;
    provenance.locator = unit.locator.to_json();
    provenance.transform = d.derivation.at("transform_sha256").get<std::string>();
    LOOM_TRY_ASSIGN(auto previous, rt.provenance().for_subject(id));
    if (previous.empty()) LOOM_TRY(rt.provenance().add(provenance));
    else if (previous.size() != 1 || previous.front().source_id != unit.source ||
             previous.front().locator != provenance.locator || previous.front().transform != provenance.transform) return invalid();
    return id;
  };
  for (const auto& page : e.at("pages")) {
    LOOM_TRY_ASSIGN(auto id, add(page, false, nodes["pages"].size()));
    nodes["pages"].push_back(id);
  }
  for (const auto& chapter : e.at("chapters")) {
    LOOM_TRY_ASSIGN(auto id, add(chapter, true, nodes["chapters"].size()));
    nodes["chapters"].push_back(id);
  }
  if (recognized) {
    if (result.at("occurrence_nodes").size() != e.at("blocks").size()) return invalid();
    for (std::size_t i = 0; i < e.at("blocks").size(); ++i) {
      const auto occurrence = result.at("occurrence_nodes")[i].get<std::string>();
      const auto page = e.at("blocks")[i].at("locator").at("page").get<std::size_t>();
      LOOM_TRY(db.create_link(occurrence, nodes.at("pages").at(page - 1).get<std::string>(), edge));
      for (std::size_t c = 0; c < e.at("chapters").size(); ++c) {
        const auto& block_range = e.at("chapters")[c].at("block_range");
        if (i >= block_range[0].get<std::size_t>() && i < block_range[1].get<std::size_t>())
          LOOM_TRY(db.create_link(occurrence, nodes.at("chapters")[c].get<std::string>(), edge, 1.0,
              Json{{"evidence_class", "inferred"}, {"method_sha256", method}}));
      }
    }
  }
  return nodes;
} catch (const Json::exception&) { return invalid(); }
}  // namespace loom::text::sources::pdf
