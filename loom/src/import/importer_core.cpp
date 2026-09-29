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
#include "loom/util/ids.h"
#include "loom/util/time.h"

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
                  {"already_imported", already_imported}, {"warnings", warnings}};
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
  if (!opts.record_provenance || !blobs_ || !prov_) return std::optional<std::vector<Conversation>>{};

  LOOM_TRY_ASSIGN(BlobRef blob, blobs_->put_file(path, idt::mime_for_format(fmt)));
  ctx.blob_hash = blob.hash;

  if (!opts.force) {
    LOOM_TRY_ASSIGN(auto sources, prov_->find_sources_by_hash(blob.hash));
    for (const auto& s : sources) {
      if (s.parser_version != parser_version) continue;
      auto recs = prov_->for_source(s.id, 1'000'000);
      if (!recs) continue;  // best-effort: fall through and re-register instead of failing the import
      std::vector<Conversation> convs;
      for (const auto& r : *recs) {
        if (r.subject_kind != "conversation") continue;
        auto c = db_.get_conv(r.subject_id);
        if (c && *c) convs.push_back(**c);
      }
      if (!convs.empty()) return std::optional<std::vector<Conversation>>(std::move(convs));
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
  LOOM_TRY_ASSIGN(ctx.source_id, prov_->add_source(std::move(rec)));
  return std::optional<std::vector<Conversation>>{};
}

Result<std::vector<Conversation>> ConversationImporter::with_source(
    const fs::path& path, std::string_view fmt, const ImportOptions& opts, std::string_view kind,
    const std::function<Result<std::vector<Conversation>>()>& body) {
  SourceCtx ctx;
  LOOM_TRY_ASSIGN(auto dup, prepare_source(path, fmt, opts, kind, ctx));
  if (dup) return std::move(*dup);
  SourceCtxGuard guard(*this, &ctx);
  return body();
}

void ConversationImporter::record_provenance(const Conversation& conv, std::string_view handler) {
  if (!prov_ || !current_source_) return;
  SourceCtx& ctx = *current_source_;

  Json loc = Json{{"conversation_index", ctx.conv_index}};
  if (ctx.zip_member) loc["zip_member"] = *ctx.zip_member;
  if (ctx.json_path) loc["json_path"] = *ctx.json_path;
  std::string transform = "import." + std::string(handler) + "@" + std::string(kImporterParserVersion);

  ProvenanceRecord conv_rec;
  conv_rec.subject_id = conv.id;
  conv_rec.subject_kind = "conversation";
  conv_rec.source_id = ctx.source_id;
  conv_rec.locator = loc;
  conv_rec.transform = transform;
  if (auto r = prov_->add(conv_rec); !r) {
    log::warn(kLog, "provenance: could not record conversation {}: {}", conv.id, r.error().message);
  }

  if (auto msgs = db_.get_msgs(conv.id, true); msgs) {
    std::vector<ProvenanceRecord> recs;
    recs.reserve(msgs->size());
    for (std::size_t i = 0; i < msgs->size(); ++i) {
      Json mloc = loc;
      mloc["message_index"] = static_cast<std::int64_t>(i);
      ProvenanceRecord mr;
      mr.subject_id = (*msgs)[i].id;
      mr.subject_kind = "message";
      mr.source_id = ctx.source_id;
      mr.locator = mloc;
      mr.transform = transform;
      recs.push_back(std::move(mr));
    }
    if (auto n = prov_->add_many(std::move(recs)); !n) {
      log::warn(kLog, "provenance: could not record messages for {}: {}", conv.id, n.error().message);
    }
  }
  ++ctx.conv_index;
}

// ── import_file(): dispatch + aggregate result ──────────────────────
Result<ImportResult> ConversationImporter::import_file_as(const fs::path& path, const ImportOptions& opts,
                                                           std::string_view source_kind,
                                                           std::optional<std::string> zip_member_rel) {
  std::string fmt = detect_format(path);
  ImportResult result;
  result.format = fmt;
  if (fmt == "unknown") return Error(Errc::Unsupported, "Unknown format: " + path.string());

  // Provider-export path: every ZIP (unless ExportMode::Off), and bare .json
  // files only with ExportMode::On. Distinct parser version so a prior legacy
  // import of the same bytes does not short-circuit the lossless one.
  const bool use_export = opts.export_mode != ExportMode::Off &&
                          (fmt == "zip" || (fmt == "json" && opts.export_mode == ExportMode::On));

  SourceCtx ctx;
  ctx.zip_member = std::move(zip_member_rel);
  LOOM_TRY_ASSIGN(auto dup, use_export ? prepare_source(path, fmt, opts, source_kind, ctx, kExportParserVersion, ".export")
                                       : prepare_source(path, fmt, opts, source_kind, ctx));
  result.source_id = ctx.source_id;
  result.blob_hash = ctx.blob_hash;

  if (dup) {
    result.conversations = std::move(*dup);
    result.already_imported = true;
    result.warnings.push_back("already imported (source " + result.source_id + ")");
    for (const auto& c : result.conversations) {
      if (auto msgs = db_.get_msgs(c.id, true); msgs) result.messages += static_cast<std::int64_t>(msgs->size());
    }
    return result;
  }

  SourceCtxGuard guard(*this, &ctx);
  Json export_report = nullptr;
  Result<std::vector<Conversation>> convs = [&]() -> Result<std::vector<Conversation>> {
    if (use_export && fmt == "zip") return export_zip_body(path, opts, export_report);
    if (use_export && fmt == "json") {
      auto r = export_json_body(path, opts, export_report);
      if (!r) return r.error();
      if (*r) return std::move(**r);
      return json_body(path, opts);  // not a provider export: legacy flattening
    }
    if (fmt == "zip") return zip_body(path, opts);
    if (fmt == "jsonl") return jsonl_body(path, opts);
    if (fmt == "sqlite") return sqlite_body(path, opts);
    if (fmt == "json") return json_body(path, opts);
    if (fmt == "html") return html_body(path, opts);
    if (fmt == "mht") return mht_body(path, opts);
    if (fmt == "screenshot") return screenshot_body(path, opts);
    if (fmt == "markdown") return markdown_body(path, opts);
    if (fmt == "text") return text_body(path, opts);
    return Error(Errc::Unsupported, "Unknown format: " + path.string());
  }();
  if (!convs) return convs.error();

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
    if (auto msgs = db_.get_msgs(c.id, true); msgs) result.messages += static_cast<std::int64_t>(msgs->size());
  }
  result.cancelled = cancelled(opts);

  if (!result.conversations.empty()) {
    bus_.emit(events::kImportDone, Json{{"count", static_cast<std::int64_t>(result.conversations.size())},
                                        {"source", path.string()},
                                        {"format", fmt}});
  }
  return result;
}

Result<ImportResult> ConversationImporter::import_file(const fs::path& path, const ImportOptions& opts) {
  return import_file_as(path, opts, "file");
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

  record_provenance(conv, handler);

  bus_.emit(events::kImportDone, Json{{"conv_id", conv.id}, {"count", count}, {"title", conv_title}});
  log::info(kLog, "Imported {} messages into '{}'", count, conv_title);
  return conv;
}

}  // namespace loom
