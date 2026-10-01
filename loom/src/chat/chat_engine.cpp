// OWNER: wave 2 net/chat/worker. Port of engine/chat_engine.py.
#include "loom/chat_engine.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <limits>
#include <set>

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
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"
#include "stub.h"
#include "active_task_spec.h"
#include "active_task_acceptance.h"

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

Result<context::ContextRequest> chat_context_request(const Json& value) {
  if (!value.is_object()) return Error(Errc::InvalidArgument, "knowledge_context must be an object");
  for (auto it = value.begin(); it != value.end(); ++it) {
    const auto& k = it.key();
    const auto& v = it.value();
    bool valid = false;
    if (k == "text" || k == "project" || k == "run" || k == "lang") valid = v.is_string();
    else if (k == "goal_type") valid = v.is_string() || v.is_null();
    else if (k == "targets") {
      valid = v.is_array() && std::all_of(v.begin(), v.end(), [](const Json& e) { return e.is_string(); });
    } else if (k == "budget_tokens") {
      valid = v.is_number_integer() && v > 0 && v <= std::numeric_limits<int>::max();
    } else if (k == "relation_hops") {
      valid = v.is_number_integer() && v >= 0 && v <= std::numeric_limits<int>::max();
    } else if (k == "detail_resolution") {
      valid = v.is_null() || v == "label" || v == "summary" || v == "full" || v == "raw";
    } else if (k == "plan" || k == "claim_targets" || k == "include_counter_evidence" ||
               k == "candidate_channels" || k == "candidate_scan_limit" || k == "lexical_shadow") {
      // These extensions have strict nested validators in ContextRequest.
      // Keep one schema: permit only their names here, then delegate below.
      // In particular plan.source_ref is caller-declared provenance, not an
      // ActiveTaskSpec binding or permission to compile/infer task statements.
      valid = true;
    }
    if (!valid) return Error(Errc::InvalidArgument, "invalid knowledge_context option: " + k);
  }
  return context::ContextRequest::from_json(value);
}

Json native_history_row(const Message& message) {
  return Json{{"id", message.id}, {"conv_id", message.conv_id}, {"status", message.status},
              {"role", message.role}, {"text", message.text}, {"weight", message.weight}};
}

Result<Json> native_history_projection(Database& db, std::string_view conversation, bool include_history) {
  Json projection = Json::array();
  if (!include_history || conversation.empty()) return projection;
  auto messages = db.get_msgs(conversation);
  if (!messages) return messages.error();
  for (const auto& message : *messages) {
    if (message.status == msg_status::kActive) projection.push_back(native_history_row(message));
  }
  return projection;
}

Result<Json> native_request_inputs(Database& db, std::string_view conversation) {
  auto messages = db.get_msgs(conversation, true);
  if (!messages) return messages.error();
  Json snapshot = Json::array();
  for (const auto& message : *messages) snapshot.push_back(message.to_json());
  return snapshot;
}

Result<Json> prepare_active_task(Database& db, std::string_view conv_id, const ChatOptions& opts,
                                const std::map<std::string, Json>& in_flight = {}) {
  auto invalid = [](std::string_view reason) -> Result<Json> {
    return Error(Errc::InvalidArgument, "active_task: " + std::string(reason));
  };
  if (!opts.active_task_spec) {
    if (!opts.active_task_bindings.is_object() || !opts.active_task_bindings.empty() ||
        opts.active_task_history != "replace_refinement") {
      return invalid("bindings and history mode require a specification");
    }
    return Json(nullptr);
  }
  if (opts.active_task_history != "replace_refinement" && opts.active_task_history != "append") {
    return invalid("unknown history mode");
  }
  auto compiled = chat::compile_active_task_spec(*opts.active_task_spec);
  if (!compiled) return compiled.error();
  const auto& spec = *compiled;
  if (conv_id.empty() || spec["scope"]["conversation_id"] != conv_id) {
    return invalid("specification must name the current existing conversation");
  }
  if (spec["scope"]["branch_id"] != "native:active") {
    return invalid("only the native:active source selection is supported");
  }
  const auto& bindings = opts.active_task_bindings;
  if (!bindings.is_object() || bindings.size() != spec["history_event_ids"].size()) {
    return invalid("bindings must cover exactly the declared history events");
  }
  Json sources = Json::array();
  std::set<std::string> message_ids;
  std::map<std::string, std::string> source_texts;
  for (const auto& event : spec["history_event_ids"]) {
    const auto id = event.get<std::string>();
    auto bound = bindings.find(id);
    if (bound == bindings.end() || !bound->is_object() || bound->size() != 2 ||
        !bound->contains("message_id") || !(*bound)["message_id"].is_string() ||
        !bound->contains("text_sha256") || !(*bound)["text_sha256"].is_string()) {
      return invalid("each event needs a message_id and text_sha256 binding");
    }
    const auto mid = (*bound)["message_id"].get<std::string>();
    if (mid.empty() || !message_ids.insert(mid).second) return invalid("message bindings must be nonempty and unique");
    auto message = db.get_msg(mid);
    if (!message) return message.error();
    if (!*message || (**message).conv_id != conv_id || (**message).status != msg_status::kActive) {
      return invalid("bound message is absent or outside the active conversation selection");
    }
    const auto& m = **message;
    if (!utf8::is_valid(m.text)) return invalid("bound source text must be valid UTF-8");
    const auto digest = Sha256::hex(m.text);
    if ((*bound)["text_sha256"] != digest) return invalid("bound source text changed");
    source_texts.emplace(id, m.text);
    sources.push_back(Json{{"event_id", id}, {"message_id", mid}, {"role", m.role}, {"text", m.text},
                           {"text_sha256", digest}, {"status", m.status},
                           {"created", m.created}, {"attachments", m.attachments},
                           {"version_group_id", m.version_group_id ? Json(*m.version_group_id) : Json(nullptr)},
                           {"version_num", m.version_num}});
  }
  for (const auto& source : spec["source_refs"]) {
    if (source.contains("quote") &&
        source_texts.at(source["event_id"].get<std::string>()).find(source["quote"].get<std::string>()) == std::string::npos) {
      return invalid("source quote does not occur in the bound native message");
    }
  }

  auto history = chat::read_active_task_history(db, conv_id, in_flight);
  if (!history) return history.error();
  const auto& retained_tasks = history->snapshots;
  const Json* latest = nullptr;
  std::map<std::string, const Json*> products;
  std::map<std::uint64_t, std::string> versions;
  for (auto retained = retained_tasks.begin(); retained != retained_tasks.end(); ++retained) {
    if (!chat::valid_active_task_snapshot(*retained)) {
      return invalid("retained revision metadata is malformed");
    }
    auto old = retained->find("supplied_spec");
    if ((*old)["product_ref"] == spec["product_ref"] &&
        (*old != *opts.active_task_spec || retained->value("bindings", Json()) != bindings ||
         (*retained)["source_messages"] != sources)) {
      return invalid("a product identifier cannot be reused for different content or bindings");
    }
    if (chat::active_task_scope_key((*old)["scope"]) != chat::active_task_scope_key(spec["scope"])) continue;
    const auto product_id = (*old)["product_ref"]["id"].get<std::string>();
    auto [product, added] = products.emplace(product_id, &*retained);
    if (!added && !chat::same_active_task_identity(*product->second, *retained)) {
      return invalid("retained product identity has conflicting content");
    }
    auto [version, first] = versions.emplace((*old)["version"].get<std::uint64_t>(), product_id);
    if (!first && version->second != product_id) return invalid("retained task has competing version identities");
    if (!latest || (*old)["version"] > (*latest)["supplied_spec"]["version"]) latest = &*retained;
  }
  // Recover the exact ancestry, not every object with a coincidentally equal
  // goal or version. This keeps source coverage when a new spec binds only its
  // new refinement messages; it never merges or invents statement content.
  std::vector<const Json*> ancestry;
  std::set<std::string> seen_products;
  for (const Json* ancestor = latest; ancestor;) {
    const auto& old = (*ancestor)["supplied_spec"];
    const auto id = old["product_ref"]["id"].get<std::string>();
    if (!seen_products.insert(id).second || old["goal_id"] != spec["goal_id"]) {
      return invalid("retained task ancestry is cyclic or changes goal");
    }
    ancestry.push_back(ancestor);
    const auto number = old["version"].get<std::uint64_t>();
    if (old["previous_product_ref"].is_null()) {
      if (number != 1) return invalid("retained ancestry has no version-one root");
      break;
    }
    auto previous = products.find(old["previous_product_ref"]["id"].get<std::string>());
    if (number <= 1 || previous == products.end() ||
        (*previous->second)["supplied_spec"]["version"].get<std::uint64_t>() != number - 1) {
      return invalid("retained ancestry has a missing or inconsistent predecessor");
    }
    ancestor = previous->second;
  }
  if (latest) {
    const auto& old = (*latest)["supplied_spec"];
    const bool replay = old["product_ref"] == spec["product_ref"] && old == *opts.active_task_spec &&
                        (*latest)["bindings"] == bindings;
    const auto previous_version = old["version"].get<std::uint64_t>();
    if (!replay && (spec["previous_product_ref"] != old["product_ref"] ||
                   previous_version == std::numeric_limits<std::uint64_t>::max() ||
                   spec["version"].get<std::uint64_t>() != previous_version + 1 ||
                   spec["goal_id"] != old["goal_id"])) {
      return invalid("revision must extend the latest retained product of this task and goal");
    }
  } else if (spec["version"] != 1 || !spec["previous_product_ref"].is_null()) {
    return invalid("a new task must start with version 1 and no predecessor");
  }
  Json covered = Json::array();
  Json inherited = Json::array();
  for (const auto& source : sources) covered.push_back(source["message_id"]);
  std::set<std::string> covered_ids = message_ids;
  for (const auto* ancestor : ancestry) {
    for (const auto& source : (*ancestor)["source_messages"]) {
      const auto mid = source["message_id"].get<std::string>();
      // Current explicit bindings, then the most recent ancestor, take priority.
      if (!covered_ids.insert(mid).second) continue;
      if (opts.include_history && opts.active_task_history == "replace_refinement") {
        auto live = db.get_msg(mid);
        if (!live) return live.error();
        if (*live && (**live).status == msg_status::kActive && (**live).conv_id == conv_id &&
            source["text_sha256"] != Sha256::hex((**live).text)) {
          return invalid("inherited active source changed; explicitly rebind it before replacing history");
        }
        // Native edits create a new active row in the same version group.
        // Its bytes are not covered by the accepted ancestor's snapshot.
        if (source.contains("version_group_id") && source["version_group_id"].is_string()) {
          auto siblings = db.get_versions(source["version_group_id"].get<std::string>());
          if (!siblings) return siblings.error();
          for (const auto& sibling : *siblings) {
            if (sibling.status == msg_status::kActive && sibling.conv_id == conv_id &&
                sibling.id != mid && !covered_ids.contains(sibling.id)) {
              return invalid("inherited source has an active edited version; explicitly bind it before replacing history");
            }
          }
        }
      }
      covered.push_back(mid);
      inherited.push_back(Json{{"product_ref", (*ancestor)["supplied_spec"]["product_ref"]},
                               {"source_message", source}});
    }
  }
  return Json{{"schema", "loom.chat_active_task/1"}, {"supplied_spec", *opts.active_task_spec},
              {"compiled_spec", *compiled}, {"bindings", bindings}, {"source_messages", sources},
              {"history_coverage_message_ids", covered}, {"inherited_source_messages", inherited},
              {"history_mode", opts.active_task_history}, {"acceptance", "explicit_caller_supplied"},
              {"compiler", {{"id", "loom.active_task_renderer"}, {"version", "1"}}},
              {"binding_verification", "native_message_text_sha256_and_optional_quote"},
              {"source_selection", "native:active"}};
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
    } else if (k == "web_search" || k == "deep_research" || k == "stream" ||
               k == "include_memory" || k == "include_graph_memory" || k == "include_history" || k == "trace_context") {
      if (!v.is_boolean()) return bad();
      if (k == "web_search") o.web_search = v.get<bool>();
      if (k == "deep_research") o.deep_research = v.get<bool>();
      if (k == "stream") o.stream = v.get<bool>();
      if (k == "include_memory") o.include_memory = v.get<bool>();
      if (k == "include_graph_memory") o.include_graph_memory = v.get<bool>();
      if (k == "include_history") o.include_history = v.get<bool>();
      if (k == "trace_context") o.trace_context = v.get<bool>();
    } else if (k == "knowledge_context") {
      auto context = chat_context_request(v);
      if (!context) return context.error();
      o.knowledge_context = *context;
    } else if (k == "active_task_spec") {
      auto compiled = chat::compile_active_task_spec(v);
      if (!compiled) return compiled.error();
      o.active_task_spec = v;
    } else if (k == "active_task_bindings") {
      if (!v.is_object()) return bad();
      o.active_task_bindings = v;
    } else if (k == "active_task_history") {
      if (!v.is_string() || (v != "replace_refinement" && v != "append")) return bad();
      o.active_task_history = v.get<std::string>();
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
  if (!o.active_task_spec && (j.contains("active_task_bindings") || j.contains("active_task_history"))) {
    return Error(Errc::InvalidArgument, "active_task options require active_task_spec");
  }
  return o;
}

Json ChatResult::to_json() const {
  Json out{{"conv_id", conv_id},
              {"user_message_id", user_message_id},
              {"assistant_message_id", assistant_message_id},
              {"text", text},
              {"reasoning", reasoning ? Json(*reasoning) : Json(nullptr)},
              {"model", model},
              {"usage", usage},
              {"title", new_title ? Json(*new_title) : Json(nullptr)},
              {"cancelled", cancelled}};
  if (!context_trace.is_null()) out["context_trace"] = context_trace;
  return out;
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

void ChatEngine::set_knowledge_context_builder(KnowledgeContextBuilder builder) {
  std::lock_guard lk(mu_);
  knowledge_context_builder_ = std::move(builder);
}

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
                                        std::string_view exclude_message_id, Json* context_trace) {
  const bool capture_native_history = context_trace && context_trace->is_object() &&
      context_trace->value("_capture_native_history", Json()) == true;
  std::unique_lock<std::recursive_mutex> source_lock;
  if (opts.active_task_spec) source_lock = db_.lock();
  auto active_task = prepare_active_task(db_, conv_id, opts, active_task_in_flight_);
  if (!active_task) return active_task.error();
  Json messages = Json::array();
  Json compiled_context;
  Json resolved_context_request;
  Json history_ids = Json::array();
  Json replaced_ids = Json::array();
  Json captured_native_history = Json::array();
  std::set<std::string> refinement_ids;
  if (opts.active_task_spec && opts.active_task_history == "replace_refinement") {
    for (const auto& mid : (*active_task)["history_coverage_message_ids"]) {
      refinement_ids.insert(mid.get<std::string>());
    }
  }

  std::string sys_prompt = opts.system_prompt ? *opts.system_prompt : cfg_string(cfg_, "system_prompt");
  if (!sys_prompt.empty()) messages.push_back(Json{{"role", "system"}, {"content", sys_prompt}});

  if (memory_ && opts.include_memory) {
    std::string mem_ctx = memory_->get_active_context();
    if (!mem_ctx.empty()) {
      messages.push_back(Json{{"role", "system"}, {"content", "User's memory/context:\n" + mem_ctx}});
    }
  }

  bool graph_disabled = opts.context_depth.has_value() && *opts.context_depth == 0;
  if (graph_memory_ && opts.include_graph_memory && !graph_disabled) {
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

  if (opts.knowledge_context) {
    auto request = *opts.knowledge_context;
    if (request.budget_tokens <= 0) return Error(Errc::InvalidArgument, "knowledge_context budget_tokens must be positive");
    if (request.text.empty()) request.text = current_text;
    KnowledgeContextBuilder builder;
    {
      std::lock_guard lk(mu_);
      builder = knowledge_context_builder_;
    }
    if (!builder) return Error(Errc::NotImplemented, "knowledge context is unavailable on this ChatEngine");
    auto compiled = builder(request);
    if (!compiled) return compiled.error();
    if (!compiled->is_object() || !compiled->contains("prompt") || !(*compiled)["prompt"].is_string() ||
        !compiled->contains("context_set") || !(*compiled)["context_set"].is_object()) {
      return Error(Errc::Internal, "knowledge context builder returned an invalid result");
    }
    compiled_context = *compiled;
    resolved_context_request = compiled->value("request", request.to_json());
    const auto prompt = (*compiled)["prompt"].get<std::string>();
    if (!prompt.empty()) messages.push_back(Json{{"role", "system"}, {"content", prompt}});
  }

  if (!conv_id.empty() && opts.include_history) {
    auto msgs = db_.get_msgs(conv_id);
    if (!msgs) return msgs.error();
    for (const auto& m : *msgs) {
      if (m.status != msg_status::kActive) continue;
      if (capture_native_history) captured_native_history.push_back(native_history_row(m));
      if (!exclude_message_id.empty() && m.id == exclude_message_id) continue;
      if (refinement_ids.count(m.id)) {
        replaced_ids.push_back(m.id);
        continue;
      }
      std::string content = m.text;
      if (m.weight > 1.5) {
        content = "[IMPORTANT] " + content;
      } else if (m.weight < 0.5) {
        content = "[low priority] " + content;
      }
      messages.push_back(Json{{"role", m.role}, {"content", content}});
      history_ids.push_back(m.id);
    }
  }

  if (opts.active_task_spec) {
    messages.push_back(Json{{"role", "user"},
      {"content", "[Active task specification; derived from the selected source messages]\n" +
        (*active_task)["compiled_spec"]["compiled_instruction"]["text"].get<std::string>()}});
  }
  messages.push_back(Json{{"role", "user"}, {"content", build_content(current_text, attachments)}});
  if (context_trace) {
    *context_trace = Json{{"kind", "compiled_messages"}, {"version", 1},
                         {"messages", messages}, {"messages_sha256", Sha256::hex(json::dump(messages))},
                         {"history_message_ids", history_ids},
                         {"selection", {{"include_memory", opts.include_memory},
                                        {"include_graph_memory", opts.include_graph_memory && !graph_disabled},
                                        {"include_history", opts.include_history},
                                        {"context_depth", opts.context_depth ? Json(*opts.context_depth) : Json(nullptr)}}},
                         {"knowledge_context_request", resolved_context_request},
                         {"knowledge_context", compiled_context}};
    if (opts.active_task_spec) {
      (*context_trace)["active_task"] = *active_task;
      (*context_trace)["replaced_history_message_ids"] = replaced_ids;
    }
    if (capture_native_history) (*context_trace)["_native_history_snapshot"] = std::move(captured_native_history);
  }
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
  // 1. Resolve the conversation. Keep one lock order (database -> mu_)
  // shared with task compilation, and serialize implicit first-conversation
  // creation so concurrent sends do not unexpectedly create separate chats.
  Conversation conv;
  {
    auto resolution_lock = db_.lock();
    if (opts.active_task_spec && db_.conn().in_transaction()) {
      return Error(Errc::InvalidArgument,
        "active_task: acceptance cannot run inside an externally owned transaction");
    }
    std::lock_guard lk(mu_);
    if (opts.conv_id) {
      auto r = db_.get_conv(*opts.conv_id);
      if (!r) return r.error();
      if (!*r) return Error(Errc::NotFound, "conversation not found: " + *opts.conv_id);
      conv = **r;
    } else if (conv_) {
      conv = *conv_;
    } else {
      if (opts.active_task_spec) {
        return Error(Errc::InvalidArgument, "active_task requires an existing conversation");
      }
      auto r = db_.create_conv("New Chat");
      if (!r) return r.error();
      conv = *r;
    }
    conv_ = conv;
  }

  // Compile a task against a stable native source selection before accepting
  // the current turn. The same snapshot becomes both the request and its
  // retained provenance; callbacks cannot change it between those operations.
  const bool record_context = opts.trace_context.value_or(opts.active_task_spec.has_value() ||
      opts.knowledge_context.has_value() || !opts.include_memory ||
      !opts.include_graph_memory || !opts.include_history);
  Json context_trace;
  Json task_messages;
  Json accepted_active_task;
  struct InFlightTaskGuard {
    Database& db;
    std::map<std::string, Json>& tasks;
    std::string message_id;
    ~InFlightTaskGuard() {
      if (!message_id.empty()) {
        auto lock = db.lock();
        tasks.erase(message_id);
      }
    }
  } in_flight_guard{db_, active_task_in_flight_, {}};
  std::unique_lock<std::recursive_mutex> source_lock;
  std::unique_ptr<sql::Txn> acceptance_tx;
  chat::ActiveTaskHistory acceptance_history;
  if (opts.active_task_spec) {
    source_lock = db_.lock();
    if (db_.conn().in_transaction()) {
      return Error(Errc::InvalidArgument,
        "active_task: acceptance cannot run inside an externally owned transaction");
    }
    auto before = native_request_inputs(db_, conv.id);
    if (!before) return before.error();
    const auto prepared_system = opts.system_prompt.value_or(cfg_string(cfg_, "system_prompt"));
    const auto prepared_memory = memory_ && opts.include_memory ? memory_->get_active_context() : std::string{};
    // Arbitrary context builders run outside SQL transactions. Their returned
    // graph/knowledge/file context is frozen derived content, not a promise
    // that independent stores or external files remain globally current.
    context_trace = Json{{"_capture_native_history", true}};
    auto built = build_messages(conv.id, text, opts.attachments, opts, "", &context_trace);
    if (!built) return built.error();
    auto captured = context_trace.find("_native_history_snapshot");
    if (captured == context_trace.end() || !captured->is_array())
      return Error(Errc::Internal, "active_task: actual native history was not captured");
    Json prepared_native_history = std::move(*captured);
    context_trace.erase("_native_history_snapshot"); // internal validation, not a public trace field
    if (db_.conn().in_transaction()) {
      return Error(Errc::InvalidArgument,
        "active_task: context preparation left an externally owned transaction open");
    }
    acceptance_tx = std::make_unique<sql::Txn>(db_.conn(), true);
    LOOM_TRY(acceptance_tx->begin_status());
    // BEGIN IMMEDIATE serializes the final authority/source read across
    // independent Database objects, including distinct processes.
    auto after = native_request_inputs(db_, conv.id);
    if (!after) return after.error();
    auto rechecked = prepare_active_task(db_, conv.id, opts, active_task_in_flight_);
    if (!rechecked) return rechecked.error();
    auto current_history = native_history_projection(db_, conv.id, opts.include_history);
    if (!current_history) return current_history.error();
    // Equal before/after endpoints alone miss A -> B (used by the builder) -> A.
    // Validate the native rows actually consumed when composing this array.
    if (*before != *after || *current_history != prepared_native_history ||
        *rechecked != context_trace["active_task"] ||
        prepared_system != opts.system_prompt.value_or(cfg_string(cfg_, "system_prompt")) ||
        prepared_memory != (memory_ && opts.include_memory ? memory_->get_active_context() : std::string{})) {
      return Error(Errc::InvalidArgument,
        "active_task: request inputs changed during context preparation; request was not accepted");
    }
    auto history = chat::read_active_task_history(db_, conv.id, active_task_in_flight_);
    if (!history) return history.error();
    acceptance_history = std::move(*history);
    task_messages = std::move(*built);
    accepted_active_task = context_trace["active_task"];
  } else {
    // Direct C++ callers can bypass from_json; do not accept orphan controls.
    auto checked = prepare_active_task(db_, conv.id, opts);
    if (!checked) return checked.error();
  }

  // 2. Persist the user row and acceptance atomically. create_msg nests a
  // savepoint; only the outer commit makes acceptance durable.
  NewMessage nm;
  nm.conv_id = conv.id;
  nm.text = std::string(text);
  nm.role = "user";
  if (opts.active_task_spec) {
    nm.metadata = Json{{"active_task", accepted_active_task}};
    if (record_context) nm.metadata["context_trace"] = context_trace;
    else context_trace = nullptr;
  }
  if (!opts.attachments.empty()) {
    Json atts = Json::array();
    for (const auto& a : opts.attachments) atts.push_back(a);
    nm.attachments = atts;
  }
  auto umid = db_.create_msg(nm);
  if (!umid) return umid.error();
  std::string user_mid = *umid;
  if (opts.active_task_spec) {
    LOOM_TRY(chat::append_active_task_acceptance(db_, conv.id, user_mid, accepted_active_task, acceptance_history));
    LOOM_TRY(acceptance_tx->commit());
    acceptance_tx.reset();
    // Only masks temporary metadata projection divergence during callbacks;
    // the committed EventLog is the durable ancestry authority.
    in_flight_guard.message_id = user_mid;
    active_task_in_flight_.emplace(user_mid, accepted_active_task);
  }
  if (source_lock.owns_lock()) source_lock.unlock();

  auto retain_request_metadata = [&]() -> Status {
    if (!record_context && !opts.active_task_spec) return ok_status();
    // Retain compilation even if the provider later fails. This is deliberately
    // not a RequestSnapshot or a claim that a provider received these messages.
    auto db_lock = db_.lock();
    auto user_message = db_.get_msg(user_mid);
    if (!user_message) return user_message.error();
    if (!*user_message) return Error(Errc::NotFound, "user message removed before accepted request metadata was retained");
    const bool current_message_changed = opts.active_task_spec && ((**user_message).conv_id != conv.id ||
        (**user_message).role != "user" || (**user_message).text != text ||
        (**user_message).attachments != nm.attachments);
    Json metadata = (**user_message).metadata;
    if (!metadata.is_object()) metadata = Json{{"loom_preserved_metadata", metadata}};
    if (opts.active_task_spec && (!metadata.contains("active_task") || metadata["active_task"] != accepted_active_task)) {
      // Callbacks may add arbitrary metadata or replace the entire column.
      // Retain their conflicting value as evidence, while keeping the exact
      // accepted task inspectable independently of trace opt-out.
      Json recovery{{"stage", "after_message_created_callbacks_before_transport"},
                    {"previous_active_task_present", metadata.contains("active_task")}};
      if (metadata.contains("active_task")) recovery["previous_active_task"] = metadata["active_task"];
      if (metadata.contains("loom_active_task_metadata_recovery")) {
        recovery["previous_recovery"] = metadata["loom_active_task_metadata_recovery"];
      }
      metadata["loom_active_task_metadata_recovery"] = std::move(recovery);
      metadata["active_task"] = accepted_active_task;
    }
    if (record_context) metadata["context_trace"] = context_trace;
    MsgPatch patch;
    patch.metadata = std::move(metadata);
    auto saved = db_.update_msg(user_mid, patch);
    if (!saved) return saved.error();
    if (current_message_changed) {
      if ((**user_message).conv_id != conv.id) {
        return Error(Errc::InvalidArgument,
          "active_task: current message moved after acceptance; metadata retained on moved row; request was not sent");
      }
      return Error(Errc::InvalidArgument, "active_task: current message changed after acceptance; request was not sent");
    }
    return ok_status();
  };

  try {
    bus_.emit(events::kMsgCreated,
             Json{{"id", user_mid}, {"text", std::string(text)}, {"conv_id", conv.id}, {"role", "user"}});
    if (cb.on_start) cb.on_start(conv.id, user_mid);
  } catch (...) {
    // Keep the original callback exception. Retention is best effort only if
    // the callback also removed/moved the row or the database itself failed.
    if (opts.active_task_spec) {
      try {
        auto retained = retain_request_metadata();
        if (!retained) log::warn("loom.chat_engine", "Failed to retain accepted task after callback exception: {}",
                                 retained.error().to_string());
      } catch (...) {
        log::warn("loom.chat_engine", "Accepted task metadata retention threw after callback exception");
      }
    }
    throw;
  }

  // 3. Build the message array (history excludes the message just persisted).
  auto msgs_json = opts.active_task_spec ? Result<Json>(std::move(task_messages)) :
      build_messages(conv.id, text, opts.attachments, opts, user_mid,
                     record_context ? &context_trace : nullptr);
  if (!msgs_json) return msgs_json.error();
  LOOM_TRY(retain_request_metadata());

  std::string model = opts.model.value_or(cfg_string(cfg_, "default_model"));

  // 5. Call the API.
  std::string key = secrets_.get_string("api_key");
  if (key.empty()) return Error(Errc::Auth, "No API key configured - open Settings");

  std::string base = rstrip_slash(cfg_string(cfg_, "base_url"));
  bool stream = opts.stream.value_or(cfg_bool(cfg_, "stream", true)) && static_cast<bool>(cb.on_chunk);

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
  result.context_trace = std::move(context_trace);

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
