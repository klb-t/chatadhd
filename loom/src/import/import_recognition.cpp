#include "import/import_recognition.h"

#include <fstream>

#include "importer_internal.h"
#include "loom/log.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom {

namespace {
constexpr std::string_view kLog = "loom.import";
constexpr std::string_view kFragmentKind = "node";
constexpr std::string_view kConversationKind = "conversation";

Json proposal(const std::string& prior, const std::string& created) {
  // The proposal is data for the caller; nothing is applied by the engine.
  return Json{{"action", "pin_immutable_data"}, {"subjects", Json::array({prior, created})}};
}
}  // namespace

ImportLedger::ImportLedger(Database& db, BlobStore* blobs, ProvenanceStore* prov, const ImportOptions& opts)
    : db_(db), blobs_(blobs), prov_(prov),
      policy_(is_duplicate_policy(opts.duplicate_policy) ? opts.duplicate_policy : default_duplicate_policy()),
      inline_threshold_(opts.json_inline_threshold_bytes), include_items_(opts.include_result_metadata) {}

Status ImportLedger::ensure_table() {
  if (table_ready_) return {};
  auto lock = db_.lock();
  LOOM_TRY(db_.conn().exec(
      "CREATE TABLE IF NOT EXISTS loom_import_fingerprints ("
      "fingerprint TEXT NOT NULL, subject_id TEXT NOT NULL, subject_kind TEXT NOT NULL, "
      "source_id TEXT, created TEXT NOT NULL, PRIMARY KEY(fingerprint, subject_id))"));
  table_ready_ = true;
  return {};
}

Result<std::optional<std::string>> ImportLedger::find_prior(std::string_view fingerprint,
                                                            std::string_view subject_kind,
                                                            std::string_view exclude_subject) {
  auto lock = db_.lock();
  LOOM_TRY_ASSIGN(auto stmt, db_.conn().prepare(
      "SELECT subject_id FROM loom_import_fingerprints WHERE fingerprint=? AND subject_kind=? ORDER BY rowid"));
  stmt.bind(1, fingerprint);
  stmt.bind(2, subject_kind);
  std::vector<std::string> candidates;
  for (;;) {
    LOOM_TRY_ASSIGN(bool row, stmt.step());
    if (!row) break;
    candidates.push_back(stmt.get_text(0));
  }
  for (const auto& id : candidates) {
    if (id == exclude_subject) continue;
    if (subject_kind == kConversationKind) {
      LOOM_TRY_ASSIGN(auto conv, db_.get_conv(id));
      if (conv) return std::optional<std::string>(id);
    } else {
      LOOM_TRY_ASSIGN(auto node, db_.get_node(id));
      if (node) return std::optional<std::string>(id);
    }
  }
  return std::optional<std::string>{};
}

Status ImportLedger::register_subject(std::string_view fingerprint, std::string_view subject_id,
                                      std::string_view subject_kind, std::string_view source_id) {
  auto lock = db_.lock();
  return db_.conn().run(
      "INSERT OR IGNORE INTO loom_import_fingerprints(fingerprint, subject_id, subject_kind, source_id, created) "
      "VALUES(?,?,?,?,?)",
      fingerprint, subject_id, subject_kind, source_id, timeutil::utc_now_iso());
}

void ImportLedger::measured(std::string_view format) { measured_.insert(std::string(format)); }

void ImportLedger::note_conversation() { ++conversations_; }

void ImportLedger::add_duplicate(Json item) {
  ++duplicate_count_;
  if (include_items_) duplicates_.push_back(std::move(item));
}

void ImportLedger::note_source_duplicate(Json item) {
  item["level"] = "source";
  add_duplicate(std::move(item));
}

// ── R44 ─────────────────────────────────────────────────────────────
std::int64_t ImportLedger::kept_total() const noexcept {
  std::int64_t total = 0;
  for (const auto& [status, count] : counts_) total += count;
  return total;
}

bool file_is_text(const std::filesystem::path& path, std::size_t chunk_bytes) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::string buffer(chunk_bytes ? chunk_bytes : 1, '\0');
  std::string carry;
  while (in) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto n = static_cast<std::size_t>(in.gcount());
    if (!n) break;
    std::string window = carry + std::string(buffer.data(), n);
    if (window.find('\0') != std::string::npos) return false;
    // Hold back an incomplete trailing multi-byte sequence for the next chunk.
    std::size_t cut = window.size();
    for (std::size_t back = 1; back <= 3 && back <= window.size(); ++back) {
      const unsigned char c = static_cast<unsigned char>(window[window.size() - back]);
      if ((c & 0xC0) == 0x80) continue;
      if (c >= 0xC0) {
        const std::size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
        if (need > back) cut = window.size() - back;
      }
      break;
    }
    if (!utf8::is_valid(std::string_view(window).substr(0, cut))) return false;
    carry = window.substr(cut);
  }
  return carry.empty();
}

void ImportLedger::note_kept(std::string_view status, const std::string& node_id) {
  ++counts_[std::string(status)];
  if (!node_id.empty()) node_ids_.push_back(node_id);
}

Result<std::string> ImportLedger::keep(const FragmentRecord& r) {
  LOOM_TRY(ensure_table());
  ++counts_[r.status];
  if (!r.conversation_id.empty() && r.status == recognition::kPartial) ++partial_conversations_;

  Json recognition = Json{{"status", r.status},
                          {"recognizer", r.recognizer},
                          {"recognizer_version", r.recognizer_version},
                          {"reason", r.reason},
                          {"attempts", r.attempts}};
  NodeOptions options;
  std::string sha;
  std::string fingerprint;
  std::optional<std::string> prior;

  std::string loaded;
  std::string_view bytes = r.bytes;
  if (!r.bytes_available) {
    // Known: what the container declared. Unknown: the bytes, hence no hash.
    recognition["stored"] = "none";
    if (r.declared_size >= 0) recognition["declared_size"] = r.declared_size;
  } else {
    std::int64_t size = static_cast<std::int64_t>(bytes.size());
    bool text = false;
    bool text_known = true;
    if (!r.file.empty()) {
      LOOM_TRY_ASSIGN(sha, sha256_file_hex(r.file));
      std::error_code ec;
      size = static_cast<std::int64_t>(std::filesystem::file_size(r.file, ec));
      if (ec) return Error(Errc::Io, "cannot stat " + r.file.string());
      if (size <= inline_threshold_) {
        LOOM_TRY_ASSIGN(loaded, fsutil::read_file(r.file));
        bytes = loaded;
        text = utf8::is_valid(bytes);
      } else {
        text_known = false;
      }
    } else {
      sha = Sha256::hex(bytes);
      text = utf8::is_valid(bytes);
    }
    fingerprint = "fragment:" + sha;
    LOOM_TRY_ASSIGN(prior, find_prior(fingerprint, kFragmentKind, ""));

    if (prior && reuse_prior()) {
      // No new copy: the prior node gains this occurrence as provenance.
      if (prov_ && !r.source_id.empty()) {
        ProvenanceRecord occurrence;
        occurrence.subject_id = *prior;
        occurrence.subject_kind = "node";
        occurrence.source_id = r.source_id;
        occurrence.locator = r.locator;
        occurrence.transform = "import.reuse_prior@" + std::string(kImporterParserVersion);
        if (auto added = prov_->add(std::move(occurrence)); !added)
          log::warn(kLog, "provenance of reused fragment: {}", added.error().message);
      }
      add_duplicate(Json{{"level", "fragment"}, {"node_id", *prior}, {"prior_id", *prior}, {"sha256", sha},
                         {"status", r.status}, {"locator", r.locator}, {"action", "reused"},
                         {"proposal", proposal(*prior, *prior)}});
      return *prior;
    }

    recognition["sha256"] = sha;
    recognition["size"] = size;
    if (text_known) recognition["valid_utf8"] = text;
    if (text_known && text && size <= inline_threshold_) {
      options.content = std::string(bytes);
      recognition["stored"] = "inline";
    } else if (blobs_) {
      // The threshold decides where bytes live, never whether they are kept.
      Result<BlobRef> blob = r.file.empty() ? blobs_->put(bytes, "application/octet-stream")
                                            : blobs_->put_file(r.file, "application/octet-stream");
      if (!blob) return blob.error();
      recognition["stored"] = "blob";
      recognition["blob_hash"] = blob->hash;
    } else if (!r.file.empty()) {
      LOOM_TRY_ASSIGN(loaded, fsutil::read_file(r.file));
      if (utf8::is_valid(loaded)) {
        options.content = loaded;
        recognition["stored"] = "inline";
      } else {
        recognition["stored"] = "metadata_base64";
        recognition["bytes_base64"] = base64::encode(loaded);
      }
      recognition["no_blob_store"] = true;
    } else if (text) {
      options.content = std::string(bytes);
      recognition["stored"] = "inline";
      recognition["no_blob_store"] = true;
    } else {
      recognition["stored"] = "metadata_base64";
      recognition["bytes_base64"] = base64::encode(bytes);
    }
  }
  Json source = Json{{"source_id", r.source_id}, {"blob_hash", r.source_blob_hash}, {"locator", r.locator}};
  if (!r.conversation_id.empty()) source["conversation_id"] = r.conversation_id;
  options.metadata = Json{{"recognition", recognition}, {"source", source}};

  LOOM_TRY_ASSIGN(auto node, db_.create_node(r.label, recognition::kNodeKind, options));
  node_ids_.push_back(node);
  if (!fingerprint.empty()) { LOOM_TRY(register_subject(fingerprint, node, kFragmentKind, r.source_id)); }

  if (prov_ && !r.source_id.empty()) {
    ProvenanceRecord provenance;
    provenance.subject_id = node;
    provenance.subject_kind = "node";
    provenance.source_id = r.source_id;
    provenance.locator = r.locator;
    provenance.transform = "import.recognition." + r.status + "@" + r.recognizer_version;
    if (auto added = prov_->add(std::move(provenance)); !added)
      log::warn(kLog, "provenance of unrecognised fragment: {}", added.error().message);
  }

  if (prior) {
    std::string action = "none";
    if (link_duplicates()) {
      auto linked = db_.create_link(node, *prior, "derived_from", 1.0,
                                    Json{{"relation", "duplicate_of"}, {"level", "fragment"}, {"sha256", sha}});
      if (!linked) return linked.error();
      action = "linked";
    }
    add_duplicate(Json{{"level", "fragment"}, {"node_id", node}, {"prior_id", *prior}, {"sha256", sha},
                       {"status", r.status}, {"locator", r.locator}, {"action", action},
                       {"proposal", proposal(*prior, node)}});
  }
  return node;
}

// ── R50 ─────────────────────────────────────────────────────────────
Result<ImportLedger::ConversationCheck> ImportLedger::check_conversation(
    std::string_view external_id, const std::vector<std::pair<std::string, std::string>>& messages) {
  LOOM_TRY(ensure_table());
  std::string canonical(external_id);
  canonical += '\n';
  for (const auto& [role, text] : messages) {
    canonical += role;
    canonical += '\x1f';
    canonical += importer_detail::collapse_ws(text);
    canonical += '\x1e';
  }
  ConversationCheck check;
  check.fingerprint = "conversation:" + Sha256::hex(canonical);
  LOOM_TRY_ASSIGN(check.prior, find_prior(check.fingerprint, kConversationKind, ""));
  return check;
}

Result<std::string> ImportLedger::conversation_ref(const std::string& conversation_id) {
  // Deterministic id (n_ + 12 hex) so the same conversation always resolves to
  // the same endpoint; conversations are not nodes themselves.
  NodeOptions options;
  options.node_id = "n_" + Sha256::hex("conversation_ref:" + conversation_id).substr(0, 12);
  options.metadata = Json{{"conversation_id", conversation_id}};
  auto title = db_.get_conv(conversation_id);
  std::string label = title && *title ? (*title)->title : conversation_id;
  return db_.create_node(label, recognition::kConversationRefKind, options);
}

Status ImportLedger::conversation_created(const ConversationCheck& check, const Conversation& created,
                                          const std::string& source_id) {
  LOOM_TRY(register_subject(check.fingerprint, created.id, kConversationKind, source_id));
  if (!check.prior) return {};
  std::string action = "none";
  if (link_duplicates()) {
    LOOM_TRY_ASSIGN(auto created_ref, conversation_ref(created.id));
    LOOM_TRY_ASSIGN(auto prior_ref, conversation_ref(*check.prior));
    auto linked = db_.create_link(created_ref, prior_ref, "derived_from", 1.0,
                                  Json{{"relation", "duplicate_of"}, {"level", "conversation"},
                                       {"fingerprint", check.fingerprint}});
    if (!linked) return linked.error();
    action = "linked";
  }
  add_duplicate(Json{{"level", "conversation"}, {"conversation_id", created.id},
                     {"prior_conversation_id", *check.prior}, {"fingerprint", check.fingerprint},
                     {"action", action}, {"proposal", proposal(*check.prior, created.id)}});
  return {};
}

Status ImportLedger::conversation_reused(const ConversationCheck& check, const std::string& prior,
                                         const std::string& source_id) {
  if (prov_ && !source_id.empty()) {
    ProvenanceRecord record;
    record.subject_id = prior;
    record.subject_kind = "conversation";
    record.source_id = source_id;
    record.transform = "import.reuse_prior@" + std::string(kImporterParserVersion);
    if (auto added = prov_->add(std::move(record)); !added)
      log::warn(kLog, "provenance of reused conversation: {}", added.error().message);
  }
  add_duplicate(Json{{"level", "conversation"}, {"conversation_id", prior}, {"prior_conversation_id", prior},
                     {"fingerprint", check.fingerprint}, {"action", "reused"},
                     {"proposal", proposal(prior, prior)}});
  return {};
}

void ImportLedger::finish(ImportResult& result) const {
  if (!measured_.empty()) {
    Json counts = Json::object();
    auto count_of = [&](const std::string& status) {
      auto it = counts_.find(status);
      return it == counts_.end() ? std::int64_t{0} : it->second;
    };
    const std::int64_t recognized = conversations_ - partial_conversations_;
    counts[std::string(recognition::kRecognized)] = recognized < 0 ? 0 : recognized;
    for (auto status : {recognition::kPartial, recognition::kUnrecognized, recognition::kOpaque})
      counts[std::string(status)] = count_of(std::string(status));
    Json formats = Json::array();
    for (const auto& format : measured_) formats.push_back(format);
    result.recognition = Json{{"counts", counts}, {"measured_formats", formats},
                              {"basis", "conversation records and kept fragments"}};
    if (include_items_) result.recognition["nodes"] = node_ids_;
  }
  if (duplicate_count_ > 0) {
    result.duplicates = Json{{"policy", policy_}, {"count", duplicate_count_}};
    if (include_items_) result.duplicates["items"] = duplicates_;
  }
}

}  // namespace loom
