// JSON / JSONL: routing (_import_json_data), the ChatGPT mapping-tree walk,
// Claude chat_messages export, generic conversation objects, and the
// streaming path for files over ImportOptions::stream_threshold_bytes.
#include <fstream>
#include <unordered_map>

#include "importer_internal.h"
#include "import/import_recognition.h"
#include "loom/importer.h"
#include "loom/log.h"
#include "loom/util/fs.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom {

namespace fs = std::filesystem;
namespace idt = importer_detail;

namespace {
constexpr std::string_view kLog = "loom.import";

Error unrecognized_structure() {
  return Error(Errc::Unsupported, "Unrecognized JSON conversation structure");
}

// `title or conv_data.get(k1, conv_data.get(k2, ..., fallback))`: a truthy
// caller title wins outright; otherwise the first `keys` entry present in
// `conv_data` (by key, not truthiness) wins; else `fallback`.
std::string resolve_title(const std::optional<std::string>& title, const Json& conv_data,
                          std::initializer_list<const char*> keys, const std::string& fallback) {
  if (title && !title->empty()) return *title;
  for (const char* k : keys) {
    if (auto v = idt::get_present_str(conv_data, k)) return *v;
  }
  return fallback;
}

std::string join_text_parts(const std::vector<std::string>& parts) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += "\n";
    out += parts[i];
  }
  return out;
}

}  // namespace

// ── ChatGPT mapping tree ────────────────────────────────────────────
Result<std::optional<Conversation>> ConversationImporter::import_chatgpt_mapping(
    const Json& conv_data, const std::optional<std::string>& title) {
  if (!conv_data.is_object()) return std::optional<Conversation>{};
  std::string conv_title =
      resolve_title(title, conv_data, {"title"}, "ChatGPT Import " + timeutil::local_now_format("%H:%M"));

  const Json* mapping = json::find(conv_data, "mapping");
  if (!mapping || !mapping->is_object() || mapping->empty()) return std::optional<Conversation>{};

  static constexpr std::string_view kNullParent = "\x01__loom_null_parent__\x01";
  std::unordered_map<std::string, std::vector<std::string>> children_of;
  std::vector<std::string> node_order;
  node_order.reserve(mapping->size());
  for (auto it = mapping->begin(); it != mapping->end(); ++it) {
    node_order.push_back(it.key());
    std::string parent_key(kNullParent);
    if (const Json* p = json::find(it.value(), "parent"); p && p->is_string()) parent_key = p->get<std::string>();
    children_of[parent_key].push_back(it.key());
  }

  Json messages = Json::array();
  std::function<void(const std::string&)> walk = [&](const std::string& node_id) {
    if (auto nit = mapping->find(node_id); nit != mapping->end()) {
      if (const Json* msg = json::find(*nit, "message"); msg && msg->is_object()) {
        const Json* author = json::find(*msg, "author");
        std::string role = author ? json::get_string(*author, "role", "unknown") : "unknown";
        const Json* content = json::find(*msg, "content");
        std::string text;
        if (content && content->is_object()) {
          std::vector<std::string> parts;
          if (const Json* p = json::find(*content, "parts"); p && p->is_array()) {
            for (const auto& part : *p) {
              if (part.is_string()) {
                parts.push_back(part.get<std::string>());
              } else if (part.is_object() && json::get_string(part, "content_type") == "text") {
                parts.push_back(json::get_string(part, "text"));
              }
            }
          }
          text = join_text_parts(parts);
        } else if (content && content->is_string()) {
          text = content->get<std::string>();
        }
        if (!utf8::is_blank(text) && (role == "user" || role == "assistant")) {
          messages.push_back(Json{{"role", role}, {"content", text}});
        }
      }
    }
    if (auto cit = children_of.find(node_id); cit != children_of.end()) {
      for (const auto& child : cit->second) walk(child);
    }
  };

  for (const auto& nid : node_order) {
    const Json& node = (*mapping)[nid];
    bool is_root = true;
    if (const Json* p = json::find(node, "parent"); p && p->is_string()) {
      is_root = mapping->find(p->get<std::string>()) == mapping->end();
    }
    if (is_root) walk(nid);
  }

  if (messages.empty()) return std::optional<Conversation>{};
  LOOM_TRY_ASSIGN(Conversation conv, import_message_list(messages, conv_title));
  LOOM_TRY(keep_partial(conv_data, {"title", "mapping"}, "loom.importer.json.chatgpt_mapping", conv));
  return std::optional<Conversation>(std::move(conv));
}

// ── Claude.ai export (chat_messages) ────────────────────────────────
Result<std::optional<Conversation>> ConversationImporter::import_claude_export(
    const Json& conv_data, const std::optional<std::string>& title) {
  if (!conv_data.is_object()) return std::optional<Conversation>{};
  std::string conv_title =
      resolve_title(title, conv_data, {"name", "title"}, "Claude Import " + timeutil::local_now_format("%H:%M"));

  Json messages = Json::array();
  if (const Json* chat_msgs = json::find(conv_data, "chat_messages"); chat_msgs && chat_msgs->is_array()) {
    for (const auto& msg : *chat_msgs) {
      if (!msg.is_object()) continue;
      std::string sender = "human";
      if (const Json* s = json::find(msg, "sender"); s && s->is_string()) sender = s->get<std::string>();
      std::string role = sender == "human" ? "user" : "assistant";

      const Json* text_v = json::find(msg, "text");
      Json content = text_v ? *text_v : Json("");
      if (!json::truthy(content)) {
        if (const Json* c = json::find(msg, "content")) {
          if (c->is_array()) {
            std::vector<std::string> parts;
            for (const auto& part : *c) {
              if (part.is_object() && json::get_string(part, "type") == "text") parts.push_back(json::get_string(part, "text"));
            }
            content = join_text_parts(parts);
          } else if (c->is_string()) {
            content = *c;
          }
        }
      }
      std::string final_text = content.is_string() ? content.get<std::string>() : idt::py_str(content);
      if (!utf8::is_blank(final_text)) messages.push_back(Json{{"role", role}, {"content", content}});
    }
  }

  if (messages.empty()) return std::optional<Conversation>{};
  LOOM_TRY_ASSIGN(Conversation conv, import_message_list(messages, conv_title));
  LOOM_TRY(keep_partial(conv_data, {"name", "title", "chat_messages"}, "loom.importer.json.claude_export", conv));
  return std::optional<Conversation>(std::move(conv));
}

// ── Generic conversation objects (messages | chat_messages | items | data) ─
Result<std::optional<Conversation>> ConversationImporter::import_conversation_obj(
    const Json& conv_data, const std::optional<std::string>& title) {
  if (!conv_data.is_object()) return unrecognized_structure();
  std::string conv_title =
      resolve_title(title, conv_data, {"title", "name", "conversation_name"}, "Import " + timeutil::local_now_format("%H:%M"));

  Json messages = Json::array();
  bool recognized = false;
  const char* used_key = "messages";
  for (const char* key : {"messages", "chat_messages", "items", "data"}) {
    if (const Json* v = json::find(conv_data, key)) {
      recognized = true;
      used_key = key;
      if (json::truthy(*v)) {
        messages = *v;
        break;
      }
    }
  }
  if (!recognized) return unrecognized_structure();
  if (messages.is_object()) {
    Json arr = Json::array();
    for (auto it = messages.begin(); it != messages.end(); ++it) arr.push_back(it.value());
    messages = arr;
  }
  if (!json::truthy(messages)) return std::optional<Conversation>{};

  LOOM_TRY_ASSIGN(Conversation conv, import_message_list(messages, conv_title));
  LOOM_TRY(keep_partial(conv_data, {"title", "name", "conversation_name", used_key},
                        "loom.importer.json.conversation_object", conv));
  return std::optional<Conversation>(std::move(conv));
}

// ── Single-element + full-document routing ──────────────────────────
Result<std::optional<Conversation>> ConversationImporter::import_single_element(
    const Json& element, const std::optional<std::string>& title) {
  if (!element.is_object()) return unrecognized_structure();
  if (json::find(element, "mapping")) return import_chatgpt_mapping(element, title);
  if (json::find(element, "chat_messages")) return import_claude_export(element, title);
  return import_conversation_obj(element, title);
}

namespace {

// Byte spans [start, end) of the elements of a top-level JSON array in `text`
// (strings and nesting tracked). Empty when `text` is not a well-formed array.
std::vector<std::pair<std::size_t, std::size_t>> array_spans(std::string_view text) {
  std::vector<std::pair<std::size_t, std::size_t>> spans;
  std::size_t i = 0;
  auto space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
  while (i < text.size() && space(text[i])) ++i;
  if (i >= text.size() || text[i] != '[') return {};
  ++i;
  std::size_t start = std::string_view::npos;
  int depth = 0;
  bool in_string = false, escape = false;
  auto close = [&](std::size_t end) {
    if (start == std::string_view::npos) return;
    while (end > start && space(text[end - 1])) --end;
    if (end > start) spans.emplace_back(start, end);
    start = std::string_view::npos;
  };
  for (; i < text.size(); ++i) {
    const char c = text[i];
    if (in_string) {
      if (escape) escape = false;
      else if (c == '\\') escape = true;
      else if (c == '"') in_string = false;
      continue;
    }
    if (start == std::string_view::npos) {
      if (space(c) || c == ',') continue;
      if (c == ']' && depth == 0) return spans;
      start = i;
    }
    if (c == '"') in_string = true;
    else if (c == '{' || c == '[') ++depth;
    else if (c == '}' || c == ']') {
      if (depth == 0) { close(i); return spans; }
      --depth;
    } else if (c == ',' && depth == 0) close(i);
  }
  return {};
}

}  // namespace

Result<std::vector<Conversation>> ConversationImporter::import_json_data(const Json& data,
                                                                         const std::optional<std::string>& title) {
  return import_json_data_at(data, title, "", {});
}

// R44: an element no handler recognises is kept verbatim with its locator
// (when a ledger is active, i.e. inside an import) instead of failing the
// whole import. `text` is the exact source text when known (byte ranges).
Result<std::vector<Conversation>> ConversationImporter::import_json_data_at(
    const Json& data, const std::optional<std::string>& title, const std::string& pointer, std::string_view text) {
  std::vector<Conversation> results;
  std::vector<std::pair<std::size_t, std::size_t>> spans;
  if (pointer.empty() && data.is_array() && !text.empty() && ledger_) {
    spans = array_spans(text);
    if (spans.size() != data.size()) spans.clear();
  }
  if (ledger_) ledger_->measured("json");

  auto locate = [&](const std::string& ptr, std::size_t index, bool use_span) {
    cursor_pointer_ = ptr;
    cursor_locator_ = Json{{"json_pointer", ptr}};
    if (use_span && index < spans.size()) {
      cursor_locator_["byte_start"] = static_cast<std::int64_t>(spans[index].first);
      cursor_locator_["byte_end"] = static_cast<std::int64_t>(spans[index].second);
    } else if (use_span && ptr.empty() && !text.empty()) {
      cursor_locator_["byte_start"] = 0;
      cursor_locator_["byte_end"] = static_cast<std::int64_t>(text.size());
    }
  };
  auto keep_unknown = [&](const Json& element, std::size_t index, bool use_span, std::string_view reason) -> Status {
    if (!ledger_) return unrecognized_structure();
    if (cursor_pointer_.empty() && caller_keeps_whole_document_) {
      whole_document_reason_ = unrecognized_structure().message;  // the caller retains the member
      return {};
    }
    std::string raw;
    if (use_span && index < spans.size()) raw = std::string(text.substr(spans[index].first, spans[index].second - spans[index].first));
    else if (use_span && cursor_pointer_.empty() && !text.empty()) raw = std::string(text);
    else raw = json::py_dumps(element);
    return keep_unmapped(recognition::kUnrecognized, "loom.importer.json", reason,
                         "unrecognized JSON element " + (cursor_pointer_.empty() ? std::string("(document)") : cursor_pointer_),
                         cursor_locator_, raw);
  };
  auto element_of = [&](const Json& element, const std::string& ptr, std::size_t index, bool use_span,
                        auto&& handler, bool object_only) -> Status {
    locate(ptr, index, use_span);
    if (object_only && ledger_ && !element.is_object())
      return keep_unknown(element, index, use_span, "element is not a conversation object");
    auto r = handler(element);
    if (!r) {
      if (ledger_ && r.error().code == Errc::Unsupported) return keep_unknown(element, index, use_span, r.error().message);
      return r.error();
    }
    if (*r) results.push_back(std::move(**r));
    return {};
  };
  auto each_index = [&](const Json& array, const std::string& prefix, bool use_span, auto&& handler,
                        bool object_only) -> Status {
    std::size_t index = 0;
    for (const auto& element : array) {
      LOOM_TRY(element_of(element, prefix + "/" + std::to_string(index), index, use_span, handler, object_only));
      ++index;
    }
    return {};
  };
  auto as_mapping = [&](const Json& e) { return import_chatgpt_mapping(e, title); };
  auto as_claude = [&](const Json& e) { return import_claude_export(e, title); };
  auto as_object = [&](const Json& e) { return import_conversation_obj(e, title); };
  auto as_message_list = [&](const Json& message_array) -> Status {
    // Non-object entries of a direct message array are not messages (R44).
    if (ledger_) {
      std::size_t index = 0;
      for (const auto& element : message_array) {
        if (!element.is_object()) {
          locate(pointer + "/" + std::to_string(index), index, true);
          LOOM_TRY(keep_unknown(element, index, true, "array entry is not a message object"));
        }
        ++index;
      }
    }
    locate(pointer, 0, false);
    LOOM_TRY_ASSIGN(Conversation c, import_message_list(message_array, title));
    results.push_back(std::move(c));
    return {};
  };

  if (data.is_array()) {
    if (data.empty()) return results;
    const Json& first = data.front();
    if (first.is_object()) {
      if (json::find(first, "mapping")) {
        LOOM_TRY(each_index(data, pointer, true, as_mapping, true));
      } else if (json::find(first, "role") || json::find(first, "content")) {
        LOOM_TRY(as_message_list(data));
      } else if (json::find(first, "messages")) {
        LOOM_TRY(each_index(data, pointer, true, as_object, false));
      } else if (json::find(first, "chat_messages")) {
        LOOM_TRY(each_index(data, pointer, true, as_claude, true));
      } else {
        LOOM_TRY(each_index(data, pointer, true, as_object, false));
      }
    } else if (ledger_) {
      LOOM_TRY(each_index(data, pointer, true, as_object, false));
    } else {
      return unrecognized_structure();
    }
  } else if (data.is_object()) {
    if (json::find(data, "mapping")) {
      LOOM_TRY(element_of(data, pointer, 0, true, as_mapping, false));
    } else if (json::find(data, "chat_messages")) {
      LOOM_TRY(element_of(data, pointer, 0, true, as_claude, false));
    } else if (json::find(data, "messages")) {
      LOOM_TRY(element_of(data, pointer, 0, true, as_object, false));
    } else if (const Json* convs = json::find(data, "conversations"); convs && convs->is_array()) {
      LOOM_TRY(each_index(*convs, pointer + "/conversations", false, as_object, false));
    } else if (json::find(data, "role")) {
      Json arr = Json::array();
      arr.push_back(data);
      locate(pointer, 0, false);
      LOOM_TRY_ASSIGN(Conversation c, import_message_list(arr, title));
      results.push_back(std::move(c));
    } else if (const Json* d = json::find(data, "data"); d && d->is_array()) {
      return import_json_data_at(*d, title, pointer + "/data", {});
    } else {
      LOOM_TRY(element_of(data, pointer, 0, true, as_object, false));
    }
  } else {
    if (!ledger_) return unrecognized_structure();
    locate(pointer, 0, true);
    LOOM_TRY(keep_unknown(data, 0, true, "document is neither an object nor an array"));
  }
  return results;
}

// ── import_json / import_jsonl bodies + streaming ───────────────────
Result<std::vector<Conversation>> ConversationImporter::parse_whole_json_file(
    const fs::path& path, const std::optional<std::string>& title) {
  LOOM_TRY_ASSIGN(std::string text, fsutil::read_file(path));
  LOOM_TRY_ASSIGN(Json parsed, json::parse(text));
  return import_json_data_at(parsed, title, "", text);
}

// Streams the file through JsonArrayStreamer with bounded memory (Python
// _stream_json_array / _iter_json_elements). Falls back to a full parse when
// the file does not start a top-level JSON array.
Result<std::vector<Conversation>> ConversationImporter::stream_json_array(const fs::path& path,
                                                                          const ImportOptions& opts,
                                                                          std::int64_t total_size) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return Error(Errc::Io, "cannot open " + path.string());
  if (ledger_) ledger_->measured("json");

  JsonArrayStreamer streamer;
  std::vector<Conversation> results;
  bool not_array = false;
  std::size_t conv_count = 0;
  std::optional<Error> stream_error;
  bool first_element = true;
  bool message_array = false;
  Json messages = Json::array();
  std::int64_t element_index = 0;

  auto on_element = [&](std::string_view raw) {
    if (stream_error) return;
    const std::int64_t index = element_index++;
    cursor_pointer_ = "/" + std::to_string(index);
    cursor_locator_ = Json{{"json_pointer", cursor_pointer_}, {"element_index", index}};
    auto parsed = json::parse(raw);
    if (!parsed) {
      if (!ledger_) { stream_error = parsed.error(); return; }
      auto kept = keep_unmapped(recognition::kOpaque, "loom.importer.json", "array element is not valid JSON: " + parsed.error().message,
                                "corrupt JSON element " + cursor_pointer_, cursor_locator_, raw);
      if (!kept) stream_error = kept.error();
      return;
    }
    if (first_element) {
      first_element = false;
      message_array = parsed->is_object() && !json::find(*parsed, "mapping") &&
          (json::find(*parsed, "role") || json::find(*parsed, "content"));
    }
    if (message_array) {
      messages.push_back(std::move(*parsed));
      return;
    }
    auto r = import_single_element(*parsed, opts.title);
    if (!r) {
      if (ledger_ && r.error().code == Errc::Unsupported) {
        auto kept = keep_unmapped(recognition::kUnrecognized, "loom.importer.json", r.error().message,
                                  "unrecognized JSON element " + cursor_pointer_, cursor_locator_, raw);
        if (!kept) stream_error = kept.error();
        return;
      }
      stream_error = r.error();
      return;
    }
    if (*r) {
      results.push_back(std::move(**r));
      ++conv_count;
      if (conv_count % 50 == 0) log::info(kLog, "Stream import: {} conversations processed", conv_count);
    }
  };

  std::string chunk(1 << 16, '\0');
  std::int64_t bytes_read = 0;
  while (f) {
    f.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    std::streamsize n = f.gcount();
    if (n <= 0) break;
    bytes_read += n;
    if (opts.progress) opts.progress(bytes_read, total_size > 0 ? total_size : -1, "json");
    if (!streamer.feed(std::string_view(chunk.data(), static_cast<std::size_t>(n)), on_element)) {
      not_array = true;
      break;
    }
    if (stream_error) return *stream_error;
    if (streamer.finished()) break;
    if (opts.cancel && opts.cancel->cancelled()) break;
  }

  if (not_array) {
    log::warn(kLog, "Stream: not a JSON array, falling back to a full parse");
    return parse_whole_json_file(path, opts.title);
  }
  if (ledger_ && !streamer.finished() && streamer.pending_bytes() > 0 && !(opts.cancel && opts.cancel->cancelled())) {
    // A started element that never closed: kept, not silently dropped.
    cursor_locator_ = Json{{"json_pointer", "/" + std::to_string(element_index)}, {"element_index", element_index}};
    LOOM_TRY(keep_unmapped(recognition::kOpaque, "loom.importer.json", "array element truncated before it closed",
                           "truncated JSON element", cursor_locator_, streamer.pending_text()));
  }
  if (message_array) {
    if (opts.cancel && opts.cancel->cancelled()) return results;
    // Direct message arrays describe one conversation on both sides of the
    // size threshold; the existing batch writer retains its mapping rules.
    LOOM_TRY_ASSIGN(auto conversation, import_message_list(messages, opts.title));
    results.push_back(std::move(conversation));
  }
  log::info(kLog, "Stream import complete: {} conversations from {}", results.size(), path.filename().string());
  return results;
}

Result<std::vector<Conversation>> ConversationImporter::json_body(const fs::path& path, const ImportOptions& opts) {
  std::error_code ec;
  auto size = fs::file_size(path, ec);
  if (ec) return Error(Errc::Io, "cannot stat " + path.string() + ": " + ec.message());

  if (static_cast<std::int64_t>(size) > opts.stream_threshold_bytes) {
    log::info(kLog, "Large JSON ({} MB) - streaming import", size / 1'000'000);
    return stream_json_array(path, opts, static_cast<std::int64_t>(size));
  }
  return parse_whole_json_file(path, opts.title);
}

Result<std::vector<Conversation>> ConversationImporter::import_json(const fs::path& path, const ImportOptions& opts) {
  return with_source(path, "json", opts, "file", [&](const fs::path& input_path) { return json_body(input_path, opts); });
}

Result<std::vector<Conversation>> ConversationImporter::jsonl_body(const fs::path& path, const ImportOptions& opts) {
  LOOM_TRY_ASSIGN(std::string text, fsutil::read_file(path));
  std::vector<Conversation> results;
  Json messages = Json::array();
  if (ledger_) ledger_->measured("jsonl");

  std::size_t pos = 0;
  std::int64_t line_number = 0;
  while (pos <= text.size()) {
    std::size_t nl = text.find('\n', pos);
    std::string_view line_view(text.data() + pos, (nl == std::string::npos ? text.size() : nl) - pos);
    std::string_view line = utf8::strip(line_view);
    pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
    ++line_number;
    if (line.empty()) continue;

    cursor_pointer_ = "";
    const auto start = static_cast<std::int64_t>(line.data() - text.data());
    cursor_locator_ = Json{{"line", line_number}, {"byte_start", start},
                           {"byte_end", start + static_cast<std::int64_t>(line.size())}};
    auto parsed = json::parse(line);
    if (!parsed) {
      log::debug(kLog, "Non-JSON line in JSONL");
      LOOM_TRY(keep_unmapped(recognition::kOpaque, "loom.importer.jsonl", "line is not valid JSON: " + parsed.error().message,
                             "corrupt JSONL line " + std::to_string(line_number), cursor_locator_, line));
      continue;
    }
    if (parsed->is_object() && (json::find(*parsed, "role") || json::find(*parsed, "content"))) {
      messages.push_back(*parsed);
    } else if (parsed->is_object() && json::find(*parsed, "messages")) {
      auto r = import_conversation_obj(*parsed, opts.title);
      if (!r) return r.error();
      if (*r) results.push_back(std::move(**r));
    } else {
      LOOM_TRY(keep_unmapped(recognition::kUnrecognized, "loom.importer.jsonl",
                             "line is neither a message (role/content) nor a conversation (messages)",
                             "unrecognized JSONL line " + std::to_string(line_number), cursor_locator_, line));
    }
  }

  if (!messages.empty()) {
    LOOM_TRY_ASSIGN(Conversation c, import_message_list(messages, opts.title));
    results.push_back(std::move(c));
  }
  return results;
}

Result<std::vector<Conversation>> ConversationImporter::import_jsonl(const fs::path& path,
                                                                     const ImportOptions& opts) {
  return with_source(path, "jsonl", opts, "file", [&](const fs::path& input_path) { return jsonl_body(input_path, opts); });
}

}  // namespace loom
