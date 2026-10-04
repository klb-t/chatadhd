// Core wiring: construction, format detection, the import_file() entry
// point, provenance/blob bookkeeping and import_message_list() (the funnel
// every format handler converges on). Format-specific bodies live in the
// sibling importer_*.cpp files.
#include "loom/importer.h"

#include <cctype>
#include <fstream>

#include "importer_internal.h"
#include "loom/event_bus.h"
#include "loom/log.h"
#include "loom/provenance.h"
#include "loom/sqlite.h"
#include "loom/util/ids.h"
#include "loom/util/time.h"
#include "loom/util/sha256.h"

namespace loom {

namespace fs = std::filesystem;
namespace idt = importer_detail;

namespace {
constexpr std::string_view kLog = "loom.import";
}  // namespace

// ── ImportResult / JsonArrayStreamer ────────────────────────────────
Json ImportResult::to_json() const {
  Json convs = Json::array();
  for (const auto& c : conversations) convs.push_back(c.to_json());
  Json out = Json{{"conversations", convs},   {"format", format},         {"source_id", source_id},
                  {"blob_hash", blob_hash},   {"messages", messages},     {"cancelled", cancelled},
                  {"already_imported", already_imported}, {"resumed", resumed},
                  {"include_result_metadata", include_result_metadata}, {"warnings", warnings}};
  out["retained_conversation_count"] = conversations.size();
  if (!export_report.is_null()) out["export_report"] = export_report;
  return out;
}

bool JsonArrayStreamer::feed(std::string_view chunk, const ElementFn& on_element) {
  if (done_) return true;
  std::size_t i = 0;
  if (!started_) {
    while (i < chunk.size()) {
      unsigned char c = static_cast<unsigned char>(chunk[i]);
      if (std::isspace(c)) {
        ++i;
        continue;
      }
      if (chunk[i] == '[') {
        started_ = true;
        ++i;
        break;
      }
      not_array_ = true;
      done_ = true;
      return false;
    }
    if (!started_) return true;  // whitespace-only so far; wait for more input
  }

  auto flush = [&] {
    std::size_t b = 0, e = buf_.size();
    while (b < e && std::isspace(static_cast<unsigned char>(buf_[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(buf_[e - 1]))) --e;
    if (e > b) on_element(std::string_view(buf_).substr(b, e - b));
    buf_.clear();
  };

  for (; i < chunk.size(); ++i) {
    char ch = chunk[i];
    if (escape_) {
      buf_.push_back(ch);
      escape_ = false;
      continue;
    }
    if (ch == '\\' && in_string_) {
      buf_.push_back(ch);
      escape_ = true;
      continue;
    }
    if (ch == '"') {
      in_string_ = !in_string_;
      buf_.push_back(ch);
      continue;
    }
    if (in_string_) {
      buf_.push_back(ch);
      continue;
    }
    if (ch == '{' || ch == '[') {
      ++depth_;
      buf_.push_back(ch);
    } else if (ch == '}' || ch == ']') {
      if (depth_ == 0) {
        done_ = true;
        return true;  // end of the top-level array; ignore trailing bytes
      }
      --depth_;
      buf_.push_back(ch);
      if (depth_ == 0) flush();
    } else if (ch == ',' && depth_ == 0) {
      flush();
    } else {
      if (depth_ > 0 || !std::isspace(static_cast<unsigned char>(ch))) buf_.push_back(ch);
    }
  }
  return true;
}

// ── Construction / format detection ─────────────────────────────────
ConversationImporter::ConversationImporter(Database& db, EventBus& bus, BlobStore* blobs, ProvenanceStore* prov,
                                           MediaProviders* media)
    : db_(db), bus_(bus), blobs_(blobs), prov_(prov), media_(media) {}

std::string ConversationImporter::detect_format(const fs::path& path) const {
  std::string ext = path.extension().string();
  for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  if (ext == ".zip") return "zip";
  if (ext == ".db") return "sqlite";
  if (ext == ".json") return "json";
  if (ext == ".jsonl" || ext == ".ndjson") return "jsonl";
  if (ext == ".html" || ext == ".htm") return "html";
  if (ext == ".mht" || ext == ".mhtml") return "mht";
  if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp") return "screenshot";
  if (ext == ".md") return "markdown";
  if (ext == ".txt" || ext == ".log") return "text";

  std::ifstream f(path, std::ios::binary);
  if (f) {
    std::string header(200, '\0');
    f.read(header.data(), static_cast<std::streamsize>(header.size()));
    header.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, f.gcount())));
    if (header.size() >= 4 && header.compare(0, 4, "PK\x03\x04") == 0) return "zip";
    if (header.find("SQLite format") != std::string::npos) return "sqlite";
    std::size_t i = 0;
    while (i < header.size() && std::isspace(static_cast<unsigned char>(header[i]))) ++i;
    if (i < header.size() && (header[i] == '{' || header[i] == '[')) return "json";
    if (idt::icontains(header, "<html") || idt::icontains(header, "<!doctype")) return "html";
  }
  return "unknown";
}

// ── Provenance / blob bookkeeping ───────────────────────────────────
Result<std::optional<std::vector<Conversation>>> ConversationImporter::prepare_source(
    const fs::path& path, std::string_view fmt, const ImportOptions& opts, std::string_view kind, SourceCtx& ctx,
    std::string_view parser_version, std::string_view parser_suffix) {
  const bool outer_binding = kind != "zip_member";
  if (outer_binding && opts.expected_source_bytes) {
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error) return Error(Errc::Io, "cannot stat admitted source: " + path.string());
    if (*opts.expected_source_bytes < 0 || size != static_cast<std::uintmax_t>(*opts.expected_source_bytes))
      return Error(Errc::Conflict, "source size changed after admission");
  }
  if (!opts.record_provenance || !blobs_ || !prov_) {
    if (outer_binding && opts.expected_source_hash) {
      LOOM_TRY_ASSIGN(auto hash, sha256_file_hex(path));
      if (hash != *opts.expected_source_hash) return Error(Errc::Conflict, "source hash changed after admission");
    }
    return std::optional<std::vector<Conversation>>{};
  }

  LOOM_TRY_ASSIGN(BlobRef blob, blobs_->put_file(path, idt::mime_for_format(fmt)));
  if (outer_binding && opts.expected_source_hash && blob.hash != *opts.expected_source_hash)
    return Error(Errc::Conflict, "source snapshot hash differs from admission");
  if (outer_binding && opts.expected_source_bytes && blob.size != *opts.expected_source_bytes)
    return Error(Errc::Conflict, "source snapshot size differs from admission");
  ctx.blob_hash = blob.hash;
  // Identity lookup/create is serialized independently of file I/O. Two
  // instances beginning the same resumable import share one source journal.
  auto lock = db_.lock();
  sql::Txn source_transaction(db_.conn());
  LOOM_TRY(source_transaction.begin_status());

  if (!opts.force) {
    LOOM_TRY_ASSIGN(auto sources, prov_->find_sources_by_hash(blob.hash));
    for (const auto& s : sources) {
      if (s.parser_version != parser_version) continue;
      if (parser_version == kExportParserVersion) {
        // export-3 records completion explicitly. A crash, cancellation or
        // partial representation must never become a successful cache hit.
        if (s.parser != "loom.importer." + std::string(fmt) + std::string(parser_suffix) ||
            json::get_string(s.metadata, "import_status") != "complete") continue;
        // The first member journal reused interpretation's -1 key for raw
        // fallback retention. Its own unknown-member delta identifies that
        // legacy phase unambiguously; it cannot prove interpretation complete.
        const std::string related_sources =
            "WITH RECURSIVE import_edges(parent_id,child_id) AS ("
            "SELECT json_extract(metadata,'$.parent_source_id'),id FROM loom_sources UNION "
            "SELECT parent.id,json_extract(part.value,'$.source_id') FROM loom_sources parent "
            "JOIN json_each(parent.metadata,'$.export_report.parts') part "
            "WHERE json_type(part.value,'$.source_id')='text'), "
            "import_sources(id) AS (SELECT ? UNION SELECT edge.child_id FROM import_edges edge "
            "JOIN import_sources parent ON edge.parent_id=parent.id) ";
        LOOM_TRY_ASSIGN(auto legacy_raw, db_.conn().query_int(related_sources +
            "SELECT COUNT(*) FROM loom_import_checkpoints c JOIN import_sources s ON s.id=c.source_id "
            "WHERE c.source_index=-1 "
            "AND EXISTS(SELECT 1 FROM json_each(c.metadata,'$.report.unknown_members') u WHERE u.value=c.member)",
            s.id));
        LOOM_TRY_ASSIGN(auto uncertified, db_.conn().query_int(related_sources +
            "SELECT COUNT(*) FROM loom_import_checkpoints c JOIN import_sources r ON r.id=c.source_id "
            "JOIN loom_sources source ON source.id=c.source_id WHERE c.source_index=-1 "
            "AND json_extract(source.metadata,'$.export_report.provider')='openai' "
            "AND COALESCE(json_extract(c.metadata,'$.binding_version'),0)<1 AND "
            "(c.member='message_feedback.json' OR c.member LIKE '%/message_feedback.json' "
            "OR c.member='shared_conversations.json' OR c.member LIKE '%/shared_conversations.json' "
            "OR (instr('/'||c.member,'/textdocs/')>0 AND substr(c.member,-5)='.json'))", s.id));
        const std::int64_t uncertified_bindings = uncertified.value_or(0);
        LOOM_TRY_ASSIGN(auto missing_child_ids, db_.conn().query_int(related_sources +
            "SELECT COUNT(*) FROM import_sources r JOIN loom_sources source ON source.id=r.id "
            "JOIN json_each(source.metadata,'$.export_report.parts') part "
            "WHERE json_type(part.value,'$.source_id') IS NOT 'text' "
            "OR json_extract(part.value,'$.source_id')=''", s.id));
        if (legacy_raw.value_or(0) != 0 || uncertified_bindings != 0 || missing_child_ids.value_or(0) != 0) {
          Json metadata = s.metadata;
          metadata["import_status"] = "partial";
          metadata["member_checkpoint_repair"] = legacy_raw.value_or(0) != 0
              ? "separate_raw_retention" : uncertified_bindings != 0
              ? "verify_auxiliary_bindings" : "record_nested_source_dependencies";
          LOOM_TRY(db_.conn().run("UPDATE loom_sources SET metadata=? WHERE id=?", json::py_dumps(metadata), s.id));
          continue;
        }
        const Json* ids = json::find(s.metadata, "conversation_ids");
        if (!ids || !ids->is_array()) continue;
        std::vector<Conversation> convs;
        bool intact = true;
        for (const auto& id : *ids) {
          if (!id.is_string()) { intact = false; break; }
          auto c = db_.get_conv(id.get<std::string>());
          if (!c || !*c) { intact = false; break; }
          Conversation returned = **c;
          if (!opts.include_result_metadata) returned.metadata = Json::object();
          convs.push_back(std::move(returned));
        }
        if (!intact) continue;
        ctx.source_id = s.id;
        LOOM_TRY(source_transaction.commit());
        return std::optional<std::vector<Conversation>>(std::move(convs));
      }
      LOOM_TRY_ASSIGN(auto records, db_.conn().prepare(
          "SELECT subject_id FROM loom_provenance WHERE source_id=? AND subject_kind='conversation' ORDER BY rowid"));
      records.bind(1, s.id);
      std::vector<Conversation> convs;
      for (;;) {
        LOOM_TRY_ASSIGN(bool row, records.step());
        if (!row) break;
        auto c = db_.get_conv(records.get_text(0));
        if (c && *c) convs.push_back(**c);
      }
      if (!convs.empty()) {
        ctx.source_id = s.id;
        LOOM_TRY(source_transaction.commit());
        return std::optional<std::vector<Conversation>>(std::move(convs));
      }
    }
  }

  if (parser_version == kExportParserVersion && opts.resume && !opts.force) {
    LOOM_TRY_ASSIGN(auto sources, prov_->find_sources_by_hash(blob.hash));
    for (const auto& source : sources) {
      if (source.parser_version != parser_version ||
          source.parser != "loom.importer." + std::string(fmt) + std::string(parser_suffix) ||
          json::get_int(source.metadata, "import_resume_version") != 1) continue;
      ctx.source_id = source.id;
      ctx.source_filename = fs::path(source.uri).filename().string();
      ctx.resumed = true;
      LOOM_TRY(source_transaction.commit());
      return std::optional<std::vector<Conversation>>{};
    }
  }

  SourceRecord rec;
  rec.kind = std::string(kind);
  rec.uri = path.string();
  rec.blob_hash = blob.hash;
  rec.size = blob.size;
  rec.format = std::string(fmt);
  rec.parser = "loom.importer." + std::string(fmt) + std::string(parser_suffix);
  rec.parser_version = std::string(parser_version);
  if (opts.title) rec.title = *opts.title;
  if (kind == "zip_member" && current_source_ && ctx.zip_member) {
    rec.metadata["parent_source_id"] = current_source_->source_id;
    rec.metadata["locator"] = Json{{"source", "sha256:" + current_source_->blob_hash}, {"member", *ctx.zip_member}};
    if (ctx.archive_index) rec.metadata["locator"]["archive_index"] = *ctx.archive_index;
  }
  if (parser_version == kExportParserVersion) {
    rec.metadata["import_status"] = "pending";
    if (opts.resume) rec.metadata["import_resume_version"] = 1;
  }
  LOOM_TRY_ASSIGN(ctx.source_id, prov_->add_source(std::move(rec)));
  LOOM_TRY(source_transaction.commit());
  return std::optional<std::vector<Conversation>>{};
}

Result<Json> ConversationImporter::materialize_zip_member(const fs::path& path, const ImportOptions& opts,
                                                            std::string_view member, std::int64_t archive_index) {
  if (!opts.record_provenance || !blobs_ || !prov_ || !current_source_) return Json::object();
  const auto& parent = *current_source_;
  LOOM_TRY_ASSIGN(auto blob, blobs_->put_file(path, idt::mime_for_format(detect_format(path))));
  Json locator{{"source", "sha256:" + parent.blob_hash}, {"member", member}, {"archive_index", archive_index}};
  SourceRecord source;
  source.kind = "zip_member";
  source.uri = "sha256:" + parent.blob_hash + "!" + std::string(member);
  source.blob_hash = blob.hash;
  source.size = blob.size;
  source.format = detect_format(path);
  source.parser = "loom.importer.zip.member";
  source.parser_version = std::string(kExportParserVersion);
  source.metadata = Json{{"parent_source_id", parent.source_id}, {"locator", locator}};
  auto lock = db_.lock();
  sql::Txn member_transaction(db_.conn());
  LOOM_TRY(member_transaction.begin_status());
  if (opts.resume && !opts.force) {
    LOOM_TRY_ASSIGN(auto prior, prov_->find_sources_by_hash(blob.hash));
    for (const auto& candidate : prior) {
      if (candidate.parser == source.parser && candidate.parser_version == source.parser_version &&
          candidate.metadata == source.metadata)
        return Json{{"source_id", candidate.id}, {"blob_hash", blob.hash}, {"locator", locator}};
    }
  }
  LOOM_TRY_ASSIGN(auto id, prov_->add_source(std::move(source)));
  ProvenanceRecord provenance;
  provenance.subject_id = id;
  provenance.subject_kind = "source";
  provenance.source_id = parent.source_id;
  provenance.locator = locator;
  provenance.transform = "extract.zip@" + std::string(kExportParserVersion);
  LOOM_TRY(prov_->add(std::move(provenance)));
  LOOM_TRY(member_transaction.commit());
  return Json{{"source_id", id}, {"blob_hash", blob.hash}, {"locator", locator}};
}

Status ConversationImporter::set_source_outcome(const SourceCtx& ctx, std::string_view status,
                                               const std::vector<Conversation>& conversations, const Json& report) {
  if (!prov_ || ctx.source_id.empty()) return {};
  auto lock = db_.lock();
  sql::Txn transaction(db_.conn());
  LOOM_TRY(transaction.begin_status());
  LOOM_TRY_ASSIGN(auto source, prov_->get_source(ctx.source_id));
  if (!source) return Error(Errc::NotFound, "import source disappeared: " + ctx.source_id);
  Json metadata = source->metadata;
  if (json::get_string(metadata, "import_status") == "complete" && status != "complete") return {};
  metadata["import_status"] = status;
  metadata["conversation_ids"] = Json::array();
  for (const auto& conversation : conversations) metadata["conversation_ids"].push_back(conversation.id);
  if (!report.is_null()) metadata["export_report"] = report;
  LOOM_TRY(db_.conn().run("UPDATE loom_sources SET metadata=? WHERE id=?", json::py_dumps(metadata), ctx.source_id));
  return transaction.commit();
}

Result<std::vector<Conversation>> ConversationImporter::with_source(
    const fs::path& path, std::string_view fmt, const ImportOptions& opts, std::string_view kind,
    const std::function<Result<std::vector<Conversation>>(const fs::path&)>& body) {
  SourceCtx ctx;
  ctx.source_filename = path.filename().string();
  LOOM_TRY_ASSIGN(auto dup, prepare_source(path, fmt, opts, kind, ctx));
  if (dup) return std::move(*dup);
  SourceCtxGuard guard(*this, &ctx);
  // A copied SQLite main file omits live WAL sidecars. Preserve the original
  // database connection path until a consistent backup source is available.
  return body(fmt != "sqlite" && blobs_ && !ctx.blob_hash.empty() ? blobs_->path_for(ctx.blob_hash) : path);
}

Status ConversationImporter::record_provenance(const Conversation& conv, std::string_view handler) {
  if (!prov_ || !current_source_ || current_source_->source_id.empty()) return {};
  SourceCtx& ctx = *current_source_;

  Json loc = Json{{"conversation_index", ctx.conv_index}};
  if (ctx.zip_member) loc["zip_member"] = *ctx.zip_member;
  if (ctx.json_path) loc["json_path"] = *ctx.json_path;
  const bool provider_export = handler == "export";
  if (provider_export) {
    if (!ctx.blob_hash.empty()) loc["source"] = "sha256:" + ctx.blob_hash;
    if (const Json* metadata = json::find(conv.metadata, "export"); metadata && metadata->is_object()) {
      if (const Json* index = json::find(*metadata, "archive_index")) loc["archive_index"] = *index;
      // source identifies the innermost archive blob. Its member name is
      // local; legacy zip_member may include the containing archive chain.
      if (ctx.zip_member) {
        if (const Json* member = json::find(*metadata, "member"); member && member->is_string()) loc["member"] = *member;
      }
      if (const Json* pointer = json::find(*metadata, "json_pointer"); pointer && pointer->is_string()) {
        loc["json_pointer"] = *pointer;
        loc["json_path"] = *pointer;  // existing provenance consumer spelling
        loc["source_conversation_index"] = (*metadata)["source_index"];
      }
    }
  }
  std::string transform = "import." + std::string(handler) + "@" +
      std::string(provider_export ? kExportParserVersion : kImporterParserVersion);

  ProvenanceRecord conv_rec;
  conv_rec.subject_id = conv.id;
  conv_rec.subject_kind = "conversation";
  conv_rec.source_id = ctx.source_id;
  conv_rec.locator = loc;
  conv_rec.transform = transform;
  if (auto r = prov_->add(conv_rec); !r) {
    return r.error();
  }

  LOOM_TRY_ASSIGN(auto msgs, db_.get_msgs(conv.id, true));
  {
    std::vector<ProvenanceRecord> recs;
    recs.reserve(msgs.size());
    for (std::size_t i = 0; i < msgs.size(); ++i) {
      Json mloc = loc;
      mloc["message_index"] = static_cast<std::int64_t>(i);
      if (provider_export) {
        const Json* metadata = json::find(msgs[i].metadata, "export");
        if (metadata && metadata->is_object()) {
          if (const Json* pointer = json::find(*metadata, "json_pointer"); pointer && pointer->is_string()) {
            mloc["json_pointer"] = *pointer;
            mloc["json_path"] = *pointer;
            mloc["view_message_index"] = static_cast<std::int64_t>(i);
            mloc.erase("message_index");  // object keys are not array offsets
            if (const Json* source_index = json::find(*metadata, "source_index"); source_index && source_index->is_number_integer())
              mloc["message_index"] = *source_index;
            if (const Json* key = json::find(*metadata, "source_key")) mloc["source_key"] = *key;
            if (const Json* traversal = json::find(*metadata, "traversal_index")) mloc["traversal_index"] = *traversal;
          }
        }
      }
      ProvenanceRecord mr;
      mr.subject_id = msgs[i].id;
      mr.subject_kind = "message";
      mr.source_id = ctx.source_id;
      mr.locator = mloc;
      mr.transform = transform;
      recs.push_back(std::move(mr));
    }
    if (auto n = prov_->add_many(std::move(recs)); !n) {
      return n.error();
    }
  }
  ++ctx.conv_index;
  return {};
}

// ── import_file(): dispatch + aggregate result ──────────────────────
Result<ImportResult> ConversationImporter::import_file_as(const fs::path& path, const ImportOptions& opts,
                                                           std::string_view source_kind,
                                                           std::optional<std::string> zip_member_rel,
                                                           std::optional<std::int64_t> archive_index) {
  std::string fmt = detect_format(path);
  ImportResult result;
  result.format = fmt;
  result.include_result_metadata = opts.include_result_metadata;
  if (fmt == "sqlite") result.warnings.push_back(
      "SQLite reads the original database, including WAL. A captured main-file blob excludes WAL bytes "
      "and does not bind the live query rows.");
  if (fmt == "unknown") return Error(Errc::Unsupported, "Unknown format: " + path.string());

  // Provider-export path: every ZIP (unless ExportMode::Off), and bare .json
  // files only with ExportMode::On. Distinct parser version so a prior legacy
  // import of the same bytes does not short-circuit the lossless one.
  const bool use_export = opts.export_mode != ExportMode::Off &&
                          (fmt == "zip" || (fmt == "json" && opts.export_mode == ExportMode::On));

  SourceCtx ctx;
  ctx.source_filename = path.filename().string();
  ctx.zip_member = std::move(zip_member_rel);
  ctx.archive_index = archive_index;
  LOOM_TRY_ASSIGN(auto dup, use_export ? prepare_source(path, fmt, opts, source_kind, ctx, kExportParserVersion, ".export")
                                       : prepare_source(path, fmt, opts, source_kind, ctx));
  result.source_id = ctx.source_id;
  result.blob_hash = ctx.blob_hash;
  result.resumed = ctx.resumed;

  if (dup) {
    result.conversations = std::move(*dup);
    result.already_imported = true;
    result.warnings.push_back("already imported (source " + result.source_id + ")");
    if (use_export && prov_) {
      auto source = prov_->get_source(result.source_id);
      if (source && *source) {
        if (const Json* report = json::find((**source).metadata, "export_report")) {
          result.export_report = *report;
          result.export_report["include_result_metadata"] = opts.include_result_metadata;
        }
      }
    }
    for (const auto& c : result.conversations) {
      auto lock = db_.lock();
      LOOM_TRY_ASSIGN(auto count, db_.conn().query_int("SELECT COUNT(*) FROM messages WHERE conv_id=?", c.id));
      result.messages += count.value_or(0);
    }
    return result;
  }

  SourceCtxGuard guard(*this, &ctx);
  // Ordinary files parse the immutable bytes that established their source
  // identity. SQLite retains its live original path so committed WAL rows
  // remain visible; the main-file blob alone is not a database snapshot.
  const fs::path input_path = fmt != "sqlite" && blobs_ && !ctx.blob_hash.empty()
      ? blobs_->path_for(ctx.blob_hash) : path;
  Json export_report = nullptr;
  Result<std::vector<Conversation>> convs = [&]() -> Result<std::vector<Conversation>> {
    if (use_export && fmt == "zip") return export_zip_body(input_path, opts, export_report);
    if (use_export && fmt == "json") {
      auto r = export_json_body(input_path, opts, export_report);
      if (!r) return r.error();
      if (*r) return std::move(**r);
      return json_body(input_path, opts);  // not a provider export: legacy flattening
    }
    if (fmt == "zip") return zip_body(input_path, opts);
    if (fmt == "jsonl") return jsonl_body(input_path, opts);
    if (fmt == "sqlite") return sqlite_body(input_path, opts);
    if (fmt == "json") return json_body(input_path, opts);
    if (fmt == "html") return html_body(input_path, opts);
    if (fmt == "mht") return mht_body(input_path, opts);
    if (fmt == "screenshot") return screenshot_body(input_path, opts);
    if (fmt == "markdown") return markdown_body(input_path, opts);
    if (fmt == "text") return text_body(input_path, opts);
    return Error(Errc::Unsupported, "Unknown format: " + path.string());
  }();
  if (!convs) {
    if (use_export) {
      auto recorded = set_source_outcome(ctx, "failed", {}, export_report);
      if (!recorded) log::warn(kLog, "could not record failed source outcome: {}", recorded.error().message);
    }
    return convs.error();
  }

  result.conversations = std::move(*convs);
  if (!export_report.is_null()) {
    result.export_report = export_report;
    if (const Json* w = json::find(export_report, "warnings"); w && w->is_array()) {
      for (const auto& x : *w) {
        if (x.is_string()) result.warnings.push_back(x.get<std::string>());
      }
    }
  }
  for (const auto& c : result.conversations) {
    auto lock = db_.lock();
    LOOM_TRY_ASSIGN(auto count, db_.conn().query_int("SELECT COUNT(*) FROM messages WHERE conv_id=?", c.id));
    result.messages += count.value_or(0);
  }
  result.cancelled = cancelled(opts);
  if (use_export) {
    const bool partial = export_report.is_object() && export_report.value("partial", false);
    LOOM_TRY(set_source_outcome(ctx, result.cancelled ? "cancelled" : partial ? "partial" : "complete",
                               result.conversations, export_report));
  }

  if (!result.conversations.empty()) {
    bus_.emit(events::kImportDone, Json{{"count", static_cast<std::int64_t>(result.conversations.size())},
                                        {"source", path.string()},
                                        {"format", fmt}});
  }
  return result;
}

Result<ImportResult> ConversationImporter::import_file(const fs::path& path, const ImportOptions& opts) {
  if (opts.preflight) LOOM_TRY(opts.preflight(path));
  auto result = import_file_as(path, opts, "file");
  if (result && opts.completed) opts.completed(*result);
  return result;
}

// ── import_message_list(): the funnel every handler converges on ────
Result<Conversation> ConversationImporter::import_message_list(const Json& messages,
                                                                const std::optional<std::string>& title) {
  std::string conv_title = idt::title_or(title, "Import ", "%Y-%m-%d %H:%M");

  std::vector<BatchMessage> batch;
  if (messages.is_array()) {
    batch.reserve(messages.size());
    for (const auto& m : messages) {
      auto norm = idt::normalize_message(m);
      if (!norm) continue;
      BatchMessage bm;
      bm.role = norm->role;
      bm.text = norm->text;
      batch.push_back(std::move(bm));
    }
  }
  return finish_import(conv_title, std::move(batch), "message_list");
}

Result<Conversation> ConversationImporter::finish_import(const std::string& conv_title, std::vector<BatchMessage> batch,
                                                          std::string_view handler) {
  LOOM_TRY_ASSIGN(Conversation conv, db_.create_conv("[Import] " + conv_title));
  LOOM_TRY_ASSIGN(int count, db_.batch_create_msgs(conv.id, batch));

  if (auto recorded = record_provenance(conv, handler); !recorded)
    log::warn(kLog, "provenance: {}", recorded.error().message);

  bus_.emit(events::kImportDone, Json{{"conv_id", conv.id}, {"count", count}, {"title", conv_title}});
  log::info(kLog, "Imported {} messages into '{}'", count, conv_title);
  return conv;
}

}  // namespace loom
