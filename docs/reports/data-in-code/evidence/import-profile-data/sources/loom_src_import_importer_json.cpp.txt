// JSON / JSONL: routing (_import_json_data), the ChatGPT mapping-tree walk,
// Claude chat_messages export, generic conversation objects, and the
// streaming path for files over ImportOptions::stream_threshold_bytes.
#include <fstream>
#include <unordered_map>

#include "importer_internal.h"
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
  return std::optional<Conversation>(std::move(conv));
}

// ── Generic conversation objects (messages | chat_messages | items | data) ─
Result<std::optional<Conversation>> ConversationImporter::import_conversation_obj(
    const Json& conv_data, const std::optional<std::string>& title) {
  if (!conv_data.is_object()) return std::optional<Conversation>{};
  std::string conv_title =
      resolve_title(title, conv_data, {"title", "name", "conversation_name"}, "Import " + timeutil::local_now_format("%H:%M"));

  Json messages = Json::array();
  for (const char* key : {"messages", "chat_messages", "items", "data"}) {
    if (const Json* v = json::find(conv_data, key); v && json::truthy(*v)) {
      messages = *v;
      break;
    }
  }
  if (messages.is_object()) {
    Json arr = Json::array();
    for (auto it = messages.begin(); it != messages.end(); ++it) arr.push_back(it.value());
    messages = arr;
  }
  if (!json::truthy(messages)) return std::optional<Conversation>{};

  LOOM_TRY_ASSIGN(Conversation conv, import_message_list(messages, conv_title));
  return std::optional<Conversation>(std::move(conv));
}

// ── Single-element + full-document routing ──────────────────────────
Result<std::optional<Conversation>> ConversationImporter::import_single_element(
    const Json& element, const std::optional<std::string>& title) {
  if (!element.is_object()) return std::optional<Conversation>{};
  if (json::find(element, "mapping")) return import_chatgpt_mapping(element, title);
  if (json::find(element, "chat_messages")) return import_claude_export(element, title);
  return import_conversation_obj(element, title);
}

Result<std::vector<Conversation>> ConversationImporter::import_json_data(const Json& data,
                                                                         const std::optional<std::string>& title) {
  std::vector<Conversation> results;

  if (data.is_array()) {
    if (data.empty()) return results;
    const Json& first = data.front();
    if (first.is_object()) {
      if (json::find(first, "mapping")) {
        for (const auto& conv_data : data) {
          LOOM_TRY_ASSIGN(auto r, import_chatgpt_mapping(conv_data, title));
          if (r) results.push_back(std::move(*r));
        }
      } else if (json::find(first, "role") || json::find(first, "content")) {
        LOOM_TRY_ASSIGN(Conversation c, import_message_list(data, title));
        results.push_back(std::move(c));
      } else if (json::find(first, "messages")) {
        for (const auto& conv_data : data) {
          LOOM_TRY_ASSIGN(auto r, import_conversation_obj(conv_data, title));
          if (r) results.push_back(std::move(*r));
        }
      } else if (json::find(first, "chat_messages")) {
        for (const auto& conv_data : data) {
          LOOM_TRY_ASSIGN(auto r, import_claude_export(conv_data, title));
          if (r) results.push_back(std::move(*r));
        }
      } else {
        for (const auto& item : data) {
          auto r = import_conversation_obj(item, title);
          if (r && *r) results.push_back(std::move(**r));
          // Python swallows per-item exceptions here (log.debug + continue);
          // import_conversation_obj never hard-fails on a malformed item
          // (non-dict -> nullopt), so there is nothing to swallow.
        }
      }
    }
  } else if (data.is_object()) {
    if (json::find(data, "mapping")) {
      LOOM_TRY_ASSIGN(auto r, import_chatgpt_mapping(data, title));
      if (r) results.push_back(std::move(*r));
    } else if (json::find(data, "chat_messages")) {
      LOOM_TRY_ASSIGN(auto r, import_claude_export(data, title));
      if (r) results.push_back(std::move(*r));
    } else if (json::find(data, "messages")) {
      LOOM_TRY_ASSIGN(auto r, import_conversation_obj(data, title));
      if (r) results.push_back(std::move(*r));
    } else if (const Json* convs = json::find(data, "conversations"); convs && convs->is_array()) {
      for (const auto& conv_data : *convs) {
        LOOM_TRY_ASSIGN(auto r, import_conversation_obj(conv_data, title));
        if (r) results.push_back(std::move(*r));
      }
    } else if (json::find(data, "role")) {
      Json arr = Json::array();
      arr.push_back(data);
      LOOM_TRY_ASSIGN(Conversation c, import_message_list(arr, title));
      results.push_back(std::move(c));
    } else if (const Json* d = json::find(data, "data"); d && d->is_array()) {
      return import_json_data(*d, title);
    } else {
      auto r = import_conversation_obj(data, title);
      if (r && *r) results.push_back(std::move(**r));
    }
  }
  return results;
}

// ── import_json / import_jsonl bodies + streaming ───────────────────
namespace {

Result<std::vector<Conversation>> parse_whole_file(ConversationImporter& self, const fs::path& path,
                                                    const std::optional<std::string>& title) {
  LOOM_TRY_ASSIGN(std::string text, fsutil::read_file(path));
  LOOM_TRY_ASSIGN(Json parsed, json::parse(text));
  return self.import_json_data(parsed, title);
}

// Streams the file through JsonArrayStreamer with bounded memory (Python
// _stream_json_array / _iter_json_elements). Falls back to a full parse when
// the file does not start a top-level JSON array.
Result<std::vector<Conversation>> stream_json_array(ConversationImporter& self, const fs::path& path,
                                                    const ImportOptions& opts, std::int64_t total_size) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return Error(Errc::Io, "cannot open " + path.string());

  JsonArrayStreamer streamer;
  std::vector<Conversation> results;
  bool not_array = false;
  std::size_t conv_count = 0;

  auto on_element = [&](std::string_view raw) {
    auto parsed = json::parse(raw);
    if (!parsed) {
      log::debug(kLog, "stream: invalid JSON element (len={})", raw.size());
      return;
    }
    auto r = self.import_single_element(*parsed, opts.title);
    if (r && *r) {
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
    if (streamer.finished()) break;
    if (opts.cancel && opts.cancel->cancelled()) break;
  }

  if (not_array) {
    log::warn(kLog, "Stream: not a JSON array, falling back to a full parse");
    return parse_whole_file(self, path, opts.title);
  }
  log::info(kLog, "Stream import complete: {} conversations from {}", conv_count, path.filename().string());
  return results;
}

}  // namespace

Result<std::vector<Conversation>> ConversationImporter::json_body(const fs::path& path, const ImportOptions& opts) {
  std::error_code ec;
  auto size = fs::file_size(path, ec);
  if (ec) return Error(Errc::Io, "cannot stat " + path.string() + ": " + ec.message());

  if (static_cast<std::int64_t>(size) > opts.stream_threshold_bytes) {
    log::info(kLog, "Large JSON ({} MB) - streaming import", size / 1'000'000);
    return stream_json_array(*this, path, opts, static_cast<std::int64_t>(size));
  }
  return parse_whole_file(*this, path, opts.title);
}

Result<std::vector<Conversation>> ConversationImporter::import_json(const fs::path& path, const ImportOptions& opts) {
  return with_source(path, "json", opts, "file", [&](const fs::path& input_path) { return json_body(input_path, opts); });
}

Result<std::vector<Conversation>> ConversationImporter::jsonl_body(const fs::path& path, const ImportOptions& opts) {
  LOOM_TRY_ASSIGN(std::string text, fsutil::read_file(path));
  std::vector<Conversation> results;
  Json messages = Json::array();

  std::size_t pos = 0;
  while (pos <= text.size()) {
    std::size_t nl = text.find('\n', pos);
    std::string_view line_view(text.data() + pos, (nl == std::string::npos ? text.size() : nl) - pos);
    std::string_view line = utf8::strip(line_view);
    pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
    if (line.empty()) continue;

    auto parsed = json::parse(line);
    if (!parsed) {
      log::debug(kLog, "Skipped non-JSON line in JSONL");
      continue;
    }
    if (parsed->is_object() && (json::find(*parsed, "role") || json::find(*parsed, "content"))) {
      messages.push_back(*parsed);
    } else if (parsed->is_object() && json::find(*parsed, "messages")) {
      auto r = import_conversation_obj(*parsed, opts.title);
      if (!r) return r.error();
      if (*r) results.push_back(std::move(**r));
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
