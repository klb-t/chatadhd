// Internal declarations for the lossless provider-export importer
// (OpenAI/ChatGPT + Anthropic/Claude data exports). Not a public contract.
//
// Storage rules (no schema change; schema_version stays "4"):
//   conversations.source   "import:openai" | "import:anthropic" | "import:unknown"
//   conversations.metadata {"export": {provider, schema:"loom.export.v1", member, index, key, title_original,
//                            title_source, fields:{every top-level key except mapping/chat_messages, verbatim},
//                            null_nodes:[...], graph:{...}, current_node_resolved, ...}}
//   messages.role          author.role / sender verbatim ("user","assistant","system","tool"; Claude human->user)
//   messages.status        active | excluded (hidden plumbing / unreachable) | version (alternate branch)
//   messages.version_group_id/version_num   siblings under one parent (edits / regenerations)
//   messages.parent_id     nearest message ancestor in the provider tree
//   messages.attachments   JSON array of readable locations (blob path or zip member) of *resolved* files
//   messages.metadata      {"export": {provider, key, node, raw (the complete original message object),
//                            blocks[], attachments[], pointers[], citations[], citation_groups[], flags...}}
//   export-2 additions     source_index is an original array position (never DFS/time order);
//                          traversal_index is the parser's graph walk; source_key addresses OpenAI mapping.
//                          json_pointer is RFC 6901 relative to the complete JSON member/file;
//                          wrapper_fields keeps siblings of a wrapper's conversations array.
//   export-3 additions     ZIP entries have separate BlobStore/source records and archive_index;
//                          source metadata records observed import completion for cache reuse.
//   nodes/links            non-conversation entities (kinds "export:account", "export:feedback",
//                            "export:shared_link", "export:project", "export:project_doc", "export:memory",
//                            "export:artifact", "export:member") with the verbatim record in metadata.
//
// Report (ImportResult::export_report), see Report::to_json().
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "loom/db.h"
#include "loom/importer.h"
#include "loom/provenance.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::xport {

namespace fs = std::filesystem;

inline constexpr std::string_view kSchema = "loom.export.v1";

// ── counters (the shape of tests/fixtures/exports/EXPECTED.json "counts") ──
struct Counts {
  std::int64_t conversation = 0, message = 0, block = 0, attachment = 0, citation = 0, citation_group = 0, branch = 0,
               fork_points = 0, custom_instruction = 0, memory = 0, artifact = 0, current_path_messages = 0, account = 0,
               feedback = 0, shared_link = 0, project = 0, project_doc = 0, record = 0;
  std::map<std::string, std::int64_t> block_kind;
  void add(const Counts& o);
  static Counts from_json(const Json& value);
  Json to_json() const;
};

// ── report ──
struct Report {
  std::string provider = "unknown";
  Counts counts;
  std::vector<Json> members;   // {name,size,disposition,...}
  std::vector<Json> errors;    // {member,index?,code,message}
  std::vector<std::string> warnings;
  Json repairs = Json::object();  // member -> {invalid_utf8, lone_surrogates}
  std::int64_t json_leaves = 0;        // JSON leaves in the conversation files
  std::int64_t leaves_preserved = 0;   // ... of which stored verbatim in the DB
  std::vector<std::string> asset_members, unreferenced_members, unresolved_keys;
  std::map<std::string, std::pair<std::string, std::string>> pointer_links;  // key -> (member, method)
  std::vector<std::vector<std::string>> duplicate_groups;
  std::vector<std::string> unknown_members;   // members no rule of the provider knows (kept verbatim)
  std::vector<Json> parts;                    // nested archives
  bool include_result_metadata = true;
  bool partial = false;                        // some element could not be imported
  std::int64_t resumed_conversations = 0;
  std::int64_t resumed_members = 0;
  std::int64_t largest_json_value_bytes = 0;
  std::int64_t scanner_buffer_bytes = 0;
  bool inferred = false;                       // conversations were guessed heuristically (unknown provider)
  Json to_json() const;
};

// ── environment shared by the writers ──
struct Env {
  Database& db;
  BlobStore* blobs = nullptr;
  const ImportOptions& opts;
  // Called after each conversation is committed (provenance hook).
  std::function<Status(const Conversation&, const std::string& member, int index)> on_conv;
  std::string checkpoint_source_id;
  std::optional<Error> write_error = std::nullopt;
  bool cancelled() const { return opts.cancel && opts.cancel->cancelled(); }
};

// ── intermediate model handed to the writer ──
struct MsgModel {
  std::string key;                    // node id / uuid
  int parent = -1;                    // index of the parent message in ConvModel::msgs
  std::string role, text;
  std::optional<std::string> model;
  std::string created;                // ISO-8601 UTC ("...Z")
  double weight = 1.0;
  std::string status = "active";
  int group = -1;                     // version group (index into ConvModel::groups) or -1
  int version_num = 1;
  Json attachments = Json::array();   // messages.attachments column
  Json export_meta = Json::object();  // messages.metadata["export"]
};

struct ConvModel {
  std::string key, title, created, updated, source;
  Json export_meta = Json::object();  // conversations.metadata["export"]
  std::vector<MsgModel> msgs;         // insertion order; Anthropic retains source array order
  int groups = 0;
  std::int64_t leaves_total = 0;      // JSON leaves of the source conversation object
  std::int64_t leaves_kept = 0;       // ... of which are stored verbatim (fields + raw messages + node structure)
};

// Writes one conversation + its messages in a single transaction.
Result<Conversation> write_conversation(Env& env, ConvModel& cm, std::map<std::string, std::string>* key_to_id = nullptr);
// Creates a graph node (kind "export:<x>") carrying a verbatim record.
Result<std::string> write_entity(Env& env, std::string_view kind, std::string_view label, std::string_view content,
                                 Json metadata);

// ── small helpers ──
std::int64_t json_leaves(const Json& j);
// Epoch seconds (number) / ISO-8601 string / other -> ISO "…Z"; nullopt when unusable.
std::optional<std::string> to_iso(const Json& v);
std::string first_line(std::string_view s, std::size_t max_cp);

// ── tolerant JSON loading ──
struct LoadStats {
  bool invalid_utf8 = false;
  std::int64_t lone_surrogates = 0;
  bool empty = false;
  bool cancelled = false;
  bool too_deep = false;
  bool truncated = false;               // last array element never closed
  std::int64_t truncated_bytes = 0;
  bool invalid = false;                 // could not be parsed at all
  std::string message;
  std::int64_t largest_value_bytes = 0;
  std::int64_t scanner_buffer_bytes = 0;
};
// Visits every element of the top-level array (streamed, so a truncated tail
// or one bad element does not lose the rest). A top-level object is visited as
// one element, except {"conversations":[...]} (wrapper) whose items are
// visited. `wrapper` reports that shape. cb returns false to stop.
// `bad_element(index, message)` is called for elements that fail to parse.
struct Loader {
  std::function<bool(Json&& element, std::int64_t index)> element;
  std::function<void(std::int64_t index, const std::string& why)> bad_element;
  std::function<bool()> cancelled;  // cooperative stop while scanning/streaming
  std::size_t read_chunk_bytes = default_import_preset().json_read_chunk_bytes;
  std::size_t max_depth = default_import_preset().json_max_depth;
  bool wrapper = false;
  bool top_is_array = false;
  std::optional<std::int64_t> archive_index;
  Json wrapper_fields = Json::object();  // every sibling of the conversations array, verbatim
};
LoadStats load_json_file(const fs::path& path, Loader& loader);
// Whole-document convenience (small files): parsed value or nullopt (see stats).
std::optional<Json> load_json_doc(const fs::path& path, LoadStats& stats, std::size_t max_depth);
Json stats_to_json(const LoadStats& s);

// Durable per-conversation/ZIP-record journal. source_index=-1 denotes a
// complete interpreted member; nonnegative indices denote conversations.
struct Checkpoint { std::string conversation_id; Json metadata; };
Result<std::optional<Checkpoint>> read_checkpoint(Env& env, const std::string& member,
                                                std::int64_t archive_index, std::int64_t source_index);
Status write_checkpoint(Env& env, const std::string& member, std::int64_t archive_index,
                        std::int64_t source_index, const std::string& conversation_id, const Json& metadata);

// ── provider parsers ──
// Asset lookup for OpenAI exports (ids/pointers -> ZIP members).
class AssetIndex {
 public:
  struct Hit {
    std::string member, method;
  };
  // Asset member layouts: root file-* / file_*, dalle-generations/*, user-*/*.
  static bool is_asset(const std::string& rel);
  void add(const std::string& rel, const fs::path& abs, std::int64_t size);
  bool empty() const { return files_.empty(); }
  // Resolves an asset key ("file-XXXX" / "file_000000..."); `name_hint` is the
  // attachment's original file name (fallback method "name").
  std::optional<Hit> resolve(const std::string& key, const std::string& name_hint);
  // Stores the member's bytes in the BlobStore (cached); returns the hash or "".
  std::string blob_hash(Env& env, const std::string& member);
  std::string readable_path(Env& env, const std::string& member);
  void finish(Env& env, Report& rep);   // duplicates, unreferenced, unresolved, blobs of leftovers
  std::vector<std::string> members() const;

  std::map<std::string, std::pair<std::string, std::string>> links;  // key -> (member, method)
  std::set<std::string> unresolved;

 private:
  struct File {
    std::string rel;
    fs::path abs;
    std::int64_t size = 0;
    int tier = 0;  // 0 root, 1 dalle-generations, 2 user-*
  };
  std::vector<File> files_;
  std::set<std::string> referenced_;
  std::set<std::string> ambiguous_members_;
  std::map<std::string, std::string> hash_cache_;
};

struct OpenAiCtx {
  AssetIndex* assets = nullptr;
  Env* env = nullptr;
  std::map<std::string, std::string> conv_db_id;   // conversation id -> db id
};

// Builds the model + counters for one ChatGPT conversation object.
void parse_openai_conversation(const Json& conv, int index, const std::string& member, OpenAiCtx& cx, ConvModel& out,
                               Counts& counts);
// Root members other than conversations (user.json, message_feedback.json,
// shared_conversations.json, textdocs/*.json, ...). Returns false when the
// member is not an OpenAI root member.
bool import_openai_member(Env& env, OpenAiCtx& cx, const std::string& rel, const fs::path& abs, Report& rep);

// DB-free overload; the Env-taking importer API forwards to this same parser.
void parse_anthropic_conversation(const Json& conv, int index, const std::string& member, ConvModel& out,
                                  Counts& counts);
void parse_anthropic_conversation(const Json& conv, int index, const std::string& member, Env& env, ConvModel& out,
                                  Counts& counts);
struct AnthropicCtx {
  std::map<std::string, std::string> project_db_id;  // project uuid -> node id
};
bool import_anthropic_member(Env& env, AnthropicCtx& cx, const std::string& rel, const fs::path& abs, Report& rep);

// Heuristic conversation extraction for exports no provider rule knows.
// Returns the number of conversations created.
struct InferOutcome {
  std::vector<Conversation> conversations;
  Counts counts;
};
bool generic_structure(const Json& doc);  // any conversation-like object with a message-like array?
void infer_generic(Env& env, const Json& doc, const std::string& member, const std::set<std::string>& file_members,
                   InferOutcome& out, Report& rep);

// ── shared by the archive driver ──
inline bool is_conversation_file(std::string_view base) {
  if (base == "conversations.json") return true;
  if (base.size() > 18 && base.substr(0, 14) == "conversations-" && base.substr(base.size() - 5) == ".json") {
    for (char c : base.substr(14, base.size() - 19)) {
      if (c < '0' || c > '9') return false;
    }
    return true;
  }
  return false;
}

}  // namespace loom::xport
