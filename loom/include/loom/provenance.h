// loom/provenance.h — content-addressed blobs, sources, provenance, artifacts,
// and the append-only event log (MEGA MASTER 2.F "raw source is immutable",
// 2.G "everything resumable and auditable", 4.2 Provenance/Event/Blob layer).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Database;

// ── BlobStore ───────────────────────────────────────────────────────
// Files live at <root>/<h[0:2]>/<h[2:4]>/<sha256-hex>. A blob is written to a
// temp file while hashing, renamed into place, and chmod 444; an existing
// blob is never rewritten (dedup by hash). Every blob is also registered in
// loom_blobs (hash, size, mime, created).
struct BlobRef {
  std::string hash;  // lowercase sha256 hex
  std::int64_t size = 0;
  bool existed = false;  // true when the content was already stored
  Json to_json() const;
};

class BlobStore {
 public:
  BlobStore(std::filesystem::path root, Database& db);

  Result<BlobRef> put(std::string_view bytes, std::string_view mime = "");
  // Streams the file (constant memory). The source file is left untouched.
  Result<BlobRef> put_file(const std::filesystem::path& file, std::string_view mime = "");

  bool has(std::string_view hash) const;
  Result<std::string> read(std::string_view hash) const;
  std::filesystem::path path_for(std::string_view hash) const;
  // Recomputes the hash of the stored file; Errc::Conflict on mismatch.
  Status verify(std::string_view hash) const;
  const std::filesystem::path& root() const noexcept { return root_; }

  static bool is_valid_hash(std::string_view hash) noexcept;

 private:
  Status register_blob(const BlobRef& ref, std::string_view mime);
  std::filesystem::path root_;
  Database& db_;
};

// ── Sources / provenance / artifacts ────────────────────────────────
struct SourceRecord {
  std::string id;             // src_… (generated when empty)
  std::string kind;           // "file", "zip_member", "url", "api", "github", ...
  std::string uri;            // original path / URL
  std::string blob_hash;      // raw bytes in BlobStore (may be empty for virtual sources)
  std::int64_t size = 0;
  std::string mime;
  std::string format;         // importer format ("json", "zip", ...)
  std::string title;
  std::string parser;         // e.g. "loom.importer.json"
  std::string parser_version;
  std::string imported_at;    // set on insert when empty
  Json metadata = Json::object();
  Json to_json() const;
};

struct ProvenanceRecord {
  std::string id;             // pv_… (generated when empty)
  std::string subject_id;     // conversation / message / node / artifact id
  std::string subject_kind;   // "conversation", "message", "node", "artifact", ...
  std::string source_id;      // may be empty for derived-only records
  Json locator = Json::object();  // where in the source: {"path":..., "index":..., "json_pointer":...}
  std::string transform;      // transformation chain step, e.g. "import.chatgpt_mapping@1"
  double confidence = 1.0;
  std::string created;
  Json to_json() const;
};

struct ArtifactRecord {
  std::string id;             // a_…
  std::string kind;
  std::string title;
  std::string blob_hash;
  std::string mime;
  std::string task_id;
  Json metadata = Json::object();
  std::string created;
  Json to_json() const;
};

class ProvenanceStore {
 public:
  explicit ProvenanceStore(Database& db) : db_(db) {}

  Result<std::string> add_source(SourceRecord rec);
  Result<std::optional<SourceRecord>> get_source(std::string_view id);
  Result<std::vector<SourceRecord>> find_sources_by_hash(std::string_view blob_hash);
  Result<std::vector<SourceRecord>> list_sources(int limit = 100, std::optional<std::string_view> kind = std::nullopt);

  Result<std::string> add(ProvenanceRecord rec);
  // Batch insert in one transaction (importer: one record per message).
  Result<int> add_many(std::vector<ProvenanceRecord> recs);
  Result<std::vector<ProvenanceRecord>> for_subject(std::string_view subject_id);
  Result<std::vector<ProvenanceRecord>> for_source(std::string_view source_id, int limit = 1000);

  Result<std::string> add_artifact(ArtifactRecord rec);
  Result<std::optional<ArtifactRecord>> get_artifact(std::string_view id);
  Result<std::vector<ArtifactRecord>> list_artifacts(int limit = 100,
                                                     std::optional<std::string_view> kind = std::nullopt);

 private:
  Database& db_;
};

// ── EventLog ────────────────────────────────────────────────────────
// Append-only (no update/delete API). seq is strictly increasing.
struct EventRecord {
  std::int64_t seq = 0;
  std::string id;          // ev_…
  std::string ts;          // ISO UTC
  std::string type;
  std::string subject_id;
  Json payload = Json::object();
  std::string input_hash;
  std::string output_hash;
  Json to_json() const;
};

struct EventQuery {
  std::int64_t after_seq = 0;
  std::optional<std::string> type;        // exact, or prefix when ending with '*'
  std::optional<std::string> subject_id;
  int limit = 500;
};

class EventLog {
 public:
  explicit EventLog(Database& db) : db_(db) {}
  // Returns the assigned seq.
  Result<std::int64_t> append(std::string_view type, std::string_view subject_id = "",
                              const Json& payload = Json::object(), std::string_view input_hash = "",
                              std::string_view output_hash = "");
  Result<std::vector<EventRecord>> query(const EventQuery& q = {});
  Result<std::int64_t> last_seq();

 private:
  Database& db_;
};

}  // namespace loom
