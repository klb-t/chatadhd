// Adapters for the use-model action: one per provider API family.  An adapter
// only knows the wire shape of its family (paths, auth headers, body, response
// and stream decoding); which provider, secret and parameters are used is data
// resolved by use_model.cpp.
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include "loom/log.h"
#include "loom/net/sse.h"
#include "loom/util/base64.h"
#include "loom/use_model.h"
#include "loom/util/utf8.h"

namespace loom::llm {

namespace {

constexpr int kDefaultTimeoutMs = 120000;

std::string join_url(const std::string& base, const std::string& path) {
  if (path.find("://") != std::string::npos) return path;
  if (path.empty()) return base;
  return base + (path.front() == '/' ? path : "/" + path);
}

// The header a route's scheme puts its credential in ("" = none, e.g. a query key).
std::string_view auth_header_name(std::string_view scheme) {
  if (scheme == "bearer" || scheme == "token") return "Authorization";
  if (scheme == "x-api-key") return "x-api-key";
  return {};
}

// The caller's complete header recipe, in its order; the route's authentication
// is appended last unless the recipe already carries it.
Status apply_wire_headers(const Route& route, const CallSpec& spec, net::HttpRequest& req) {
  req.headers = *spec.wire_headers;
  const std::string_view carried = auth_header_name(route.auth_scheme);
  if (!carried.empty() && !net::header_value(req.headers, carried).empty()) return ok_status();
  return apply_auth(route, req);
}

Json base_body(const CallSpec& spec) {
  return Json{{"model", spec.model}, {"messages", spec.messages}};
}

const Json* choice_message(const Json& j, const Json** choices_out) {
  const Json* choices = json::find(j, "choices");
  if (choices_out) *choices_out = choices;
  if (!choices || !choices->is_array() || choices->empty()) return nullptr;
  return json::find((*choices)[0], "message");
}

// ── openai_chat: OpenAI-compatible chat completions (OpenRouter included) ──
class OpenAiChatDecoder final : public StreamDecoder {
 public:
  OpenAiChatDecoder(const StreamHooks& hooks, const CancelToken* cancel) : hooks_(hooks), cancel_(cancel) {}
  void feed(std::string_view chunk) override {
    parser_.feed(chunk, [this](const net::SseEvent& e) { handle(e); });
  }
  void finish() override {
    parser_.finish([this](const net::SseEvent& e) { handle(e); });
  }
  const Parsed& parsed() const override { return parsed_; }

 private:
  bool cancelled() const { return cancel_ && cancel_->cancelled(); }
  void issue(std::string_view what) {
    parsed_.issues.emplace_back(what);
    if (hooks_.on_issue) hooks_.on_issue(what);
  }
  void handle(const net::SseEvent& evt) {
    if (cancelled()) return;
    if (evt.is_done()) return;
    auto parsed = json::parse(evt.data);
    if (!parsed) {
      issue("malformed_chunk");
      log::debug("loom.use_model", "Skipped malformed SSE chunk: {}", std::string(utf8::prefix(evt.data, 100)));
      return;
    }
    if (const auto* name = json::find(*parsed, "model"); name && !name->is_null()) {
      if (!name->is_string()) issue("model_invalid");
      else if (!parsed_.reported_model.empty() && parsed_.reported_model != name->get<std::string>()) issue("model_changed");
      else parsed_.reported_model = name->get<std::string>();
    }
    const Json* choices = json::find(*parsed, "choices");
    if (choices && choices->is_array() && !choices->empty()) {
      if (const Json* delta = json::find((*choices)[0], "delta")) {
        std::string text = json::get_string(*delta, "content");
        if (!text.empty()) {
          parsed_.text += text;
          if (hooks_.on_text) hooks_.on_text(text);
          if (cancelled()) return;
        }
        std::string reasoning = json::get_string(*delta, "reasoning");
        if (!reasoning.empty()) {
          parsed_.reasoning += reasoning;
          if (hooks_.on_reasoning) hooks_.on_reasoning(reasoning);
          if (cancelled()) return;
        }
      }
    }
    if (const Json* u = json::find(*parsed, "usage"); u && u->is_object() && !u->empty()) parsed_.usage = *u;
  }

  const StreamHooks& hooks_;
  const CancelToken* cancel_;
  net::SseParser parser_;
  Parsed parsed_;
};

class OpenAiChatAdapter final : public Adapter {
 public:
  std::string_view family() const noexcept override { return "openai_chat"; }

  Result<net::HttpRequest> build(const Route& route, Operation op, const CallSpec& spec) const override {
    if (op != Operation::Complete)
      return Error(Errc::Unsupported, "adapter openai_chat has no batch operations");
    net::HttpRequest req;
    req.method = "POST";
    std::string path = route.endpoint;
    if (path.empty()) path = json::get_string(route.metadata, "chat_completions_path", "/chat/completions");
    req.url = join_url(route.base_url, path);
    if (spec.wire_headers) {
      LOOM_TRY(apply_wire_headers(route, spec, req));
    } else {
      LOOM_TRY(apply_auth(route, req));
      req.headers.emplace_back(route.content_type_header, "application/json");
      for (const auto& h : route.default_headers) req.headers.push_back(h);
      if (spec.referer) req.headers.emplace_back("HTTP-Referer", std::string(kAppReferer));
      if (!spec.title.empty()) req.headers.emplace_back("X-Title", spec.title);
      for (const auto& h : spec.extra_headers) req.headers.push_back(h);
    }

    if (!spec.body_bytes.empty()) {
      req.body = spec.body_bytes;
    } else if (spec.body.is_object()) {
      req.body = json::dump(spec.body);
    } else {
      Json body = base_body(spec);
      for (auto it = spec.parameters.begin(); it != spec.parameters.end(); ++it) body[it.key()] = it.value();
      if (spec.stream && !body.contains("stream")) body["stream"] = true;
      req.body = json::dump(body);
    }
    req.timeout_ms = spec.timeout_ms.value_or(kDefaultTimeoutMs);
    req.stream = spec.stream;
    return req;
  }

  bool success(Operation, int http_status) const noexcept override { return http_status == 200; }

  Capabilities capabilities() const override { return {{"text", "json"}, {"text", "json"}, {"stream"}}; }

  Result<Parsed> parse(std::string_view body) const override {
    auto j = json::parse(body);
    const Json* choices = nullptr;
    const Json* message = j ? choice_message(*j, &choices) : nullptr;
    if (!j) return Error(Errc::Parse, "invalid JSON response from chat completions");
    if (!message) {
      return Error(Errc::Parse, (!choices || !choices->is_array() || choices->empty())
                                    ? "response missing choices" : "response missing message");
    }
    Parsed out;
    if (const auto* name = json::find(*j, "model"); name && !name->is_null()) {
      if (!name->is_string()) out.issues.emplace_back("model_invalid");
      else out.reported_model = name->get<std::string>();
    }
    out.text = json::get_string(*message, "content");
    out.reasoning = json::get_string(*message, "reasoning");
    if (const Json* u = json::find(*j, "usage"); u && u->is_object()) out.usage = *u;
    return out;
  }

  std::unique_ptr<StreamDecoder> stream_decoder(const StreamHooks& hooks, const CancelToken* cancel) const override {
    return std::make_unique<OpenAiChatDecoder>(hooks, cancel);
  }
};

// ── anthropic_messages: Messages API and Message Batches ────────────────
class AnthropicDecoder final : public StreamDecoder {
 public:
  AnthropicDecoder(const StreamHooks& hooks, const CancelToken* cancel) : hooks_(hooks), cancel_(cancel) {}
  void feed(std::string_view chunk) override {
    parser_.feed(chunk, [this](const net::SseEvent& e) { handle(e); });
  }
  void finish() override {
    parser_.finish([this](const net::SseEvent& e) { handle(e); });
  }
  const Parsed& parsed() const override { return parsed_; }

 private:
  bool cancelled() const { return cancel_ && cancel_->cancelled(); }
  void handle(const net::SseEvent& evt) {
    if (cancelled()) return;
    auto j = json::parse(evt.data);
    if (!j) {
      parsed_.issues.emplace_back("malformed_chunk");
      if (hooks_.on_issue) hooks_.on_issue("malformed_chunk");
      return;
    }
    const std::string type = json::get_string(*j, "type", evt.event);
    if (type == "message_start") {
      if (const Json* m = json::find(*j, "message")) {
        parsed_.reported_model = json::get_string(*m, "model");
        if (const Json* u = json::find(*m, "usage"); u && u->is_object()) merge_usage(*u);
      }
    } else if (type == "content_block_delta") {
      if (const Json* d = json::find(*j, "delta")) {
        const std::string kind = json::get_string(*d, "type");
        if (kind == "text_delta") {
          std::string t = json::get_string(*d, "text");
          if (!t.empty()) {
            parsed_.text += t;
            if (hooks_.on_text) hooks_.on_text(t);
          }
        } else if (kind == "thinking_delta") {
          std::string t = json::get_string(*d, "thinking");
          if (!t.empty()) {
            parsed_.reasoning += t;
            if (hooks_.on_reasoning) hooks_.on_reasoning(t);
          }
        }
      }
    } else if (type == "message_delta") {
      if (const Json* u = json::find(*j, "usage"); u && u->is_object()) merge_usage(*u);
    }
  }
  void merge_usage(const Json& u) {
    for (auto it = u.begin(); it != u.end(); ++it) parsed_.usage[it.key()] = it.value();
  }

  const StreamHooks& hooks_;
  const CancelToken* cancel_;
  net::SseParser parser_;
  Parsed parsed_;
};

class AnthropicMessagesAdapter final : public Adapter {
 public:
  std::string_view family() const noexcept override { return "anthropic_messages"; }

  Result<net::HttpRequest> build(const Route& route, Operation op, const CallSpec& spec) const override {
    net::HttpRequest req;
    switch (op) {
      case Operation::Complete: {
        req.method = "POST";
        req.url = join_url(route.base_url, route.endpoint.empty() ? "/v1/messages" : route.endpoint);
        if (spec.wire_headers) {
          LOOM_TRY(apply_wire_headers(route, spec, req));
        } else {
          LOOM_TRY(apply_auth(route, req));
          for (const auto& h : route.default_headers) req.headers.push_back(h);
          req.headers.emplace_back(route.content_type_header, "application/json");
          for (const auto& h : spec.extra_headers) req.headers.push_back(h);
        }
        if (!spec.body_bytes.empty() || spec.body.is_object()) {
          req.body = !spec.body_bytes.empty() ? spec.body_bytes : json::dump(spec.body);
          req.timeout_ms = spec.timeout_ms.value_or(kDefaultTimeoutMs);
          req.stream = spec.stream;
          return req;
        }
        if (!spec.parameters.contains("max_tokens"))
          return Error(Errc::InvalidArgument, "anthropic_messages needs the max_tokens parameter");
        Json messages = Json::array();
        std::string system;
        for (const auto& m : spec.messages) {
          if (json::get_string(m, "role") == "system" && m.contains("content") && m["content"].is_string()) {
            if (!system.empty()) system += "\n\n";
            system += m["content"].get<std::string>();
          } else {
            messages.push_back(m);
          }
        }
        Json body{{"model", spec.model}, {"max_tokens", spec.parameters["max_tokens"]}, {"messages", messages}};
        if (!system.empty()) body["system"] = system;
        for (auto it = spec.parameters.begin(); it != spec.parameters.end(); ++it)
          if (it.key() != "max_tokens") body[it.key()] = it.value();
        if (spec.stream && !body.contains("stream")) body["stream"] = true;
        req.body = json::dump(body);
        req.timeout_ms = spec.timeout_ms.value_or(kDefaultTimeoutMs);
        req.stream = spec.stream;
        return req;
      }
      case Operation::BatchSubmit:
        req.method = "POST";
        req.url = join_url(route.base_url, route.endpoint.empty() ? "/v1/messages/batches" : route.endpoint);
        LOOM_TRY(apply_auth(route, req));
        for (const auto& h : route.default_headers) req.headers.push_back(h);
        req.headers.emplace_back(route.content_type_header, "application/json");
        req.body = json::dump(Json{{"requests", spec.batch_requests}});
        req.timeout_ms = spec.timeout_ms.value_or(kDefaultTimeoutMs);
        return req;
      case Operation::BatchStatus:
        req.method = "GET";
        req.url = join_url(route.base_url, route.endpoint.empty() ? "/v1/messages/batches" : route.endpoint) + "/" + spec.target;
        LOOM_TRY(apply_auth(route, req));
        for (const auto& h : route.default_headers) req.headers.push_back(h);
        req.timeout_ms = spec.timeout_ms.value_or(kDefaultTimeoutMs);
        return req;
      case Operation::BatchResults:
        req.method = "GET";
        // A results URL as the batch status reports it, or a batch id.
        req.url = spec.target.find("://") != std::string::npos
                      ? spec.target
                      : join_url(route.base_url, route.endpoint.empty() ? "/v1/messages/batches" : route.endpoint) +
                            "/" + spec.target + "/results";
        LOOM_TRY(apply_auth(route, req));
        for (const auto& h : route.default_headers) req.headers.push_back(h);
        req.timeout_ms = spec.timeout_ms.value_or(kDefaultTimeoutMs);
        req.stream = spec.stream;
        return req;
    }
    return Error(Errc::Internal, "unknown operation");
  }

  bool success(Operation op, int http_status) const noexcept override {
    if (op == Operation::BatchSubmit) return http_status == 200 || http_status == 201;
    return http_status == 200;
  }

  Capabilities capabilities() const override { return {{"text", "json"}, {"text", "json"}, {"stream", "batch"}}; }

  Result<Parsed> parse(std::string_view body) const override {
    auto j = json::parse(body);
    if (!j) return Error(Errc::Parse, "invalid JSON response from messages");
    const Json* content = json::find(*j, "content");
    if (!content || !content->is_array()) return Error(Errc::Parse, "response missing content");
    Parsed out;
    for (const auto& block : *content) {
      const std::string type = json::get_string(block, "type");
      if (type == "text") out.text += json::get_string(block, "text");
      else if (type == "thinking") out.reasoning += json::get_string(block, "thinking");
    }
    out.reported_model = json::get_string(*j, "model");
    if (const Json* u = json::find(*j, "usage"); u && u->is_object()) out.usage = *u;
    return out;
  }

  std::unique_ptr<StreamDecoder> stream_decoder(const StreamHooks& hooks, const CancelToken* cancel) const override {
    return std::make_unique<AnthropicDecoder>(hooks, cancel);
  }
};

// ── openai_images: OpenAI-compatible image generation ──────────────────
std::string sniff_image_mime(std::string_view b) {
  if (b.substr(0, 4) == "\x89PNG") return "image/png";
  if (b.size() > 2 && static_cast<unsigned char>(b[0]) == 0xFF && static_cast<unsigned char>(b[1]) == 0xD8) return "image/jpeg";
  if (b.size() > 12 && b.substr(0, 4) == "RIFF" && b.substr(8, 4) == "WEBP") return "image/webp";
  if (b.substr(0, 3) == "GIF") return "image/gif";
  return "image/png";
}

class OpenAiImagesAdapter final : public Adapter {
 public:
  std::string_view family() const noexcept override { return "openai_images"; }

  Capabilities capabilities() const override { return {{"text"}, {"image"}, {}}; }

  // Inline base64 is the only way media bytes arrive; "url" replies are references.
  bool needs_output_store(const CallSpec& spec) const override {
    return json::get_string(spec.parameters, "response_format") == "b64_json";
  }

  Result<net::HttpRequest> build(const Route& route, Operation op, const CallSpec& spec) const override {
    if (op != Operation::Complete) return Error(Errc::Unsupported, "adapter openai_images has no batch operations");
    std::string prompt;
    for (auto it = spec.messages.rbegin(); it != spec.messages.rend(); ++it) {
      if (json::get_string(*it, "role") == "user" && it->contains("content") && (*it)["content"].is_string()) {
        prompt = (*it)["content"].get<std::string>();
        break;
      }
    }
    if (prompt.empty()) return Error(Errc::InvalidArgument, "image generation needs a text prompt");
    net::HttpRequest req;
    req.method = "POST";
    std::string path = route.endpoint;
    if (path.empty()) path = json::get_string(route.metadata, "images_path", "/images/generations");
    req.url = join_url(route.base_url, path);
    if (spec.wire_headers) {
      LOOM_TRY(apply_wire_headers(route, spec, req));
    } else {
      LOOM_TRY(apply_auth(route, req));
      req.headers.emplace_back(route.content_type_header, "application/json");
      for (const auto& h : route.default_headers) req.headers.push_back(h);
      if (spec.referer) req.headers.emplace_back("HTTP-Referer", std::string(kAppReferer));
      if (!spec.title.empty()) req.headers.emplace_back("X-Title", spec.title);
      for (const auto& h : spec.extra_headers) req.headers.push_back(h);
    }
    Json body{{"model", spec.model}, {"prompt", prompt}};
    for (auto it = spec.parameters.begin(); it != spec.parameters.end(); ++it) body[it.key()] = it.value();
    req.body = spec.body_bytes.empty() ? json::dump(body) : spec.body_bytes;
    req.timeout_ms = spec.timeout_ms.value_or(kDefaultTimeoutMs);
    return req;
  }

  bool success(Operation, int http_status) const noexcept override { return http_status == 200; }

  Result<Parsed> parse(std::string_view body) const override {
    auto j = json::parse(body);
    if (!j) return Error(Errc::Parse, "invalid JSON response from image generation");
    const Json* data = json::find(*j, "data");
    if (!data || !data->is_array()) return Error(Errc::Parse, "response missing data");
    Parsed out;
    for (const auto& item : *data) {
      Part part;
      part.kind = "image";
      if (const std::string b64 = json::get_string(item, "b64_json"); !b64.empty()) {
        LOOM_TRY_ASSIGN(part.bytes, base64::decode(b64));
        part.mime = sniff_image_mime(part.bytes);
      } else if (const std::string url = json::get_string(item, "url"); !url.empty()) {
        part.url = url;
      } else {
        return Error(Errc::Parse, "image entry has neither b64_json nor url");
      }
      out.outputs.push_back(std::move(part));
    }
    if (const Json* u = json::find(*j, "usage"); u && u->is_object()) out.usage = *u;
    return out;
  }

  std::unique_ptr<StreamDecoder> stream_decoder(const StreamHooks&, const CancelToken*) const override { return nullptr; }
};

// ── openai_embeddings: OpenAI-compatible embeddings ────────────────────
// An embedding is one more kind of output: each input text yields one
// "embedding" part (the vector, in input order).
class OpenAiEmbeddingsAdapter final : public Adapter {
 public:
  std::string_view family() const noexcept override { return "openai_embeddings"; }

  Capabilities capabilities() const override { return {{"text"}, {"embedding"}, {}}; }

  Result<net::HttpRequest> build(const Route& route, Operation op, const CallSpec& spec) const override {
    if (op != Operation::Complete) return Error(Errc::Unsupported, "adapter openai_embeddings has no batch operations");
    net::HttpRequest req;
    req.method = "POST";
    std::string path = route.endpoint;
    if (path.empty()) {
      path = json::get_string(route.metadata, "embedding_path", "/embeddings");
      if (path.empty() || path.front() != '/') path.insert(path.begin(), '/');
    }
    req.url = join_url(route.base_url, path);
    if (spec.wire_headers) {
      req.headers = *spec.wire_headers;
    } else {
      for (const auto& h : route.default_headers) req.headers.push_back(h);
      req.headers.emplace_back(route.content_type_header, "application/json");
      if (spec.referer) req.headers.emplace_back("HTTP-Referer", std::string(kAppReferer));
      if (!spec.title.empty()) req.headers.emplace_back("X-Title", spec.title);
      for (const auto& h : spec.extra_headers) req.headers.push_back(h);
    }
    // Authentication follows the other headers on this wire.
    const std::string_view carried = auth_header_name(route.auth_scheme);
    if (carried.empty() || net::header_value(req.headers, carried).empty()) LOOM_TRY(apply_auth(route, req));
    if (!spec.body_bytes.empty()) {
      req.body = spec.body_bytes;
    } else if (spec.body.is_object()) {
      req.body = json::dump(spec.body);
    } else {
      Json input = Json::array();
      for (const auto& m : spec.messages)
        if (json::get_string(m, "role") == "user" && m.contains("content") && m["content"].is_string())
          input.push_back(m["content"]);
      if (input.empty()) return Error(Errc::InvalidArgument, "embedding needs at least one text input");
      Json body{{"model", spec.model}, {"input", input}};
      for (auto it = spec.parameters.begin(); it != spec.parameters.end(); ++it) body[it.key()] = it.value();
      req.body = json::dump(body);
    }
    req.timeout_ms = spec.timeout_ms.value_or(kDefaultTimeoutMs);
    return req;
  }

  bool success(Operation, int http_status) const noexcept override { return http_status == 200; }

  Result<Parsed> parse(std::string_view body) const override {
    auto j = json::parse(body);
    if (!j || !j->is_object()) return Error(Errc::Parse, "embedding response is not a JSON object");
    const Json* data = json::find(*j, "data");
    if (!data || !data->is_array()) return Error(Errc::Parse, "embedding response count mismatch");
    Parsed out;
    if (const auto* reported = json::find(*j, "model"); reported && !reported->is_null()) {
      if (!reported->is_string() || reported->get_ref<const std::string&>().empty())
        return Error(Errc::Parse, "embedding reported model invalid");
      out.reported_model = reported->get<std::string>();
    }
    // Rows carry their index; the parts come out in input order.
    std::vector<Json> rows(data->size());
    std::vector<bool> seen(data->size(), false);
    std::optional<std::size_t> dimensions;
    for (const auto& row : *data) {
      const auto* index = json::find(row, "index");
      const auto* vector = json::find(row, "embedding");
      if (!index || !index->is_number_integer() ||
          (!index->is_number_unsigned() && index->get<std::int64_t>() < 0) ||
          index->get<std::uint64_t>() >= data->size() || !vector || !vector->is_array() || vector->empty())
        return Error(Errc::Parse, "embedding response index/vector invalid");
      const auto position = index->get<std::size_t>();
      if (seen[position]) return Error(Errc::Parse, "embedding response duplicate index");
      seen[position] = true;
      if (dimensions && *dimensions != vector->size()) return Error(Errc::Parse, "embedding vector dimension mismatch");
      dimensions = vector->size();
      for (const auto& value : *vector)
        if (!value.is_number()) return Error(Errc::Parse, "embedding vector contains nonnumber");
      rows[position] = *vector;
    }
    for (auto& row : rows) {
      Part part;
      part.kind = "embedding";
      part.value = std::move(row);
      out.outputs.push_back(std::move(part));
    }
    if (const Json* u = json::find(*j, "usage"); u && u->is_object()) out.usage = *u;
    return out;
  }

  std::unique_ptr<StreamDecoder> stream_decoder(const StreamHooks&, const CancelToken*) const override { return nullptr; }
};

struct AdapterTable {
  std::mutex mu;
  std::map<std::string, AdapterFactory, std::less<>> factories;
  AdapterTable() {
    factories["openai_chat"] = [] { return std::make_unique<OpenAiChatAdapter>(); };
    factories["anthropic_messages"] = [] { return std::make_unique<AnthropicMessagesAdapter>(); };
    factories["openai_images"] = [] { return std::make_unique<OpenAiImagesAdapter>(); };
    factories["openai_embeddings"] = [] { return std::make_unique<OpenAiEmbeddingsAdapter>(); };
  }
};

AdapterTable& table() {
  static AdapterTable t;
  return t;
}

}  // namespace

void register_adapter(std::string family, AdapterFactory factory) {
  auto& t = table();
  std::lock_guard lk(t.mu);
  t.factories[std::move(family)] = std::move(factory);
}

std::unique_ptr<Adapter> make_adapter(std::string_view family) {
  auto& t = table();
  AdapterFactory factory;
  {
    std::lock_guard lk(t.mu);
    auto it = t.factories.find(family);
    if (it == t.factories.end()) return nullptr;
    factory = it->second;
  }
  return factory();
}

std::vector<std::string> adapter_families() {
  auto& t = table();
  std::lock_guard lk(t.mu);
  std::vector<std::string> out;
  for (const auto& [name, _] : t.factories) out.push_back(name);
  return out;
}

}  // namespace loom::llm
