// catalog.h: Catalog::scan — streams files, directories and zips into
// loom_cat_units + sketches WITHOUT touching core tables (R1). Single
// producer, sequential (ScanConfig::threads is accepted but not yet honoured
// -- see the final report's gaps); still bounded memory and still streaming:
// a zip member is inflated through miniz's pull iterator (stream.cpp) and a
// JSON array member through OffsetArrayScanner, both in kReadChunk pieces,
// so a multi-GB export never sits in RAM at once. Units are committed in
// stream order (member, then byte offset), so the catalogue is identical for
// any thread count once true parallelism is added (I5).
#include "loom/catalog.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "archive/archive_internal.h"
#include "catalog_internal.h"
#include "loom/db.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom::catalog {

namespace fs = std::filesystem;
using namespace loom::catalog::internal;

namespace {

struct Stats {
  std::int64_t units = 0;
  std::int64_t units_new = 0;
  std::int64_t unchanged = 0;
  std::int64_t versions = 0;
  std::int64_t bytes = 0;
  std::vector<std::string> warnings;
  Json to_json() const {
    Json w = Json::array();
    for (auto& s : warnings) w.push_back(s);
    return Json{{"units", units}, {"new", units_new}, {"unchanged", unchanged},
                {"versions", versions}, {"bytes", bytes}, {"warnings", w}};
  }
};

std::string lower_ext(const fs::path& p) {
  std::string e = p.extension().string();
  for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return e;
}

bool skip_name(const std::string& name) {
  return name.empty() || name[0] == '.' || name == "__MACOSX";
}

std::string platform_of(std::string_view kind) {
  if (kind == "chatgpt") return "chatgpt";
  if (kind == "claude" || kind == "claude_projects" || kind == "claude_memories") return "claude";
  return "file";
}

std::string unit_kind_of(std::string_view kind) {
  if (kind == "chatgpt" || kind == "claude") return "conversation";
  if (kind == "claude_projects") return "project";
  if (kind == "claude_memories") return "memory";
  if (kind == "file") return "file";
  return "record";
}

std::string ext_id_of(const Json& element, std::string_view kind) {
  if (kind == "chatgpt") return json::get_string(element, "id");
  if (kind == "claude" || kind == "claude_projects") return json::get_string(element, "uuid");
  if (kind == "claude_memories") return "memories";
  return "";
}

Result<std::string> register_source(Database& db, std::string_view source_id, const fs::path& path,
                                    std::int64_t bytes, std::string_view kind) {
  auto lk = db.lock();
  LOOM_TRY(db.conn().run(
      "INSERT OR IGNORE INTO loom_cat_sources (id, path, bytes, kind, scanned_at) VALUES (?, ?, ?, ?, ?)",
      std::string(source_id), path.string(), bytes, std::string(kind), timeutil::utc_now_iso()));
  return std::string(source_id);
}

Result<bool> checkpoint_done(Database& db, std::string_view source_id, std::string_view member) {
  auto lk = db.lock();
  auto v = db.conn().query_int("SELECT done FROM loom_cat_checkpoint WHERE source_id = ? AND member = ?",
                               std::string(source_id), std::string(member));
  if (!v) return Error(v.error());
  return v->has_value() && **v != 0;
}

Status mark_checkpoint(Database& db, std::string_view source_id, std::string_view member, bool done) {
  auto lk = db.lock();
  return db.conn().run(
      "INSERT OR REPLACE INTO loom_cat_checkpoint (source_id, member, element_ordinal, byte_offset, done, updated) "
      "VALUES (?, ?, -1, 0, ?, ?)",
      std::string(source_id), std::string(member), done ? 1 : 0, timeutil::utc_now_iso());
}

// One unit's identity, already resolved bytes/hash; ingest_unit() sketches,
// finds mentions, resolves dedup/versioning and writes the row.
struct PendingUnit {
  std::string platform;
  std::string kind;       // chatgpt | claude | claude_projects | claude_memories | file | record
  model::Locator locator;
  std::string content_hash;
  std::int64_t bytes = 0;
  std::string ext_id;
  ExtractedText text;   // prose/code/n_msgs/... already extracted
  std::string title_hint;
  std::string date_hint;
};

Status ingest_unit(Database& db, const AliasIndex& alias_idx, const kb::Normalizer& norm, const SketchParams& params,
                   const std::string& source_id, PendingUnit&& pu, Stats& stats) {
  auto lk = db.lock();
  sql::Connection& c = db.conn();
  auto existing = c.query_text("SELECT id FROM loom_cat_units WHERE content_hash = ? LIMIT 1", pu.content_hash);
  if (!existing) return Error(existing.error());
  if (*existing) {
    ++stats.unchanged;
    return {};
  }

  Sketch sketch = Sketch::build(pu.text.prose, pu.text.code, params, norm);
  std::string folded = norm.fold(pu.text.prose.substr(0, static_cast<std::size_t>(std::min<std::int64_t>(
                                    kSketchByteCap, static_cast<std::int64_t>(pu.text.prose.size())))));
  auto mentions = alias_idx.find(folded, params.max_mentions);
  auto versions = find_version_mentions(folded, mentions, /*window_chars=*/80);
  if (static_cast<int>(mentions.size()) < params.max_mentions) {
    int room = params.max_mentions - static_cast<int>(mentions.size());
    for (auto& v : versions) {
      if (room-- <= 0) break;
      mentions.push_back(std::move(v));
    }
  }
  Json mentions_j = Json::array();
  for (auto& m : mentions) mentions_j.push_back(m.to_json());

  model::Unit unit;
  unit.source = source_id;
  unit.kind = unit_kind_of(pu.kind);
  unit.locator = pu.locator;
  unit.title = !pu.text.title.empty() ? pu.text.title : pu.title_hint;
  unit.date = !pu.text.date.empty() ? pu.text.date : pu.date_hint;
  unit.lang = sketch.lang;
  unit.bytes = pu.bytes;
  unit.id = model::Unit::make_id(source_id, pu.locator);

  std::string prev_version;
  if (!pu.ext_id.empty()) {
    auto prev = c.query_text(
        "SELECT id FROM loom_cat_units WHERE platform = ? AND ext_id = ? ORDER BY created DESC LIMIT 1",
        pu.platform, pu.ext_id);
    if (!prev) return Error(prev.error());
    if (*prev) {
      prev_version = **prev;
      ++stats.versions;
    }
  }

  CatalogUnit cu;
  cu.unit = unit;
  cu.platform = pu.platform;
  cu.ext_id = pu.ext_id;
  cu.content_hash = pu.content_hash;
  cu.n_msgs = pu.text.n_msgs;
  cu.n_chars = sketch.n_chars;
  cu.n_code_chars = sketch.n_code_chars;
  cu.n_forks = pu.text.n_forks;
  cu.attachments = pu.text.attachments;
  cu.head = pu.text.head;
  cu.prev_version = prev_version;
  cu.mentions = mentions_j;

  LOOM_TRY(c.run(
      "INSERT OR REPLACE INTO loom_cat_units (id, source, kind, locator, title, date, lang, bytes, platform, "
      "ext_id, content_hash, prev_version, sketch, body, created) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
      unit.id, source_id, unit.kind, json::dump(unit.locator.to_json()), unit.title, unit.date, unit.lang, unit.bytes,
      pu.platform, pu.ext_id, pu.content_hash, prev_version, json::dump(sketch.to_json()), json::dump(cu.to_json()),
      timeutil::utc_now_iso()));
  ++stats.units;
  ++stats.units_new;
  stats.bytes += pu.bytes;
  return {};
}

// ── JSON array member/file: streams elements, dispatching each through
// ingest_unit(); falls back to whole-document parse for a JSON object member
// (e.g. Claude's memories.json), bounded by kElementByteCap.
Status scan_json_stream(Database& db, const AliasIndex& idx, const kb::Normalizer& norm, const SketchParams& params,
                        const std::string& source_id, const std::string& member,
                        const std::function<Status(const std::function<void(std::string_view)>&)>& reader,
                        Stats& stats) {
  OffsetArrayScanner scanner;
  std::string tail;  // captured only if the stream turns out not to be an array
  bool capturing_tail = true;
  auto on_chunk = [&](std::string_view chunk) {
    if (!scanner.finished()) {
      scanner.feed(chunk, [&](std::string_view elem, std::int64_t begin, std::int64_t end) {
        if (static_cast<std::int64_t>(elem.size()) > kElementByteCap) {
          stats.warnings.push_back(member + ": element too large (" + std::to_string(elem.size()) + " bytes), skipped");
          return true;
        }
        auto parsed = json::parse(elem);
        if (!parsed) {
          stats.warnings.push_back(member + ": malformed JSON element skipped: " + parsed.error().message);
          return true;
        }
        std::string kind = archive::sniff_export_element(*parsed);
        PendingUnit pu;
        pu.platform = platform_of(kind);
        pu.kind = kind.empty() ? "record" : kind;
        pu.locator.source = source_id;
        pu.locator.member = member;
        pu.locator.byte_start = begin;
        pu.locator.byte_len = end - begin;
        pu.content_hash = Sha256::hex(elem);
        pu.bytes = static_cast<std::int64_t>(elem.size());
        pu.ext_id = ext_id_of(*parsed, kind);
        pu.text = extract_text(*parsed, kind.empty() ? std::string_view("record") : std::string_view(kind));
        auto st = ingest_unit(db, idx, norm, params, source_id, std::move(pu), stats);
        if (!st) stats.warnings.push_back(member + ": " + st.error().message);
        return true;
      });
    }
    if (capturing_tail && scanner.not_array() && static_cast<std::int64_t>(tail.size()) < kElementByteCap) {
      tail.append(chunk);
    }
  };
  LOOM_TRY(reader(on_chunk));
  if (scanner.not_array()) {
    auto parsed = json::parse(tail);
    if (!parsed) {
      stats.warnings.push_back(member + ": not JSON, skipped");
      return {};
    }
    std::vector<const Json*> elements;
    if (parsed->is_object()) {
      const char* wrappers[] = {"conversations", "projects", "memories"};
      const Json* inner = nullptr;
      for (auto* w : wrappers) {
        if (const Json* v = json::find(*parsed, w); v && v->is_array()) {
          inner = v;
          break;
        }
      }
      if (inner) {
        for (const auto& e : *inner) elements.push_back(&e);
      } else {
        elements.push_back(&*parsed);
      }
    } else if (parsed->is_array()) {
      for (const auto& e : *parsed) elements.push_back(&e);
    }
    int ord = 0;
    for (const auto* ep : elements) {
      std::string kind = archive::sniff_export_element(*ep);
      PendingUnit pu;
      pu.platform = platform_of(kind);
      pu.kind = kind.empty() ? "record" : kind;
      pu.locator.source = source_id;
      pu.locator.member = member;
      pu.locator.json_pointer = "/" + std::to_string(ord++);
      std::string dumped = json::dump(*ep);
      pu.content_hash = Sha256::hex(dumped);
      pu.bytes = static_cast<std::int64_t>(dumped.size());
      pu.ext_id = ext_id_of(*ep, kind);
      pu.text = extract_text(*ep, kind.empty() ? std::string_view("record") : std::string_view(kind));
      auto st = ingest_unit(db, idx, norm, params, source_id, std::move(pu), stats);
      if (!st) stats.warnings.push_back(member + ": " + st.error().message);
    }
  }
  return {};
}

// ── Whole-file / whole-member unit (markdown, text, code, unknown): hashed
// and sketched with bounded memory (running sha256 + a capped sample), never
// DOM-parsed.
Status scan_whole(Database& db, const AliasIndex& idx, const kb::Normalizer& norm, const SketchParams& params,
                  const std::string& source_id, const std::string& member,
                  const std::function<Status(const std::function<void(std::string_view)>&)>& reader, Stats& stats) {
  Sha256 hasher;
  std::string sample;
  std::int64_t total = 0;
  LOOM_TRY(reader([&](std::string_view chunk) {
    hasher.update(chunk);
    total += static_cast<std::int64_t>(chunk.size());
    if (static_cast<std::int64_t>(sample.size()) < kSketchByteCap) {
      std::size_t room = static_cast<std::size_t>(kSketchByteCap) - sample.size();
      sample.append(chunk.substr(0, std::min(room, chunk.size())));
    }
  }));
  PendingUnit pu;
  pu.platform = "file";
  pu.kind = "file";
  pu.locator.source = source_id;
  pu.locator.member = member;
  pu.locator.byte_start = 0;
  pu.locator.byte_len = total;
  pu.content_hash = to_hex(hasher.finish());
  pu.bytes = total;
  pu.ext_id = member;
  pu.text.prose = sample;
  fs::path mp(member);
  pu.title_hint = mp.filename().string();
  return ingest_unit(db, idx, norm, params, source_id, std::move(pu), stats);
}

Status stream_file_chunks(const fs::path& path, const std::function<void(std::string_view)>& on_chunk) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Error(Errc::Io, "cannot open " + path.string());
  std::string buf(kReadChunk, '\0');
  while (in) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    std::streamsize n = in.gcount();
    if (n <= 0) break;
    on_chunk(std::string_view(buf.data(), static_cast<std::size_t>(n)));
  }
  return {};
}

Status scan_zip_source(Database& db, const AliasIndex& idx, const kb::Normalizer& norm, const SketchParams& params,
                       const std::string& source_id, const fs::path& zip_path, bool force, Stats& stats,
                       const CancelToken* cancel) {
  LOOM_TRY_ASSIGN(auto entries, list_zip_entries(zip_path));
  std::sort(entries.begin(), entries.end(), [](const ZipEntry& a, const ZipEntry& b) { return a.name < b.name; });
  for (const auto& e : entries) {
    if (cancel && cancel->cancelled()) break;
    if (e.is_dir) continue;
    fs::path mp(e.name);
    if (skip_name(mp.filename().string()) || e.name.find("__MACOSX") != std::string::npos) continue;
    if (!force) {
      LOOM_TRY_ASSIGN(bool done, checkpoint_done(db, source_id, e.name));
      if (done) continue;
    }
    std::string ext = lower_ext(mp);
    auto reader = [&](const std::function<void(std::string_view)>& on_chunk) {
      return stream_zip_member(zip_path, e.index, on_chunk);
    };
    Status st = (ext == ".json" || ext == ".jsonl")
                    ? scan_json_stream(db, idx, norm, params, source_id, e.name, reader, stats)
                    : scan_whole(db, idx, norm, params, source_id, e.name, reader, stats);
    if (!st) {
      stats.warnings.push_back(e.name + ": " + st.error().message);
      continue;
    }
    LOOM_TRY(mark_checkpoint(db, source_id, e.name, true));
  }
  return {};
}

}  // namespace

Result<Json> Catalog::scan(const ScanConfig& cfg, const ProgressFn& progress, const CancelToken* cancel) {
  LOOM_TRY(ensure_schema(rt_.db()));
  kb::Normalizer norm(*pack_);
  AliasIndex idx = AliasIndex::from_pack(*pack_);
  Stats stats;

  std::int64_t src_i = 0;
  std::int64_t total_src = static_cast<std::int64_t>(cfg.sources.size());
  for (const auto& src : cfg.sources) {
    ++src_i;
    if (cancel && cancel->cancelled()) break;
    fs::path p = fsutil::resolve_path(src);
    std::error_code ec;
    if (!fs::exists(p, ec)) {
      stats.warnings.push_back("source not found: " + src);
      continue;
    }
    if (progress) progress("scan", src_i, total_src, p.string());

    std::vector<fs::path> files;
    if (fs::is_directory(p, ec)) {
      for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
           !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const auto& entry = *it;
        std::error_code fec;
        if (entry.is_directory(fec)) {
          if (fs::exists(entry.path() / ".loom-archive", fec) || skip_name(entry.path().filename().string())) {
            it.disable_recursion_pending();
          }
          continue;
        }
        if (fec || skip_name(entry.path().filename().string())) continue;
        files.push_back(entry.path());
      }
      std::sort(files.begin(), files.end());
    } else {
      files.push_back(p);
    }

    for (const auto& file : files) {
      if (cancel && cancel->cancelled()) break;
      LOOM_TRY_ASSIGN(std::string bytes_hex, sha256_file_hex(file));
      std::string source_id = "sha256:" + bytes_hex;
      std::error_code sec;
      std::int64_t fsize = static_cast<std::int64_t>(fs::file_size(file, sec));
      std::string ext = lower_ext(file);
      LOOM_TRY(register_source(rt_.db(), source_id, file, fsize, ext == ".zip" ? "zip" : "file"));
      if (ext == ".zip") {
        LOOM_TRY(scan_zip_source(rt_.db(), idx, norm, cfg.sketch, source_id, file, cfg.force, stats, cancel));
        continue;
      }
      if (!cfg.force) {
        LOOM_TRY_ASSIGN(bool done, checkpoint_done(rt_.db(), source_id, ""));
        if (done) continue;
      }
      auto reader = [&](const std::function<void(std::string_view)>& on_chunk) { return stream_file_chunks(file, on_chunk); };
      Status st = (ext == ".json" || ext == ".jsonl")
                      ? scan_json_stream(rt_.db(), idx, norm, cfg.sketch, source_id, "", reader, stats)
                      : scan_whole(rt_.db(), idx, norm, cfg.sketch, source_id, "", reader, stats);
      if (!st) {
        stats.warnings.push_back(file.string() + ": " + st.error().message);
        continue;
      }
      LOOM_TRY(mark_checkpoint(rt_.db(), source_id, "", true));
    }
  }
  return stats.to_json();
}

}  // namespace loom::catalog
