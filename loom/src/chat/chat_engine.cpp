// OWNER: wave 2 net/chat/worker. Port of engine/chat_engine.py.
#include "loom/chat_engine.h"

#include <algorithm>
#include <cctype>
#include <map>

#include "loom/config.h"
#include "loom/event_bus.h"
#include "loom/graph_memory.h"
#include "loom/log.h"
#include "loom/memory_engine.h"
#include "loom/net/http.h"
#include "loom/net/sse.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/utf8.h"
#include "stub.h"

namespace loom {

namespace {

std::string rstrip_slash(std::string s) {
  while (!s.empty() && s.back() == '/') s.pop_back();
  return s;
}

std::string cfg_string(const Config& cfg, std::string_view key, std::string fallback = "") {
  Json v = cfg.get(key);
  return v.is_string() ? v.get<std::string>() : std::move(fallback);
}

double cfg_number(const Config& cfg, std::string_view key, double fallback) {
  Json v = cfg.get(key, fallback);
  return v.is_number() ? v.get<double>() : fallback;
}

bool cfg_bool(const Config& cfg, std::string_view key, bool fallback) {
  return json::truthy(cfg.get(key, fallback));
}

const std::vector<std::string>& image_exts() {
  static const std::vector<std::string> kExts = {".png", ".jpg", ".jpeg", ".gif", ".webp"};
  return kExts;
}
const std::vector<std::string>& text_exts() {
  static const std::vector<std::string> kExts = {".txt", ".py", ".md", ".json", ".csv", ".xml", ".html",
                                                  ".css", ".js", ".ts", ".yaml", ".yml", ".toml", ".ini",
                                                  ".cfg", ".sh", ".bat", ".rs", ".go", ".java", ".c",
                                                  ".cpp", ".h", ".sql"};
  return kExts;
}
constexpr std::size_t kMaxFileChars = 15000;

std::string lower_ext(const std::filesystem::path& p) {
  std::string e = p.extension().string();
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return e;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

Result<ChatOptions> ChatOptions::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "chat request must be a JSON object");
  ChatOptions o;
  for (auto it = j.begin(); it != j.end(); ++it) {
    const std::string& k = it.key();
    const Json& v = it.value();
    if (v.is_null() || k == "message" || k == "request_id") continue;
    auto bad = [&] { return Error(Errc::InvalidArgument, "invalid type for chat option: " + k); };
    if (k == "conv_id" || k == "model" || k == "reasoning_effort" || k == "system_prompt") {
      if (!v.is_string()) return bad();
      std::string s = v.get<std::string>();
      if (k == "conv_id") o.conv_id = s;
      if (k == "model") o.model = s;
      if (k == "reasoning_effort") o.reasoning_effort = s;
      if (k == "system_prompt") o.system_prompt = s;
    } else if (k == "attachments") {
      if (!v.is_array()) return bad();
      for (const auto& a : v) {
        if (!a.is_string()) return bad();
        o.attachments.push_back(a.get<std::string>());
      }
    } else if (k == "web_search" || k == "deep_research" || k == "stream") {
      if (!v.is_boolean()) return bad();
      if (k == "web_search") o.web_search = v.get<bool>();
      if (k == "deep_research") o.deep_research = v.get<bool>();
      if (k == "stream") o.stream = v.get<bool>();
    } else if (k == "temperature") {
      if (!v.is_number()) return bad();
      o.temperature = v.get<double>();
    } else if (k == "max_tokens" || k == "context_depth") {
      if (!v.is_number_integer()) return bad();
      if (k == "max_tokens") o.max_tokens = v.get<int>();
      if (k == "context_depth") o.context_depth = v.get<int>();
    } else {
      return Error(Errc::InvalidArgument, "unknown chat option: " + k);
    }
  }
  return o;
}

Json ChatResult::to_json() const {
  return Json{{"conv_id", conv_id},
              {"user_message_id", user_message_id},
              {"assistant_message_id", assistant_message_id},
              {"text", text},
              {"reasoning", reasoning ? Json(*reasoning) : Json(nullptr)},
              {"model", model},
              {"usage", usage},
              {"title", new_title ? Json(*new_title) : Json(nullptr)},
              {"cancelled", cancelled}};
}

ChatEngine::ChatEngine(const Config& cfg, const Secrets& secrets, Database& db, EventBus& bus,
                       net::HttpTransport& http, const SemanticAnalyzer& analyzer, MemoryEngine* memory,
                       GraphMemorySelector* graph_memory)
    : cfg_(cfg),
      secrets_(secrets),
      db_(db),
      bus_(bus),
      http_(http),
      analyzer_(analyzer),
      memory_(memory),
      graph_memory_(graph_memory) {}

Status ChatEngine::resume_last() {
  auto r = db_.list_convs(1);
  if (!r) return r.error();
  std::lock_guard lk(mu_);
  conv_ = r->empty() ? std::nullopt : std::optional<Conversation>(r->front());
  return ok_status();
}

Result<Conversation> ChatEngine::new_conv(std::string_view title) {
  auto r = db_.create_conv(title.empty() ? std::string_view("New Chat") : title);
  if (!r) return r.error();
  std::lock_guard lk(mu_);
  conv_ = *r;
  return *r;
}

Result<std::optional<Conversation>> ChatEngine::load_conv(std::string_view conv_id) {
  auto r = db_.get_conv(conv_id);
  if (!r) return r.error();
  std::lock_guard lk(mu_);
  conv_ = *r;
  return *r;
}

std::optional<Conversation> ChatEngine::current_conv() const {
  std::lock_guard lk(mu_);
  return conv_;
}

Json ChatEngine::build_content(std::string_view text, const std::vector<std::string>& attachments) const {
  if (attachments.empty()) return Json(std::string(text));

  Json parts = Json::array();
  parts.push_back(Json{{"type", "text"}, {"text", std::string(text)}});

  for (const auto& path_str : attachments) {
    std::filesystem::path p(path_str);
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
      log::warn("loom.chat_engine", "Attachment not found: {}", path_str);
      continue;
    }
    std::string ext = lower_ext(p);
    if (contains(image_exts(), ext)) {
      auto data = fsutil::read_file(p);
      if (!data) {
        log::error("loom.chat_engine", "Failed to encode image: {}", path_str);
        continue;
      }
      std::string mime = "image/" + ext.substr(1);
      if (mime == "image/jpg") mime = "image/jpeg";
      parts.push_back(Json{{"type", "image_url"},
                           {"image_url", Json{{"url", "data:" + mime + ";base64," + base64::encode(*data)}}}});
    } else if (contains(text_exts(), ext)) {
      auto data = fsutil::read_file(p);
      if (!data) {
        log::error("loom.chat_engine", "Failed to read text file: {}", path_str);
        continue;
      }
      std::string repaired = utf8::repair(*data);
      std::string file_text(utf8::prefix(repaired, kMaxFileChars));
      std::string name = p.filename().string();
      parts.push_back(
          Json{{"type", "text"}, {"text", "\n--- FILE: " + name + " ---\n" + file_text + "\n--- END FILE ---"}});
    }
  }
  return parts;
}

Result<Json> ChatEngine::build_messages(std::string_view conv_id, std::string_view current_text,
                                        const std::vector<std::string>& attachments, const ChatOptions& opts,
                                        std::string_view exclude_message_id) {
  Json messages = Json::array();

  std::string sys_prompt = opts.system_prompt ? *opts.system_prompt : cfg_string(cfg_, "system_prompt");
  if (!sys_prompt.empty()) messages.push_back(Json{{"role", "system"}, {"content", sys_prompt}});

  if (memory_) {
    std::string mem_ctx = memory_->get_active_context();
    if (!mem_ctx.empty()) {
      messages.push_back(Json{{"role", "system"}, {"content", "User's memory/context:\n" + mem_ctx}});
    }
  }

  bool graph_disabled = opts.context_depth.has_value() && *opts.context_depth == 0;
  if (graph_memory_ && !graph_disabled) {
    try {
      GraphSelectOptions gopts;
      if (opts.context_depth && *opts.context_depth > 0) gopts.depth = *opts.context_depth;
      if (!conv_id.empty()) gopts.current_conv_id = std::string(conv_id);
      std::string graph_ctx = graph_memory_->select_context(current_text, gopts);
      if (!graph_ctx.empty()) messages.push_back(Json{{"role", "system"}, {"content", graph_ctx}});
    } catch (const std::exception& e) {
      log::debug("loom.chat_engine", "Graph memory selection failed: {}", e.what());
    }
  }

  if (!conv_id.empty()) {
    auto msgs = db_.get_msgs(conv_id);
    if (!msgs) return msgs.error();
    for (const auto& m : *msgs) {
      if (m.status != msg_status::kActive) continue;
      if (!exclude_message_id.empty() && m.id == exclude_message_id) continue;
      std::string content = m.text;
      if (m.weight > 1.5) {
        content = "[IMPORTANT] " + content;
      } else if (m.weight < 0.5) {
        content = "[low priority] " + content;
      }
      messages.push_back(Json{{"role", m.role}, {"content", content}});
    }
  }

  messages.push_back(Json{{"role", "user"}, {"content", build_content(current_text, attachments)}});
  return messages;
}

void ChatEngine::configure_reasoning(Json& payload, std::string_view model, const std::optional<std::string>& effort) {
  std::string lower(model);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  static const std::vector<std::string> kThinkingIndicators = {"opus", "sonnet", "o1", "o3", "r1", "thinking", "deepseek"};
  bool is_thinking = std::any_of(kThinkingIndicators.begin(), kThinkingIndicators.end(),
                                 [&](const std::string& s) { return lower.find(s) != std::string::npos; });
  if (!is_thinking) return;

  Json reasoning{{"enabled", true}};
  std::string model_str(model);
  static const std::vector<std::string> kNewClaudeMarkers = {"4.6", "4-6", "4.5", "4-5"};
  bool is_claude_new = std::any_of(kNewClaudeMarkers.begin(), kNewClaudeMarkers.end(),
                                   [&](const std::string& v) { return model_str.find(v) != std::string::npos; });

  if (is_claude_new) {
    if (effort && *effort == "max") {
      payload["verbosity"] = "max";
    } else if (effort && *effort != "adaptive") {
      static const std::map<std::string, int> kBudget = {{"low", 5000}, {"medium", 15000}, {"high", 30000}};
      auto it = kBudget.find(*effort);
      reasoning["max_tokens"] = it != kBudget.end() ? it->second : 15000;
    }
  } else if (effort) {
    reasoning["effort"] = *effort;
  }
  payload["reasoning"] = reasoning;
}

std::optional<std::string> ChatEngine::last_reasoning() const {
  std::lock_guard lk(mu_);
  return last_reasoning_;
}

Result<ChatResult> ChatEngine::send(std::string_view text, const ChatOptions& opts, const ChatCallbacks& cb,
                                    const CancelToken* cancel) {
  // 1. Resolve the conversation.
  Conversation conv;
  {
    std::lock_guard lk(mu_);
    if (opts.conv_id) {
      auto r = db_.get_conv(*opts.conv_id);
      if (!r) return r.error();
      if (!*r) return Error(Errc::NotFound, "conversation not found: " + *opts.conv_id);
      conv = **r;
    } else if (conv_) {
      conv = *conv_;
    } else {
      auto r = db_.create_conv("New Chat");
      if (!r) return r.error();
      conv = *r;
    }
    conv_ = conv;
  }

  // 2. Persist the user message.
  NewMessage nm;
  nm.conv_id = conv.id;
  nm.text = std::string(text);
  nm.role = "user";
  if (!opts.attachments.empty()) {
    Json atts = Json::array();
    for (const auto& a : opts.attachments) atts.push_back(a);
    nm.attachments = atts;
  }
  auto umid = db_.create_msg(nm);
  if (!umid) return umid.error();
  std::string user_mid = *umid;

  bus_.emit(events::kMsgCreated,
           Json{{"id", user_mid}, {"text", std::string(text)}, {"conv_id", conv.id}, {"role", "user"}});
  if (cb.on_start) cb.on_start(conv.id, user_mid);

  // 3. Build the message array (history excludes the message just persisted).
  auto msgs_json = build_messages(conv.id, text, opts.attachments, opts, user_mid);
  if (!msgs_json) return msgs_json.error();

  std::string model = opts.model.value_or(cfg_string(cfg_, "default_model"));

  // 5. Call the API.
  std::string key = secrets_.get_string("api_key");
  if (key.empty()) return Error(Errc::Auth, "No API key configured - open Settings");

  std::string base = rstrip_slash(cfg_string(cfg_, "base_url"));
  bool stream = opts.stream && static_cast<bool>(cb.on_chunk);

  double temperature = opts.temperature.value_or(cfg_number(cfg_, "temperature", 0.7));
  int max_tokens = opts.max_tokens.value_or(static_cast<int>(cfg_number(cfg_, "max_tokens", 4096)));

  Json payload{{"model", model}, {"messages", *msgs_json}, {"temperature", temperature},
              {"max_tokens", max_tokens}, {"stream", stream}};

  if (opts.web_search || opts.deep_research) {
    Json plugin{{"id", "web"}};
    if (opts.deep_research) {
      plugin["max_results"] = 10;
      payload["web_search_options"] = Json{{"search_context_size", "high"}};
    }
    payload["plugins"] = Json::array({plugin});
  }

  configure_reasoning(payload, model, opts.reasoning_effort);
  payload["include_reasoning"] = true;

  net::HttpRequest req;
  req.method = "POST";
  req.url = base + "/chat/completions";
  req.headers = {
      {"Authorization", "Bearer " + key},
      {"Content-Type", "application/json"},
      {"HTTP-Referer", "https://github.com/chatadhd"},
      {"X-Title", "ChatADHD"},
  };
  req.body = json::dump(payload);
  req.timeout_ms = 180000;
  req.stream = stream;

  std::string full_text, reasoning_text;
  Json usage = Json::object();
  bool cancelled = false;

  auto handle_event = [&](const net::SseEvent& evt) {
    if (evt.is_done()) return;
    auto parsed = json::parse(evt.data);
    if (!parsed) {
      log::debug("loom.chat_engine", "Skipped malformed SSE chunk: {}", std::string(utf8::prefix(evt.data, 100)));
      return;
    }
    const Json* choices = json::find(*parsed, "choices");
    if (choices && choices->is_array() && !choices->empty()) {
      const Json* delta = json::find((*choices)[0], "delta");
      if (delta) {
        std::string chunk_text = json::get_string(*delta, "content");
        if (!chunk_text.empty()) {
          full_text += chunk_text;
          if (cb.on_chunk) cb.on_chunk(chunk_text);
        }
        std::string r = json::get_string(*delta, "reasoning");
        if (!r.empty()) {
          reasoning_text += r;
          if (cb.on_reasoning) cb.on_reasoning(r);
        }
      }
    }
    if (const Json* u = json::find(*parsed, "usage"); u && u->is_object() && !u->empty()) usage = *u;
  };

  if (stream) {
    net::SseParser parser;
    net::StreamSink sink;
    int status = 0;
    bool aborted = false;
    std::string err_body;

    sink.on_headers = [&](int s, const net::Headers&) {
      status = s;
      return true;
    };
    sink.on_data = [&](std::string_view chunk) -> bool {
      if (cancel && cancel->cancelled()) {
        aborted = true;
        return false;
      }
      if (status != 200) {
        err_body.append(chunk);
        return true;
      }
      parser.feed(chunk, handle_event);
      return true;
    };

    auto resp = http_.send(req, &sink, cancel);
    bool was_cancelled = aborted || (cancel && cancel->cancelled()) || (!resp && resp.error().code == Errc::Cancelled);
    if (was_cancelled) {
      cancelled = true;
    } else if (!resp) {
      return resp.error();
    } else if (status != 200) {
      return Error(Errc::Http, "API error " + std::to_string(status) + ": " + std::string(utf8::prefix(err_body, 500)));
    } else {
      parser.finish(handle_event);
    }
  } else {
    auto resp = http_.send(req, nullptr, cancel);
    if (!resp) {
      if (resp.error().code == Errc::Cancelled) {
        cancelled = true;
      } else {
        return resp.error();
      }
    } else if (resp->status != 200) {
      return Error(Errc::Http, "API error " + std::to_string(resp->status) + ": " + std::string(utf8::prefix(resp->body, 500)));
    } else {
      auto j = resp->json();
      if (!j) return Error(Errc::Parse, "invalid JSON response from chat completions");
      const Json* choices = json::find(*j, "choices");
      if (!choices || !choices->is_array() || choices->empty()) return Error(Errc::Parse, "response missing choices");
      const Json* message = json::find((*choices)[0], "message");
      if (!message) return Error(Errc::Parse, "response missing message");
      full_text = json::get_string(*message, "content");
      reasoning_text = json::get_string(*message, "reasoning");
      if (const Json* u = json::find(*j, "usage"); u && u->is_object()) usage = *u;
    }
  }

  // 7. Persist the assistant message.
  Json metadata = Json::object();
  if (!reasoning_text.empty()) metadata["reasoning"] = std::string(utf8::prefix(reasoning_text, 2000));
  {
    auto topics = analyzer_.extract_topics(full_text, 2);
    if (!topics.empty()) metadata["topics"] = topics;
  }
  if (cancelled) metadata["cancelled"] = true;

  NewMessage anm;
  anm.conv_id = conv.id;
  anm.text = full_text;
  anm.role = "assistant";
  anm.model = model;
  anm.metadata = metadata;
  auto amid = db_.create_msg(anm);
  if (!amid) return amid.error();
  std::string asst_mid = *amid;

  bus_.emit(events::kMsgCreated,
           Json{{"id", asst_mid}, {"text", full_text}, {"conv_id", conv.id}, {"role", "assistant"}});

  {
    std::lock_guard lk(mu_);
    last_reasoning_ = reasoning_text.empty() ? std::nullopt : std::optional<std::string>(reasoning_text);
  }

  ChatResult result;
  result.conv_id = conv.id;
  result.user_message_id = user_mid;
  result.assistant_message_id = asst_mid;
  result.text = full_text;
  result.reasoning = reasoning_text.empty() ? std::nullopt : std::optional<std::string>(reasoning_text);
  result.model = model;
  result.usage = usage;
  result.cancelled = cancelled;

  // 8. Auto-title from the first exchange.
  auto all_msgs = db_.get_msgs(conv.id);
  if (all_msgs && all_msgs->size() <= 2 && cfg_bool(cfg_, "auto_title", true)) {
    std::string title = std::string(utf8::prefix(text, 30)) + (utf8::length(text) > 30 ? "..." : "");
    ConvPatch patch;
    patch.title = title;
    if (db_.update_conv(conv.id, patch)) {
      result.new_title = title;
      std::lock_guard lk(mu_);
      if (conv_ && conv_->id == conv.id) conv_->title = title;
    }
  }

  return result;
}

}  // namespace loom
