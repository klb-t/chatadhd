// OWNER: wave 2 net/chat/worker. Stub.
#include "loom/chat_engine.h"

#include "stub.h"

namespace loom {

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

Status ChatEngine::resume_last() { return LOOM_NOT_IMPLEMENTED("ChatEngine::resume_last"); }  // STUB: wave2
Result<Conversation> ChatEngine::new_conv(std::string_view) {
  return LOOM_NOT_IMPLEMENTED("ChatEngine::new_conv");  // STUB: wave2
}
Result<std::optional<Conversation>> ChatEngine::load_conv(std::string_view) {
  return LOOM_NOT_IMPLEMENTED("ChatEngine::load_conv");  // STUB: wave2
}
std::optional<Conversation> ChatEngine::current_conv() const {
  std::lock_guard lk(mu_);
  return conv_;
}
Result<ChatResult> ChatEngine::send(std::string_view, const ChatOptions&, const ChatCallbacks&, const CancelToken*) {
  return LOOM_NOT_IMPLEMENTED("ChatEngine::send");  // STUB: wave2
}
Result<Json> ChatEngine::build_messages(std::string_view, std::string_view, const std::vector<std::string>&,
                                        const ChatOptions&, std::string_view) {
  return LOOM_NOT_IMPLEMENTED("ChatEngine::build_messages");  // STUB: wave2
}
Json ChatEngine::build_content(std::string_view text, const std::vector<std::string>&) const {
  return Json(std::string(text));  // STUB: wave2
}
void ChatEngine::configure_reasoning(Json&, std::string_view, const std::optional<std::string>&) {}  // STUB: wave2
std::optional<std::string> ChatEngine::last_reasoning() const {
  std::lock_guard lk(mu_);
  return last_reasoning_;
}

}  // namespace loom
