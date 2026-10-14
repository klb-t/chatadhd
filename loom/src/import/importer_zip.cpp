// ZIP: extract to a temp dir (miniz) and recursively import_file() every
// importable member, sorted for determinism. Failures of one member are
// logged and skipped, matching engine/importer.py's import_zip().
#include <algorithm>
#include <cstring>
#include <fstream>

#include "importer_internal.h"
#include "import/import_recognition.h"
#include "loom/importer.h"
#include "loom/log.h"
#include "loom/util/fs.h"
#include "loom/util/utf8.h"
#include "util/safety.h"
#include <miniz.h>

namespace loom {

namespace fs = std::filesystem;

namespace {
constexpr std::string_view kLog = "loom.import";

// Rejects absolute paths and ".." components (zip-slip); empty/root entries.
bool safe_zip_relpath(const fs::path& rel) {
  if (rel.empty() || rel.is_absolute()) return false;
  for (const auto& part : rel) {
    if (part == "..") return false;
  }
  return true;
}

}  // namespace

Result<std::vector<Conversation>> ConversationImporter::zip_body(const fs::path& path, const ImportOptions& opts) {
  fsutil::TempDir td("loom_import_");
  if (!td.valid()) return Error(Errc::Io, "could not create a temp dir for zip extraction");

  mz_zip_archive zip;
  std::memset(&zip, 0, sizeof(zip));
  if (!mz_zip_reader_init_file(&zip, path.string().c_str(), 0)) {
    return Error(Errc::Parse, "invalid zip archive: " + path.string());
  }

  // R20: zip bomb guard (loom_zip_max_total_bytes / loom_zip_max_ratio in config.json).
  safety::ZipBudget budget(safety::zip_limits(db_.path().parent_path()));
  struct Entry {
    std::string rel;  // POSIX-style relative path inside the archive
    fs::path abs;      // extracted location on disk
    std::int64_t archive_index;
  };
  std::vector<Entry> entries;
  if (ledger_) ledger_->measured("zip");
  mz_uint n = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < n; ++i) {
    if (cancelled(opts)) break;
    if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
    std::string name = st.m_filename;
    fs::path relp(name);
    if (!safe_zip_relpath(relp)) {
      log::warn(kLog, "zip: skipping unsafe entry path {}", name);
      // Not extracted (zip-slip), but never silently dropped: recorded as opaque.
      if (ledger_) {
        FragmentRecord record;
        record.status = std::string(recognition::kOpaque);
        record.recognizer = "loom.importer.zip";
        record.recognizer_version = std::string(kImporterParserVersion);
        record.reason = "unsafe archive path; not extracted";
        record.label = "ZIP member " + name;
        record.locator = Json{{"member", name}, {"archive_index", static_cast<std::int64_t>(i)}};
        record.bytes_available = false;
        record.declared_size = static_cast<std::int64_t>(st.m_uncomp_size);
        auto kept = keep_fragment(std::move(record));
        if (!kept) { mz_zip_reader_end(&zip); return kept.error(); }
      }
      continue;
    }
    fs::path dest = td.path() / std::to_string(i) / relp;
    std::error_code ec;
    if (auto admitted = budget.admit(st); !admitted) { mz_zip_reader_end(&zip); return admitted.error(); }
    fs::create_directories(dest.parent_path(), ec);
    if (auto extracted = budget.extract_to_file(zip, i, dest); !extracted) {
      if (budget.refused()) { mz_zip_reader_end(&zip); return extracted.error(); }
      log::warn(kLog, "zip: failed to extract {}", name);
      if (ledger_) {
        FragmentRecord record;
        record.status = std::string(recognition::kOpaque);
        record.recognizer = "loom.importer.zip";
        record.recognizer_version = std::string(kImporterParserVersion);
        record.reason = "entry could not be extracted (corrupt?)";
        record.label = "ZIP member " + name;
        record.locator = Json{{"member", name}, {"archive_index", static_cast<std::int64_t>(i)}};
        record.bytes_available = false;
        record.declared_size = static_cast<std::int64_t>(st.m_uncomp_size);
        auto kept = keep_fragment(std::move(record));
        if (!kept) { mz_zip_reader_end(&zip); return kept.error(); }
      }
      continue;
    }
    auto materialized = materialize_zip_member(dest, opts, name, i);
    if (!materialized) { mz_zip_reader_end(&zip); return materialized.error(); }
    entries.push_back(Entry{name, dest, i});
  }
  mz_zip_reader_end(&zip);

  std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.rel < b.rel; });

  std::vector<std::string> importable_rels;
  for (const auto& e : entries) {
    if (detect_format(e.abs) != "unknown") importable_rels.push_back(e.rel);
  }
  log::info(kLog, "ZIP contains {} importable files out of {} total", importable_rels.size(), entries.size());

  // R44: a member that cannot be imported is kept verbatim, annotated.
  auto keep_member = [&](const Entry& e, std::string_view status, std::string reason) -> Status {
    if (!ledger_) return {};
    FragmentRecord record;
    record.status = std::string(status);
    record.recognizer = "loom.importer.zip";
    record.recognizer_version = std::string(kImporterParserVersion);
    record.reason = std::move(reason);
    record.label = "ZIP member " + e.rel;
    record.locator = Json{{"member", e.rel}, {"archive_index", e.archive_index}};
    record.file = e.abs;
    record.attempts = Json::array({Json{{"recognizer", "loom.importer.zip"},
                                         {"version", std::string(kImporterParserVersion)},
                                         {"outcome", std::string(status)}, {"reason", record.reason}}});
    auto kept = keep_fragment(std::move(record));
    if (!kept) return kept.error();
    return {};
  };

  std::vector<Conversation> results;
  std::int64_t done = 0;
  for (const auto& e : entries) {
    if (cancelled(opts)) break;
    if (detect_format(e.abs) == "unknown") {
      const bool text = file_is_text(e.abs, opts.json_read_chunk_bytes);
      LOOM_TRY(keep_member(e, text ? recognition::kUnrecognized : recognition::kOpaque,
                           text ? "no importer recognises this member's format" : "binary member; no importer recognises its format"));
      continue;
    }

    ImportOptions member_opts;
    member_opts.title = opts.title.has_value() ? opts.title : std::optional<std::string>(e.rel);
    member_opts.progress = nullptr;  // per-member progress would double-report; only the aggregate below fires
    member_opts.cancel = opts.cancel;
    member_opts.record_provenance = opts.record_provenance;
    member_opts.force = opts.force;
    member_opts.stream_threshold_bytes = opts.stream_threshold_bytes;

    auto r = import_file_as(e.abs, member_opts, "zip_member", e.rel, e.archive_index);
    ++done;
    if (opts.progress) opts.progress(done, static_cast<std::int64_t>(importable_rels.size()), "zip");
    if (!r) {
      log::warn(kLog, "Failed to import {} from ZIP: {}", e.rel, r.error().message);
      LOOM_TRY(keep_member(e, r.error().code == Errc::Parse ? recognition::kOpaque : recognition::kUnrecognized,
                           "import failed: " + r.error().message));
      continue;
    }
    for (auto& c : r->conversations) results.push_back(std::move(c));
  }
  return results;
}

Result<std::vector<Conversation>> ConversationImporter::import_zip(const fs::path& path, const ImportOptions& opts) {
  return with_source(path, "zip", opts, "file", [&](const fs::path& input_path) { return zip_body(input_path, opts); });
}

}  // namespace loom
