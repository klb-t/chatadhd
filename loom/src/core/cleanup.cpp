#include "loom/cleanup.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "loom/log.h"
#include "loom/util/time.h"

namespace loom {

namespace fs = std::filesystem;

namespace {

constexpr const char* kLog = "loom.cleanup";
constexpr std::size_t kLogged = 50;  // hashes / files listed in the report and the log

bool is_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

// "up_" + 12 hex + ".part": what client.blob_begin hands out (client_blobs.cpp).
bool is_upload_part(const std::string& name) {
  if (name.size() != 20 || name.rfind("up_", 0) != 0 || name.compare(15, 5, ".part") != 0) return false;
  return std::all_of(name.begin() + 3, name.begin() + 15, [](unsigned char c) { return std::isxdigit(c) != 0; });
}

// Marks every candidate hash that occurs in `text` (any 64-hex window of a hex run, case-insensitive).
void mark_named(const std::string& text, std::unordered_set<std::string>& candidates, std::unordered_set<std::string>& named) {
  std::size_t i = 0;
  while (i < text.size()) {
    if (!is_hex(text[i])) { ++i; continue; }
    std::size_t j = i;
    while (j < text.size() && is_hex(text[j])) ++j;
    if (j - i >= 64)
      for (std::size_t k = i; k + 64 <= j; ++k) {
        std::string w = text.substr(k, 64);
        std::transform(w.begin(), w.end(), w.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (candidates.count(w)) named.insert(std::move(w));
      }
    i = j;
  }
}

Result<double> number_of(const Json& obj, const char* key, double fallback) {
  const Json* v = json::find(obj, key);
  if (!v || v->is_null()) return fallback;
  if (!v->is_number() || v->get<double>() < 0 || !std::isfinite(v->get<double>()))
    return Error(Errc::InvalidArgument, std::string("cleanup.") + key + " must be a number >= 0");
  return v->get<double>();
}

Json upload_pass(const fs::path& data_root, double max_age_hours) {
  Json out{{"removed", 0}, {"bytes", 0}, {"files", Json::array()}};
  if (max_age_hours <= 0) { out["status"] = "off"; return out; }
  std::error_code ec;
  const fs::path dir = data_root / "tmp" / "uploads";
  if (!fs::is_directory(dir, ec)) return out;
  const auto limit = std::chrono::duration_cast<fs::file_time_type::duration>(
      std::chrono::duration<double, std::ratio<3600>>(max_age_hours));
  const auto now = fs::file_time_type::clock::now();
  std::int64_t removed = 0, bytes = 0;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (!is_upload_part(name)) continue;
    std::error_code e;
    if (!it->is_regular_file(e) || it->is_symlink(e)) continue;
    const auto mtime = fs::last_write_time(it->path(), e);
    if (e || now - mtime < limit) continue;
    const auto size = fs::file_size(it->path(), e);
    if (!fs::remove(it->path(), e) || e) {
      log::warn(kLog, "cannot remove abandoned upload {}: {}", name, e.message());
      continue;
    }
    ++removed;
    bytes += static_cast<std::int64_t>(size);
    if (out["files"].size() < kLogged) out["files"].push_back(name);
  }
  out["removed"] = removed;
  out["bytes"] = bytes;
  if (removed) log::info(kLog, "removed {} abandoned upload part(s) ({} bytes) older than {} h", removed, bytes, max_age_hours);
  return out;
}

Result<Json> blob_pass(Database& db, BlobStore& blobs, const fs::path& data_root, const CleanupOptions& o, const std::string& now_iso) {
  Json out{{"status", "off"}, {"candidates", 0}, {"bytes", 0}, {"removed", 0}, {"hashes", Json::array()}};
  if (o.orphan_blob_max_age_days <= 0) return out;
  const auto now = timeutil::parse_iso_utc(now_iso.empty() ? timeutil::utc_now_iso() : now_iso);
  if (!now) return Error(Errc::InvalidArgument, "now_iso is not an ISO timestamp");
  const auto limit = std::chrono::duration_cast<timeutil::Clock::duration>(
      std::chrono::duration<double, std::ratio<86400>>(o.orphan_blob_max_age_days));

  auto lock = db.lock();
  sql::Connection& c = db.conn();
  std::unordered_set<std::string> candidates;
  std::unordered_map<std::string, std::int64_t> sizes;
  {
    LOOM_TRY_ASSIGN(sql::Stmt st, c.prepare("SELECT hash, size, created FROM loom_blobs"));
    for (;;) {
      LOOM_TRY_ASSIGN(bool row, st.step());
      if (!row) break;
      const auto created = timeutil::parse_iso_utc(st.get_text(2));
      if (!created || *now - *created < limit) continue;  // unreadable or young: not a candidate
      std::string h = st.get_text(0);
      std::transform(h.begin(), h.end(), h.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      sizes[h] = st.get_int(1);
      candidates.insert(std::move(h));
    }
  }
  std::unordered_set<std::string> named;
  if (!candidates.empty()) {
    // Every text of every table (except the blob register itself) and the JSON files of the data directory root.
    std::vector<std::string> tables;
    {
      LOOM_TRY_ASSIGN(sql::Stmt st, c.prepare("SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' AND name <> 'loom_blobs'"));
      for (;;) {
        LOOM_TRY_ASSIGN(bool row, st.step());
        if (!row) break;
        tables.push_back(st.get_text(0));
      }
    }
    for (const auto& t : tables) {
      const auto cols = c.columns(t);
      if (cols.empty()) continue;
      std::string sql = "SELECT ";
      for (std::size_t i = 0; i < cols.size(); ++i) sql += (i ? ", " : "") + std::string("CAST(\"") + cols[i] + "\" AS TEXT)";
      sql += " FROM \"" + t + "\"";
      auto st = c.prepare(sql);
      if (!st) {  // an unreadable table could name a blob: refuse to delete anything
        return Error(Errc::Internal, "cleanup: cannot read table " + t + ": " + st.error().message);
      }
      for (;;) {
        LOOM_TRY_ASSIGN(bool row, st->step());
        if (!row) break;
        for (int i = 0; i < static_cast<int>(cols.size()); ++i)
          if (!st->is_null(i)) mark_named(st->get_text(i), candidates, named);
      }
    }
    std::error_code ec;
    for (fs::directory_iterator it(data_root, ec), end; !ec && it != end; it.increment(ec)) {
      if (it->path().extension() != ".json") continue;
      std::error_code e;
      if (!it->is_regular_file(e)) continue;
      std::ifstream in(it->path(), std::ios::binary);
      std::stringstream ss;
      ss << in.rdbuf();
      mark_named(ss.str(), candidates, named);
    }
  }
  std::vector<std::string> orphans;
  for (const auto& h : candidates)
    if (!named.count(h)) orphans.push_back(h);
  std::sort(orphans.begin(), orphans.end());
  std::int64_t bytes = 0;
  for (const auto& h : orphans) bytes += sizes[h];
  out["candidates"] = static_cast<std::int64_t>(orphans.size());
  out["bytes"] = bytes;
  for (std::size_t i = 0; i < orphans.size() && i < kLogged; ++i) out["hashes"].push_back(orphans[i]);
  if (o.orphan_blob_dry_run) {
    out["status"] = "dry_run";
    log::info(kLog, "dry run: {} unreferenced blob(s) older than {} days ({} bytes) would be removed (cleanup.orphan_blob_dry_run=false deletes)",
              orphans.size(), o.orphan_blob_max_age_days, bytes);
    for (std::size_t i = 0; i < orphans.size() && i < kLogged; ++i) log::info(kLog, "  candidate blob {}", orphans[i]);
    return out;
  }
  out["status"] = "deleted";
  std::int64_t removed = 0;
  for (const auto& h : orphans) {
    std::error_code ec;
    const fs::path file = blobs.path_for(h);
    if (fs::exists(file, ec)) {
      fs::permissions(file, fs::perms::owner_write, fs::perm_options::add, ec);
      if (!fs::remove(file, ec) || ec) {
        log::warn(kLog, "cannot remove blob {}: {}", h, ec.message());
        continue;  // the register row stays with its file
      }
    }
    if (auto s = c.run("DELETE FROM loom_blobs WHERE hash = ?", h); !s) {
      log::warn(kLog, "cannot unregister blob {}: {}", h, s.error().message);
      continue;
    }
    ++removed;
  }
  out["removed"] = removed;
  log::info(kLog, "removed {} unreferenced blob(s) older than {} days", removed, o.orphan_blob_max_age_days);
  return out;
}

}  // namespace

Result<CleanupOptions> cleanup_options(const Config& config) {
  CleanupOptions o;
  const Json cfg = config.get("cleanup", Json::object());
  if (cfg.is_null()) return o;
  if (!cfg.is_object()) return Error(Errc::InvalidArgument, "cleanup must be an object");
  LOOM_TRY_ASSIGN(o.upload_part_max_age_hours, number_of(cfg, "upload_part_max_age_hours", o.upload_part_max_age_hours));
  LOOM_TRY_ASSIGN(o.orphan_blob_max_age_days, number_of(cfg, "orphan_blob_max_age_days", o.orphan_blob_max_age_days));
  if (const Json* d = json::find(cfg, "orphan_blob_dry_run"); d && !d->is_null()) {
    if (!d->is_boolean()) return Error(Errc::InvalidArgument, "cleanup.orphan_blob_dry_run must be true or false");
    o.orphan_blob_dry_run = d->get<bool>();
  }
  return o;
}

Result<Json> run_cleanup(Database& db, BlobStore& blobs, const fs::path& data_root, const CleanupOptions& options, const std::string& now_iso) {
  Json report{{"schema", "loom.cleanup/1"}, {"upload_parts", upload_pass(data_root, options.upload_part_max_age_hours)}};
  auto blob = blob_pass(db, blobs, data_root, options, now_iso);
  if (!blob) {
    log::warn(kLog, "blob cleanup skipped: {}", blob.error().message);
    report["orphan_blobs"] = Json{{"status", "error"}, {"message", blob.error().message}};
  } else {
    report["orphan_blobs"] = *blob;
  }
  return report;
}

}  // namespace loom
