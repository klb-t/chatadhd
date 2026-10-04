// Shared pieces of the lossless provider-export importer: counters/report,
// tolerant JSON loading, the SQL writer and the OpenAI asset index.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

#include "export_internal.h"
#include "loom/log.h"
#include "loom/sqlite.h"
#include "loom/util/ids.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom::xport {

namespace {
constexpr std::string_view kLog = "loom.import.export";
}  // namespace

// ── Counts / Report ─────────────────────────────────────────────────
void Counts::add(const Counts& o) {
  conversation += o.conversation;
  message += o.message;
  block += o.block;
  attachment += o.attachment;
  citation += o.citation;
  citation_group += o.citation_group;
  branch += o.branch;
  fork_points += o.fork_points;
  custom_instruction += o.custom_instruction;
  memory += o.memory;
  artifact += o.artifact;
  current_path_messages += o.current_path_messages;
  account += o.account;
  feedback += o.feedback;
  shared_link += o.shared_link;
  project += o.project;
  project_doc += o.project_doc;
  record += o.record;
  for (const auto& [k, v] : o.block_kind) block_kind[k] += v;
}

Counts Counts::from_json(const Json& value) {
  Counts result;
  result.conversation = json::get_int(value, "conversation");
  result.message = json::get_int(value, "message");
  result.block = json::get_int(value, "block");
  result.attachment = json::get_int(value, "attachment");
  result.citation = json::get_int(value, "citation");
  result.citation_group = json::get_int(value, "citation_group");
  result.branch = json::get_int(value, "branch");
  result.fork_points = json::get_int(value, "fork_points");
  result.custom_instruction = json::get_int(value, "custom_instruction");
  result.memory = json::get_int(value, "memory");
  result.artifact = json::get_int(value, "artifact");
  result.current_path_messages = json::get_int(value, "current_path_messages");
  result.account = json::get_int(value, "account");
  result.feedback = json::get_int(value, "feedback");
  result.shared_link = json::get_int(value, "shared_link");
  result.project = json::get_int(value, "project");
  result.project_doc = json::get_int(value, "project_doc");
  result.record = json::get_int(value, "record");
  if (const Json* kinds = json::find(value, "block_kind"); kinds && kinds->is_object())
    for (auto item = kinds->begin(); item != kinds->end(); ++item) result.block_kind[item.key()] = item.value().get<std::int64_t>();
  return result;
}

Json Counts::to_json() const {
  Json bk = Json::object();
  for (const auto& [k, v] : block_kind) bk[k] = v;
  Json j = Json::object();
  j["account"] = account;
  j["artifact"] = artifact;
  j["attachment"] = attachment;
  j["block"] = block;
  j["block_kind"] = bk;
  j["branch"] = branch;
  j["citation"] = citation;
  j["citation_group"] = citation_group;
  j["conversation"] = conversation;
  j["current_path_messages"] = current_path_messages;
  j["custom_instruction"] = custom_instruction;
  j["feedback"] = feedback;
  j["fork_points"] = fork_points;
  j["memory"] = memory;
  j["message"] = message;
  j["project"] = project;
  j["project_doc"] = project_doc;
  j["record"] = record;
  j["shared_link"] = shared_link;
  return j;
}

Json Report::to_json() const {
  Json j = Json::object();
  j["schema"] = "loom.export_report.v1";
  j["provider"] = provider;
  j["counts"] = counts.to_json();
  j["members"] = Json(members);
  j["member_count"] = static_cast<std::int64_t>(members.size());
  j["errors"] = Json(errors);
  j["warnings"] = Json(warnings);
  j["partial"] = partial;
  j["include_result_metadata"] = include_result_metadata;
  j["inferred"] = inferred;
  j["resumed_conversations"] = resumed_conversations;
  j["resumed_members"] = resumed_members;
  j["largest_json_value_bytes"] = largest_json_value_bytes;
  j["scanner_buffer_bytes"] = scanner_buffer_bytes;
  j["repairs"] = repairs;
  j["json_leaves"] = json_leaves;
  j["leaves_preserved"] = leaves_preserved;
  j["asset_members"] = Json(asset_members);
  j["unreferenced_members"] = Json(unreferenced_members);
  j["unresolved_keys"] = Json(unresolved_keys);
  Json pl = Json::object();
  for (const auto& [k, v] : pointer_links) pl[k] = Json::array({v.first, v.second});
  j["pointer_links"] = pl;
  j["duplicate_groups"] = Json(duplicate_groups);
  j["unknown_members"] = Json(unknown_members);
  j["parts"] = Json(parts);
  return j;
}

// ── small helpers ───────────────────────────────────────────────────
std::int64_t json_leaves(const Json& j) {
  if (j.is_object()) {
    if (j.empty()) return 1;
    std::int64_t n = 0;
    for (auto it = j.begin(); it != j.end(); ++it) n += json_leaves(it.value());
    return n;
  }
  if (j.is_array()) {
    if (j.empty()) return 1;
    std::int64_t n = 0;
    for (const auto& x : j) n += json_leaves(x);
    return n;
  }
  return 1;
}

std::optional<std::string> to_iso(const Json& v) {
  using namespace std::chrono;
  if (v.is_number()) {
    double d = v.get<double>();
    if (!std::isfinite(d) || d < -62135596800.0) return std::nullopt;
    if (d > 1e11) d /= 1000.0;  // milliseconds
    auto us = static_cast<std::int64_t>(std::llround(d * 1e6));
    return timeutil::format_iso_utc(timeutil::Clock::time_point(duration_cast<timeutil::Clock::duration>(microseconds(us))));
  }
  if (v.is_string()) {
    const std::string& s = v.get_ref<const std::string&>();
    auto tp = timeutil::parse_iso_utc(s);
    if (!tp) return std::nullopt;
    // Trailing UTC offset (+HH:MM / -HH:MM) after the optional fraction.
    std::size_t i = 19;
    if (i < s.size() && s[i] == '.') {
      ++i;
      while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
    }
    if (s.size() >= i + 6 && (s[i] == '+' || s[i] == '-') && s[i + 3] == ':') {
      int hh = std::atoi(s.substr(i + 1, 2).c_str());
      int mm = std::atoi(s.substr(i + 4, 2).c_str());
      auto off = hours(hh) + minutes(mm);
      *tp = (s[i] == '+') ? (*tp - off) : (*tp + off);
    }
    return timeutil::format_iso_utc(*tp);
  }
  return std::nullopt;
}

std::string first_line(std::string_view s, std::size_t max_cp) {
  std::string_view t = utf8::strip(s);
  std::size_t nl = t.find('\n');
  if (nl != std::string_view::npos) t = t.substr(0, nl);
  return std::string(utf8::prefix(utf8::strip(t), max_cp));
}

// ── tolerant JSON ───────────────────────────────────────────────────
namespace {

int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
bool read_u16(std::string_view s, std::size_t i, unsigned& out) {  // s[i]='\\', s[i+1]='u'
  if (i + 6 > s.size() || s[i] != '\\' || s[i + 1] != 'u') return false;
  unsigned v = 0;
  for (int k = 2; k < 6; ++k) {
    int h = hexval(s[i + k]);
    if (h < 0) return false;
    v = v * 16 + static_cast<unsigned>(h);
  }
  out = v;
  return true;
}

// Replaces unpaired \uD800-\uDFFF escapes with � (same length); JSON
// permits them, UTF-8 (and nlohmann) do not.
std::int64_t fix_lone_surrogates(std::string& s) {
  std::int64_t n = 0;
  std::size_t i = 0;
  while (i < s.size()) {
    if (s[i] != '\\') {
      ++i;
      continue;
    }
    if (i + 1 >= s.size()) break;
    if (s[i + 1] != 'u') {
      i += 2;
      continue;
    }
    unsigned c = 0;
    if (!read_u16(s, i, c)) {
      i += 2;
      continue;
    }
    if (c >= 0xD800 && c <= 0xDBFF) {
      unsigned d = 0;
      if (read_u16(s, i + 6, d) && d >= 0xDC00 && d <= 0xDFFF) {
        i += 12;
      } else {
        std::memcpy(&s[i], "\\ufffd", 6);
        ++n;
        i += 6;
      }
    } else if (c >= 0xDC00 && c <= 0xDFFF) {
      std::memcpy(&s[i], "\\ufffd", 6);
      ++n;
      i += 6;
    } else {
      i += 6;
    }
  }
  return n;
}

bool too_deep(std::string_view s, std::size_t max_depth) {
  if (!max_depth) return false;
  std::size_t depth = 0;
  bool in_str = false, esc = false;
  for (char c : s) {
    if (in_str) {
      if (esc) esc = false;
      else if (c == '\\') esc = true;
      else if (c == '"') in_str = false;
      continue;
    }
    if (c == '"') in_str = true;
    else if (c == '[' || c == '{') {
      if (++depth > max_depth) return true;
    } else if (c == ']' || c == '}') {
      --depth;
    }
  }
  return false;
}

bool parse_tolerant(std::string_view raw, LoadStats& st, Json& out, std::string& why, std::size_t max_depth = 512) {
  std::string fixed;
  std::string_view text = raw;
  if (!utf8::is_valid(raw)) {
    fixed = utf8::repair(raw);
    text = fixed;
    st.invalid_utf8 = true;
  }
  if (text.find("\\u") != std::string_view::npos || text.find("\\U") != std::string_view::npos) {
    if (fixed.empty()) fixed.assign(text);
    if (auto n = fix_lone_surrogates(fixed); n > 0) st.lone_surrogates += n;
    text = fixed;
  }
  if (too_deep(text, max_depth)) {
    st.too_deep = true;
    why = "JSON nesting deeper than " + std::to_string(max_depth);
    return false;
  }
  auto r = json::parse(text);
  if (!r) {
    why = "invalid JSON";
    return false;
  }
  out = std::move(*r);
  return true;
}

}  // namespace

std::optional<Json> load_json_doc(const fs::path& path, LoadStats& st) {
  auto raw = fsutil::read_file(path);
  if (!raw) {
    st.invalid = true;
    st.message = raw.error().message;
    return std::nullopt;
  }
  if (utf8::is_blank(*raw)) {
    st.empty = true;
    st.message = "empty file";
    return std::nullopt;
  }
  Json out;
  std::string why;
  if (!parse_tolerant(*raw, st, out, why)) {
    st.invalid = true;
    st.message = why;
    return std::nullopt;
  }
  return out;
}

namespace {
// The scanner never keeps the complete conversations array. A wrapper is
// scanned once to retain all sibling fields, then its array is visited using
// seek. Thus suffix metadata is already available for the first conversation.
class JsonReader {
 public:
  JsonReader(const fs::path& path, const Loader& loader, LoadStats& stats)
      : file_(path, std::ios::binary), loader_(loader), stats_(stats), buffer_(loader.read_chunk_bytes, '\0') {
    stats_.scanner_buffer_bytes = static_cast<std::int64_t>(buffer_.size());
  }
  bool open() const { return file_.is_open(); }
  int peek() {
    if (index_ == used_) {
      if (loader_.cancelled && loader_.cancelled()) { stats_.cancelled = true; return EOF; }
      file_.read(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
      used_ = static_cast<std::size_t>(file_.gcount());
      index_ = 0;
      if (!used_) return EOF;
    }
    return static_cast<unsigned char>(buffer_[index_]);
  }
  int get() { const int c = peek(); if (c != EOF) { ++index_; ++offset_; } return c; }
  std::int64_t position() const { return offset_; }
  void seek(std::int64_t position) {
    file_.clear(); file_.seekg(position); index_ = used_ = 0; offset_ = position;
  }
  void spaces() { while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') get(); }
  bool value(std::string* captured) {
    spaces();
    if (peek() == EOF) return false;
    std::vector<int> closes;
    bool quoted = false, escaped = false, scalar = false;
    std::int64_t bytes = 0;
    const int first = peek();
    scalar = first != '{' && first != '[' && first != '"';
    for (;;) {
      int c = peek();
      if (c == EOF) { pending_ = bytes; return scalar && bytes > 0 && !stats_.cancelled; }
      if (scalar && !quoted && closes.empty() && (c == ',' || c == ']' || c == '}' ||
          c == ' ' || c == '\t' || c == '\r' || c == '\n')) break;
      get(); ++bytes;
      if (captured) captured->push_back(static_cast<char>(c));
      if (quoted) {
        if (escaped) escaped = false;
        else if (c == '\\') escaped = true;
        else if (c == '"') { quoted = false; if (closes.empty()) break; }
      } else if (c == '"') quoted = true;
      else if (c == '{') closes.push_back('}');
      else if (c == '[') closes.push_back(']');
      else if (c == '}' || c == ']') {
        if (closes.empty() || closes.back() != c) { pending_ = bytes; return false; }
        closes.pop_back(); if (closes.empty()) break;
      }
    }
    if (captured) stats_.largest_value_bytes = std::max(stats_.largest_value_bytes, bytes);
    pending_ = 0;
    return bytes > 0;
  }
  std::int64_t pending() const { return pending_; }
 private:
  std::ifstream file_;
  const Loader& loader_;
  LoadStats& stats_;
  std::string buffer_;
  std::size_t index_ = 0, used_ = 0;
  std::int64_t offset_ = 0, pending_ = 0;
};

bool scan_array(JsonReader& reader, Loader& loader, LoadStats& stats) {
  if (reader.get() != '[') return false;
  std::int64_t index = 0;
  reader.spaces();
  if (reader.peek() == ']') { reader.get(); return true; }
  for (;;) {
    if (loader.cancelled && loader.cancelled()) { stats.cancelled = true; return false; }
    std::string raw;
    if (!reader.value(&raw)) {
      if (!stats.cancelled) {
        stats.truncated = true; stats.truncated_bytes = reader.pending(); stats.message = "array element not terminated";
      }
      return false;
    }
    Json element;
    std::string why;
    if (!parse_tolerant(raw, stats, element, why, loader.max_depth)) {
      if (loader.bad_element) loader.bad_element(index, why);
    } else if (loader.element && !loader.element(std::move(element), index)) return false;
    ++index;
    reader.spaces();
    const int delimiter = reader.get();
    if (delimiter == ']') return true;
    if (delimiter != ',') {
      stats.truncated = delimiter == EOF && !stats.cancelled;
      stats.invalid = delimiter != EOF;
      stats.message = "expected comma or array terminator";
      return false;
    }
    reader.spaces();
    if (reader.peek() == ']') { stats.invalid = true; stats.message = "trailing comma in array"; return false; }
  }
}
}  // namespace

LoadStats load_json_file(const fs::path& path, Loader& loader) {
  LoadStats stats;
  if (!loader.read_chunk_bytes || loader.read_chunk_bytes > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
    stats.invalid = true; stats.message = "JSON read chunk must be positive and representable"; return stats;
  }
  JsonReader reader(path, loader, stats);
  if (!reader.open()) { stats.invalid = true; stats.message = "cannot open file"; return stats; }
  reader.spaces();
  if (reader.peek() == 0xEF) {
    if (reader.get() != 0xEF || reader.get() != 0xBB || reader.get() != 0xBF) {
      stats.invalid = true; stats.message = "invalid UTF-8 BOM"; return stats;
    }
    reader.spaces();
  }
  if (reader.peek() == EOF) {
    stats.empty = !stats.cancelled; stats.message = stats.cancelled ? "cancelled" : "empty file"; return stats;
  }
  const auto root = reader.position();
  bool finished = false;
  if (reader.peek() == '[') {
    loader.top_is_array = true;
    finished = scan_array(reader, loader, stats);
  } else if (reader.peek() == '{') {
    // Capture sibling values individually. Discard the conversation array
    // while locating its end; it will be read element by element below.
    reader.get(); reader.spaces();
    std::int64_t array_position = -1;
    bool provider_object = false, valid_object = true;
    if (reader.peek() == '}') reader.get();
    else for (;;) {
      std::string key_raw, value_raw;
      Json key;
      std::string why;
      if (reader.peek() != '"' || !reader.value(&key_raw) ||
          !parse_tolerant(key_raw, stats, key, why, loader.max_depth) || !key.is_string()) { valid_object = false; break; }
      const auto name = key.get<std::string>();
      reader.spaces(); if (reader.get() != ':') { valid_object = false; break; }
      reader.spaces();
      if (name == "conversations" && reader.peek() == '[') {
        array_position = reader.position();
        if (!reader.value(nullptr)) { valid_object = false; break; }
      } else {
        if (!reader.value(&value_raw)) { valid_object = false; break; }
        Json value;
        if (!parse_tolerant(value_raw, stats, value, why, loader.max_depth)) {
          stats.invalid = true; stats.message = why; return stats;
        }
        loader.wrapper_fields[name] = std::move(value);
        provider_object = provider_object || name == "mapping" || name == "chat_messages";
      }
      reader.spaces(); const int delimiter = reader.get();
      if (delimiter == '}') break;
      if (delimiter != ',') { valid_object = false; break; }
      reader.spaces();
    }
    if (stats.cancelled) return stats;
    if (!valid_object) {
      stats.truncated = reader.peek() == EOF; stats.invalid = !stats.truncated;
      stats.truncated_bytes = reader.pending(); stats.message = "invalid or truncated top-level object";
      if (array_position >= 0 && !provider_object) {
        // Completed elements remain usable even when a wrapper's array/tail
        // was truncated. Missing suffix fields remain an explicit partial run.
        loader.wrapper = true; loader.top_is_array = true;
        reader.seek(array_position); (void)scan_array(reader, loader, stats);
      }
      return stats;
    }
    reader.spaces();
    if (reader.peek() != EOF) { stats.invalid = true; stats.message = "trailing bytes after JSON object"; return stats; }
    if (array_position >= 0 && !provider_object) {
      loader.wrapper = true; loader.top_is_array = true;
      reader.seek(array_position);
      // The complete wrapper was already checked, so suffix bytes are known.
      (void)scan_array(reader, loader, stats);
      return stats;
    }
    loader.wrapper_fields = Json::object();
    // Sibling probing of a single provider object is not an additional source
    // observation; count tolerant repairs once when parsing the whole object.
    stats.invalid_utf8 = false; stats.lone_surrogates = 0;
    reader.seek(root);
    std::string raw; Json value; std::string why;
    if (!reader.value(&raw) || !parse_tolerant(raw, stats, value, why, loader.max_depth)) {
      stats.invalid = true; stats.message = why.empty() ? "invalid JSON value" : why; return stats;
    }
    if (loader.element) loader.element(std::move(value), 0);
    return stats;
  } else {
    std::string raw; Json value; std::string why;
    if (!reader.value(&raw) || !parse_tolerant(raw, stats, value, why, loader.max_depth)) {
      stats.invalid = true; stats.message = why.empty() ? "invalid JSON value" : why; return stats;
    }
    if (loader.element) loader.element(std::move(value), 0);
    finished = true;
  }
  if (finished) {
    reader.spaces();
    if (reader.peek() != EOF) { stats.invalid = true; stats.message = "trailing bytes after JSON value"; }
  }
  return stats;
}

Json stats_to_json(const LoadStats& s) {
  Json j = Json::object();
  if (s.invalid_utf8) j["invalid_utf8"] = true;
  if (s.lone_surrogates) j["lone_surrogates"] = s.lone_surrogates;
  if (s.truncated) {
    j["truncated"] = true;
    j["truncated_bytes"] = s.truncated_bytes;
  }
  if (s.too_deep) j["too_deep"] = true;
  return j;
}

// ── writer ──────────────────────────────────────────────────────────
Result<Conversation> write_conversation(Env& env, ConvModel& cm, std::map<std::string, std::string>* key_to_id) {
  Database& db = env.db;
  auto lk = db.lock();
  auto& conn = db.conn();

  Conversation conv;
  conv.id = gen_id(id_prefix::kConversation);
  conv.title = cm.title;
  conv.created = cm.created;
  conv.updated = cm.updated.empty() ? cm.created : cm.updated;
  conv.source = cm.source;
  conv.metadata = Json::object();
  conv.metadata["export"] = cm.export_meta;

  sql::Txn txn(conn);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY(conn.run("INSERT INTO conversations (id, title, created, updated, source, metadata) VALUES (?, ?, ?, ?, ?, ?)",
                    conv.id, conv.title, conv.created, conv.updated, conv.source, json::py_dumps(conv.metadata)));

  std::vector<std::string> ids(cm.msgs.size());
  for (auto& id : ids) id = gen_id(id_prefix::kMessage);
  std::vector<std::string> group_ids(static_cast<std::size_t>(std::max(0, cm.groups)));
  for (auto& g : group_ids) g = gen_id(id_prefix::kVersionGroup);

  LOOM_TRY_ASSIGN(auto st, conn.prepare("INSERT INTO messages (id, conv_id, parent_id, role, text, model, status, "
                                        "version_group_id, version_num, weight, attachments, metadata, created, "
                                        "semantic_status) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
  for (std::size_t i = 0; i < cm.msgs.size(); ++i) {
    const MsgModel& m = cm.msgs[i];
    std::optional<std::string> parent;
    // IDs are allocated for the whole conversation above. parent_id has no
    // immediate foreign-key constraint; the transaction commits only after all
    // rows exist, so source ordering need not be parent-before-child ordering.
    if (m.parent >= 0 && static_cast<std::size_t>(m.parent) < ids.size() && static_cast<std::size_t>(m.parent) != i)
      parent = ids[static_cast<std::size_t>(m.parent)];
    std::string vg = m.group >= 0 ? group_ids[static_cast<std::size_t>(m.group)] : gen_id(id_prefix::kVersionGroup);
    Json md = Json::object();
    md["export"] = m.export_meta;
    st.reset();
    st.bind_all(ids[i], conv.id, parent, m.role, m.text, m.model, m.status, vg, static_cast<std::int64_t>(m.version_num),
                m.weight, json::py_dumps(m.attachments), json::py_dumps(md), m.created, std::string_view("pending"));
    LOOM_TRY(st.run());
    if (key_to_id && !m.key.empty()) (*key_to_id)[m.key] = ids[i];
  }
  LOOM_TRY(txn.commit());
  return conv;
}

Result<std::string> write_entity(Env& env, std::string_view kind, std::string_view label, std::string_view content,
                                 Json metadata) {
  NodeOptions no;
  no.content = std::string(content);
  no.metadata = std::move(metadata);
  auto result = env.db.create_node(label, kind, no);
  if (!result) env.write_error = result.error();
  return result;
}

// ── AssetIndex ──────────────────────────────────────────────────────
namespace {
std::string base_name(const std::string& rel) {
  auto p = rel.find_last_of('/');
  return p == std::string::npos ? rel : rel.substr(p + 1);
}
bool starts_with(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
}  // namespace

bool AssetIndex::is_asset(const std::string& rel) {
  auto slash = rel.find('/');
  if (slash == std::string::npos) return starts_with(rel, "file-") || starts_with(rel, "file_");
  std::string top = rel.substr(0, slash);
  return top == "dalle-generations" || starts_with(top, "user-");
}

void AssetIndex::add(const std::string& rel, const fs::path& abs, std::int64_t size) {
  File f;
  f.rel = rel;
  f.abs = abs;
  f.size = size;
  auto slash = rel.find('/');
  f.tier = slash == std::string::npos ? 0 : (rel.substr(0, slash) == "dalle-generations" ? 1 : 2);
  if (std::any_of(files_.begin(), files_.end(), [&](const File& prior) { return prior.rel == rel; }))
    ambiguous_members_.insert(rel);
  files_.push_back(std::move(f));
  std::sort(files_.begin(), files_.end(), [](const File& a, const File& b) {
    return a.tier != b.tier ? a.tier < b.tier : a.rel < b.rel;
  });
}

std::vector<std::string> AssetIndex::members() const {
  std::vector<std::string> v;
  for (const auto& f : files_) v.push_back(f.rel);
  std::sort(v.begin(), v.end());
  return v;
}

std::optional<AssetIndex::Hit> AssetIndex::resolve(const std::string& key, const std::string& name_hint) {
  if (key.empty()) return std::nullopt;
  if (auto it = links.find(key); it != links.end()) return Hit{it->second.first, it->second.second};
  std::optional<Hit> hit;
  // (1) exact base name, (2) id followed by a delimiter; tiers root, dalle, user-*.
  for (int pass = 0; pass < 2 && !hit; ++pass) {
    for (const auto& f : files_) {
      std::string b = base_name(f.rel);
      bool ok = false;
      if (pass == 0) ok = (b == key);
      else ok = b.size() > key.size() && b.compare(0, key.size(), key) == 0 &&
                (b[key.size()] == '-' || b[key.size()] == '_' || b[key.size()] == '.');
      if (ok) {
        hit = Hit{f.rel, pass == 0 ? "id_exact" : "id_prefix"};
        break;
      }
    }
  }
  // (3) original file name carried inside the member name.
  if (!hit && !name_hint.empty()) {
    std::vector<const File*> cands;
    for (const auto& f : files_) {
      std::string b = base_name(f.rel);
      if (b == name_hint || (b.size() > name_hint.size() && b.compare(b.size() - name_hint.size(), name_hint.size(), name_hint) == 0 &&
                             b[b.size() - name_hint.size() - 1] == '-')) {
        cands.push_back(&f);
      }
    }
    if (cands.size() == 1) hit = Hit{cands[0]->rel, "name"};
  }
  if (hit && ambiguous_members_.count(hit->member)) hit.reset();
  if (hit) {
    links[key] = {hit->member, hit->method};
    referenced_.insert(hit->member);
    unresolved.erase(key);
  } else {
    unresolved.insert(key);
  }
  return hit;
}

std::string AssetIndex::blob_hash(Env& env, const std::string& member) {
  if (!env.blobs || ambiguous_members_.count(member)) return "";
  if (auto it = hash_cache_.find(member); it != hash_cache_.end()) return it->second;
  for (const auto& f : files_) {
    if (f.rel != member) continue;
    auto r = env.blobs->put_file(f.abs, "");
    std::string h = r ? r->hash : "";
    if (!r) log::warn(kLog, "asset {}: blob store failed: {}", member, r.error().message);
    hash_cache_[member] = h;
    return h;
  }
  return "";
}

std::string AssetIndex::readable_path(Env& env, const std::string& member) {
  std::string h = blob_hash(env, member);
  if (!h.empty() && env.blobs) return env.blobs->path_for(h).string();
  return member;
}

void AssetIndex::finish(Env& env, Report& rep) {
  for (const auto& member : ambiguous_members_) {
    rep.errors.push_back(Json{{"member", member}, {"code", "ambiguous_asset_member"},
                              {"message", "multiple archive entries share this asset path; no unique attachment binding"}});
    rep.partial = true;
  }
  std::map<std::string, std::vector<std::string>> by_hash;
  for (const auto& f : files_) {
    if (env.cancelled()) { rep.partial = true; break; }
    rep.asset_members.push_back(f.rel);
    if (!referenced_.count(f.rel)) rep.unreferenced_members.push_back(f.rel);
    if (auto h = sha256_file_hex(f.abs); h) by_hash[*h].push_back(f.rel);
    (void)blob_hash(env, f.rel);  // keep unreferenced bytes too
  }
  std::sort(rep.asset_members.begin(), rep.asset_members.end());
  std::sort(rep.unreferenced_members.begin(), rep.unreferenced_members.end());
  for (auto& [h, v] : by_hash) {
    if (v.size() > 1) {
      std::sort(v.begin(), v.end());
      rep.duplicate_groups.push_back(v);
    }
  }
  std::sort(rep.duplicate_groups.begin(), rep.duplicate_groups.end());
  for (const auto& k : unresolved) rep.unresolved_keys.push_back(k);
  rep.pointer_links = std::map<std::string, std::pair<std::string, std::string>>(links.begin(), links.end());
}

}  // namespace loom::xport
