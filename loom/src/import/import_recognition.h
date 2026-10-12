// R44 (unrecognised input enters the graph, annotated) and R50 (duplicates are
// allowed, flagged, linking proposed) for the importer.
//
// One ImportLedger lives for one outermost import call. Handlers hand it the
// verbatim bytes of whatever they could not (fully) map; it stores them as
// graph nodes carrying a recognition annotation and a source locator, counts
// what it saw per status, and keeps the duplicate report. It never refuses
// anything: policy only decides whether a duplicate is linked, reused or just
// reported.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "loom/db.h"
#include "loom/importer.h"
#include "loom/provenance.h"

namespace loom {

namespace recognition {
// Recognition statuses (GRAPH_ENTRY_PARTIES_ECONOMY_2026-10-10 section 2).
inline constexpr std::string_view kRecognized = "recognized";
inline constexpr std::string_view kPartial = "partial";
inline constexpr std::string_view kUnrecognized = "unrecognized";
inline constexpr std::string_view kOpaque = "opaque";
inline constexpr std::string_view kExcludedByUser = "excluded_by_user";
inline constexpr std::string_view kNodeKind = "import:unrecognized";
inline constexpr std::string_view kConversationRefKind = "import:conversation_ref";
}  // namespace recognition

// One piece of input a handler could not (fully) map.
struct FragmentRecord {
  std::string status;             // recognition:: status (partial/unrecognized/opaque)
  std::string recognizer;         // e.g. "loom.importer.json"
  std::string recognizer_version;
  std::string reason;
  std::string label;              // node label (human readable locator)
  Json locator = Json::object();  // json_pointer / member / archive_index / line / byte_start / byte_end ...
  std::string source_id;          // loom_sources id ("" when provenance is off)
  std::string source_blob_hash;   // blob of the containing source file
  std::string conversation_id;    // partial: the conversation the mapped part became
  std::string_view bytes;         // verbatim content
  // Alternative to `bytes` for large members: the content is read from this
  // file (hashed and, past the inline threshold, stored by streaming).
  std::filesystem::path file;
  // False when the content could not be obtained at all (unsafe path, corrupt
  // entry): the node records only what is known (declared size), no sha256.
  bool bytes_available = true;
  std::int64_t declared_size = -1;
  Json attempts = Json::array();  // earlier recognition attempts (history)
};

// No NUL byte and valid UTF-8 across the whole file (chunk-safe).
bool file_is_text(const std::filesystem::path& path, std::size_t chunk_bytes);

class ImportLedger {
 public:
  ImportLedger(Database& db, BlobStore* blobs, ProvenanceStore* prov, const ImportOptions& opts);

  const std::string& policy() const noexcept { return policy_; }
  bool reuse_prior() const noexcept { return policy_ == kDuplicateReusePrior; }
  bool link_duplicates() const noexcept { return policy_ == kDuplicateReimportAndLink; }

  // ── R44 ──────────────────────────────────────────────────────────
  // Stores the fragment as a node. Returns the node id (the prior node under
  // reuse_prior when identical bytes were already kept).
  Result<std::string> keep(const FragmentRecord& record);
  // A handler that classifies its input marks the format as measured; counts
  // are reported only when something was measured.
  void measured(std::string_view format);
  void note_conversation();  // one conversation record produced
  // A kept record that lives in a node created elsewhere (export:member).
  void note_kept(std::string_view status, const std::string& node_id);
  // Fragments/members kept so far (any status).
  std::int64_t kept_total() const noexcept;

  // ── R50 ──────────────────────────────────────────────────────────
  struct ConversationCheck {
    std::string fingerprint;
    std::optional<std::string> prior;  // existing conversation id with the same fingerprint
  };
  // external_id may be empty. Messages are (role, text); text is whitespace
  // normalised before hashing.
  Result<ConversationCheck> check_conversation(std::string_view external_id,
                                               const std::vector<std::pair<std::string, std::string>>& messages);
  // Called once the conversation exists: registers the fingerprint and, for a
  // duplicate, links/reports according to the policy.
  Status conversation_created(const ConversationCheck& check, const Conversation& created, const std::string& source_id);
  // reuse_prior: nothing was created; records this source against `prior`.
  Status conversation_reused(const ConversationCheck& check, const std::string& prior, const std::string& source_id);
  void note_source_duplicate(Json item);

  // Fills result.recognition / result.duplicates.
  void finish(ImportResult& result) const;

 private:
  Status ensure_table();
  Result<std::optional<std::string>> find_prior(std::string_view fingerprint, std::string_view subject_kind,
                                                std::string_view exclude_subject);
  Status register_subject(std::string_view fingerprint, std::string_view subject_id, std::string_view subject_kind,
                          std::string_view source_id);
  Result<std::string> conversation_ref(const std::string& conversation_id);
  void add_duplicate(Json item);

  Database& db_;
  BlobStore* blobs_;
  ProvenanceStore* prov_;
  std::string policy_;
  std::int64_t inline_threshold_;
  bool include_items_;
  bool table_ready_ = false;
  std::set<std::string> measured_;
  std::map<std::string, std::int64_t> counts_;
  std::int64_t conversations_ = 0;
  std::int64_t partial_conversations_ = 0;
  std::vector<std::string> node_ids_;
  std::vector<Json> duplicates_;
  std::int64_t duplicate_count_ = 0;
};

}  // namespace loom
