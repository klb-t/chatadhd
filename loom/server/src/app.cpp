#include "app.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

#include "sse_stream.h"

namespace loom_server {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

std::string own_and_free(const char* s) {
  std::string result = s ? s : "";
  if (s) loom_free_string(s);
  return result;
}

json parse_or_empty(const std::string& text) {
  auto v = json::parse(text, nullptr, false);
  if (v.is_discarded()) return json::object();
  return v;
}

json parse_body(const httplib::Request& req) {
  if (req.body.empty()) return json::object();
  return parse_or_empty(req.body);
}

// Maps a loom.h error "code" string (see loom.h top-of-file comment) to an
// HTTP status.
int status_for_error_code(const std::string& code) {
  static const std::unordered_map<std::string, int> table = {
      {"invalid_argument", 400}, {"not_found", 404},   {"already_exists", 409},
      {"io", 500},              {"database", 500},     {"parse", 400},
      {"network", 502},         {"http", 502},         {"auth", 401},
      {"cancelled", 409},       {"timeout", 504},      {"unavailable", 503},
      {"not_implemented", 501}, {"crypto", 500},       {"conflict", 409},
      {"busy", 409},            {"unsupported", 400},  {"rate_limited", 429},
      {"paused", 409},          {"internal", 500},
  };
  auto it = table.find(code);
  return it == table.end() ? 500 : it->second;
}

// Maps a negative LOOM_E_* return code to the same code-name table used for
// JSON error objects, so int-returning ABI calls produce a matching error
// body.
std::string errc_name_for_rc(int rc) {
  static const char* names[] = {
      "invalid_argument", "not_found",  "already_exists", "io",
      "database",         "parse",      "network",        "http",
      "auth",             "cancelled",  "timeout",        "unavailable",
      "not_implemented",  "crypto",     "conflict",        "busy",
      "unsupported",      "rate_limited", "paused",        "internal",
  };
  int idx = -rc - 1;
  if (idx < 0 || idx >= static_cast<int>(sizeof(names) / sizeof(names[0])))
    return "internal";
  return names[idx];
}

void send_error(httplib::Response& res, const std::string& code, const std::string& message,
                 int status = -1) {
  json err = {{"error", {{"code", code}, {"message", message}}}};
  res.status = status >= 0 ? status : status_for_error_code(code);
  res.set_content(err.dump(), "application/json");
}

// Sends the raw JSON string returned by a loom_* call, frees it, and picks
// the HTTP status from the embedded {"error":{"code":...}} object if present.
void send_loom(httplib::Response& res, const char* raw, int ok_status = 200) {
  std::string body = own_and_free(raw);
  auto parsed = json::parse(body, nullptr, false);
  int status = ok_status;
  if (!parsed.is_discarded() && parsed.is_object() && parsed.size() == 1 && parsed.contains("error")) {
    status = status_for_error_code(parsed["error"].value("code", std::string("internal")));
  }
  res.status = status;
  res.set_content(body, "application/json");
}

// Sends a result for an int-returning ABI call: `ok_body` on LOOM_OK,
// otherwise a matching error object.
void send_rc(httplib::Response& res, int rc, const json& ok_body) {
  if (rc == LOOM_OK) {
    res.status = 200;
    res.set_content(ok_body.dump(), "application/json");
    return;
  }
  send_error(res, errc_name_for_rc(rc), "operation failed (code " + std::to_string(rc) + ")");
}

std::string next_request_id() {
  static std::atomic<uint64_t> counter{0};
  auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return "req_" + std::to_string(now) + "_" + std::to_string(counter.fetch_add(1));
}

std::string mime_for_export_format(const std::string& fmt) {
  if (fmt == "markdown") return "text/markdown; charset=utf-8";
  if (fmt == "text") return "text/plain; charset=utf-8";
  if (fmt == "html") return "text/html; charset=utf-8";
  return "application/json";
}

std::string ext_for_export_format(const std::string& fmt) {
  if (fmt == "markdown") return ".md";
  if (fmt == "text") return ".txt";
  if (fmt == "html") return ".html";
  return ".json";
}

}  // namespace

App::App(ServerOptions opts) : opts_(std::move(opts)) {}

App::~App() {
  if (ctx_) loom_shutdown(ctx_);
}

bool App::init() {
  const char* data_dir = opts_.data_dir.empty() ? nullptr : opts_.data_dir.c_str();
  ctx_ = loom_init(data_dir);
  if (!ctx_) return false;

  svr_.set_payload_max_length(opts_.max_body_bytes);
  svr_.set_keep_alive_max_count(100);

  register_middleware();
  register_routes();

  if (!opts_.static_dir.empty()) {
    svr_.set_mount_point("/", opts_.static_dir);
  }

  // SPA fallback: anything under /api/ that reached here is a genuine 404;
  // anything else falls back to index.html so client-side routing works.
  svr_.Get(".*", [this](const httplib::Request& req, httplib::Response& res) {
    if (req.path.rfind("/api/", 0) == 0) {
      send_error(res, "not_found", "no such route: " + req.path, 404);
      return;
    }
    if (opts_.static_dir.empty()) {
      res.status = 404;
      return;
    }
    std::ifstream f(opts_.static_dir + "/index.html", std::ios::binary);
    if (!f) {
      res.status = 404;
      return;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    res.set_content(ss.str(), "text/html; charset=utf-8");
  });

  return true;
}

void App::run() {
  running_ = true;
  svr_.listen(opts_.host, opts_.port);
  running_ = false;
}

void App::stop() {
  if (running_.exchange(false)) svr_.stop();
}

void App::register_middleware() {
  // Bearer token auth for every /api/ route, when configured. Off by
  // default (matches "bind 127.0.0.1, no auth" for local/dev use).
  svr_.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
    if (!opts_.bearer_token.empty() && req.path.rfind("/api/", 0) == 0) {
      std::string want = "Bearer " + opts_.bearer_token;
      if (req.get_header_value("Authorization") != want) {
        send_error(res, "auth", "missing or invalid bearer token", 401);
        return httplib::Server::HandlerResponse::Handled;
      }
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  // CORS is off by default: no Access-Control-* headers are ever added, so
  // browsers enforce same-origin. The web app is always served by this
  // same process, so this never needs to be relaxed for normal use.
}

void App::register_routes() {
  route_conversations();
  route_messages();
  route_search();
  route_chat();
  route_models_providers();
  route_config_secrets();
  route_graph_context();
  route_semantic();
  route_memory();
  route_import_export();
  route_provenance_events_tasks();
  route_logs_misc();
  route_archive_placeholder();
}

// ── Conversations ──────────────────────────────────────────────────────

void App::route_conversations() {
  svr_.Get("/api/conversations", [this](const httplib::Request& req, httplib::Response& res) {
    int limit = req.has_param("limit") ? std::atoi(req.get_param_value("limit").c_str()) : 50;
    send_loom(res, loom_list_conversations(ctx_, limit));
  });

  svr_.Post("/api/conversations", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    std::string title = body.value("title", std::string());
    send_loom(res, loom_create_conversation(ctx_, title.empty() ? nullptr : title.c_str()), 201);
  });

  svr_.Get(R"(/api/conversations/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    send_loom(res, loom_get_conversation(ctx_, req.matches[1].str().c_str()));
  });

  svr_.Patch(R"(/api/conversations/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    send_loom(res, loom_update_conversation(ctx_, req.matches[1].str().c_str(), body.dump().c_str()));
  });

  svr_.Delete(R"(/api/conversations/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    int rc = loom_delete_conversation(ctx_, req.matches[1].str().c_str());
    send_rc(res, rc, {{"deleted", true}});
  });
}

// ── Messages ───────────────────────────────────────────────────────────

void App::route_messages() {
  svr_.Get(R"(/api/conversations/([^/]+)/messages)", [this](const httplib::Request& req, httplib::Response& res) {
    std::string conv_id = req.matches[1].str();
    bool all = req.has_param("all") && req.get_param_value("all") != "0" && req.get_param_value("all") != "false";
    send_loom(res, all ? loom_get_messages_ex(ctx_, conv_id.c_str(), 1) : loom_get_messages(ctx_, conv_id.c_str()));
  });

  svr_.Get(R"(/api/messages/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    send_loom(res, loom_get_message(ctx_, req.matches[1].str().c_str()));
  });

  svr_.Post(R"(/api/messages/([^/]+)/edit)", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    std::string text = body.value("text", std::string());
    send_loom(res, loom_edit_message(ctx_, req.matches[1].str().c_str(), text.c_str()));
  });

  svr_.Post(R"(/api/messages/([^/]+)/restore)", [this](const httplib::Request& req, httplib::Response& res) {
    int rc = loom_restore_version(ctx_, req.matches[1].str().c_str());
    send_rc(res, rc, {{"restored", true}});
  });

  svr_.Get(R"(/api/messages/([^/]+)/versions)", [this](const httplib::Request& req, httplib::Response& res) {
    send_loom(res, loom_get_versions(ctx_, req.matches[1].str().c_str()));
  });

  svr_.Post(R"(/api/messages/([^/]+)/status)", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    std::string status = body.value("status", std::string());
    int rc = loom_set_message_status(ctx_, req.matches[1].str().c_str(), status.c_str());
    send_rc(res, rc, {{"status", status}});
  });

  svr_.Patch(R"(/api/messages/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    std::string id = req.matches[1].str();
    int rc = loom_update_message(ctx_, id.c_str(), body.dump().c_str());
    if (rc != LOOM_OK) {
      send_rc(res, rc, json::object());
      return;
    }
    send_loom(res, loom_get_message(ctx_, id.c_str()));
  });
}

// ── Search ─────────────────────────────────────────────────────────────

void App::route_search() {
  svr_.Get("/api/search", [this](const httplib::Request& req, httplib::Response& res) {
    std::string q = req.get_param_value("q");
    json opts = json::object();
    if (req.has_param("limit")) opts["limit"] = std::atoi(req.get_param_value("limit").c_str());
    if (req.has_param("conv_id")) opts["conv_id"] = req.get_param_value("conv_id");
    if (req.has_param("include_inactive"))
      opts["include_inactive"] = req.get_param_value("include_inactive") == "1" ||
                                  req.get_param_value("include_inactive") == "true";
    if (req.has_param("mode")) opts["mode"] = req.get_param_value("mode");
    send_loom(res, loom_search(ctx_, q.c_str(), opts.dump().c_str()));
  });
}

// ── Chat (SSE) ─────────────────────────────────────────────────────────

void App::route_chat() {
  svr_.Post("/api/chat", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    if (!body.contains("message") || !body["message"].is_string() || body["message"].get<std::string>().empty()) {
      send_error(res, "invalid_argument", "\"message\" is required", 400);
      return;
    }
    if (!body.contains("request_id") || !body["request_id"].is_string() || body["request_id"].get<std::string>().empty()) {
      body["request_id"] = next_request_id();
    }
    std::string request_json = body.dump();

    auto queue = std::make_shared<SseQueue>();
    LoomContext* ctx = ctx_;
    auto thread_ptr = std::make_shared<std::thread>([ctx, request_json, queue]() {
      const char* result = loom_chat_ex(
          ctx, request_json.c_str(),
          [](const char* chunk, int done, void* ud) {
            auto* q = static_cast<SseQueue*>(ud);
            q->push_data(chunk ? chunk : "{}");
            if (done) q->close();
          },
          queue.get());
      loom_free_string(result);
      queue->close();  // defensive: guarantees the provider always terminates
    });

    res.set_chunked_content_provider(
        "text/event-stream",
        [queue](size_t offset, httplib::DataSink& sink) { return queue->provide(offset, sink); },
        [thread_ptr, queue](bool success) {
          if (!success) queue->cancel();
          if (thread_ptr->joinable()) thread_ptr->join();
        });
    res.set_header("Cache-Control", "no-cache");
    res.set_header("X-Accel-Buffering", "no");
  });

  svr_.Post("/api/chat/cancel", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    std::string request_id = body.value("request_id", req.get_param_value("request_id"));
    if (request_id.empty()) {
      send_error(res, "invalid_argument", "\"request_id\" is required", 400);
      return;
    }
    int rc = loom_chat_cancel(ctx_, request_id.c_str());
    send_rc(res, rc, {{"cancelled", true}});
  });
}

// ── Models & providers ─────────────────────────────────────────────────

void App::route_models_providers() {
  svr_.Get("/api/models", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_get_models(ctx_));
  });
  svr_.Post("/api/models/refresh", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_refresh_models(ctx_));
  });
  svr_.Get("/api/providers", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_get_providers(ctx_));
  });
}

// ── Config & secrets ───────────────────────────────────────────────────

void App::route_config_secrets() {
  svr_.Get("/api/config", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_get_config(ctx_));
  });

  // Merge-patch: {"key": value, ...} -> loom_set_config_json.
  svr_.Patch("/api/config", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    int rc = loom_set_config_json(ctx_, body.dump().c_str());
    if (rc != LOOM_OK) {
      send_rc(res, rc, json::object());
      return;
    }
    send_loom(res, loom_get_config(ctx_));
  });

  // Single-key set: PUT /api/config/:key {"value": <json or raw text>}.
  svr_.Put(R"(/api/config/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    std::string key = req.matches[1].str();
    std::string value;
    if (body.contains("value")) {
      value = body["value"].is_string() ? body["value"].get<std::string>() : body["value"].dump();
    }
    loom_set_config(ctx_, key.c_str(), value.c_str());
    send_loom(res, loom_get_config(ctx_));
  });

  // Secrets are write-only: values never round-trip back to the client.
  svr_.Get("/api/secrets", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_list_secret_keys(ctx_));
  });
  svr_.Post(R"(/api/secrets/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    std::string value = body.value("value", std::string());
    int rc = loom_set_secret(ctx_, req.matches[1].str().c_str(), value.c_str());
    send_rc(res, rc, {{"set", true}});
  });
  svr_.Get(R"(/api/secrets/([^/]+)/has)", [this](const httplib::Request& req, httplib::Response& res) {
    int rc = loom_has_secret(ctx_, req.matches[1].str().c_str());
    if (rc < 0) {
      send_rc(res, rc, json::object());
      return;
    }
    res.set_content(json{{"has", rc == 1}}.dump(), "application/json");
  });
  svr_.Delete(R"(/api/secrets/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    int rc = loom_delete_secret(ctx_, req.matches[1].str().c_str());
    send_rc(res, rc, {{"deleted", true}});
  });
}

// ── Graph & context ────────────────────────────────────────────────────

void App::route_graph_context() {
  svr_.Get("/api/graph/nodes", [this](const httplib::Request& req, httplib::Response& res) {
    json filter = json::object();
    if (req.has_param("kind")) filter["kind"] = req.get_param_value("kind");
    if (req.has_param("label")) filter["label"] = req.get_param_value("label");
    if (req.has_param("limit")) filter["limit"] = std::atoi(req.get_param_value("limit").c_str());
    send_loom(res, loom_get_nodes(ctx_, filter.dump().c_str()));
  });

  svr_.Get("/api/graph/edges", [this](const httplib::Request& req, httplib::Response& res) {
    json filter = json::object();
    if (req.has_param("node_id")) filter["node_id"] = req.get_param_value("node_id");
    if (req.has_param("link_type")) filter["link_type"] = req.get_param_value("link_type");
    if (req.has_param("limit")) filter["limit"] = std::atoi(req.get_param_value("limit").c_str());
    send_loom(res, loom_get_edges(ctx_, filter.dump().c_str()));
  });

  svr_.Post("/api/graph/expand", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    json seeds = body.value("seed_ids", json::array());
    int depth = body.value("depth", 1);
    send_loom(res, loom_expand_graph(ctx_, seeds.dump().c_str(), depth));
  });

  svr_.Get("/api/graph/data", [this](const httplib::Request& req, httplib::Response& res) {
    std::string conv_id = req.get_param_value("conv_id");
    send_loom(res, loom_get_graph_data(ctx_, conv_id.empty() ? nullptr : conv_id.c_str()));
  });

  svr_.Post("/api/graph/reindex", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    std::string conv_id = body.value("conv_id", std::string());
    send_loom(res, loom_graph_reindex(ctx_, conv_id.empty() ? nullptr : conv_id.c_str()));
  });

  svr_.Post("/api/context/select", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    send_loom(res, loom_select_context_ex(ctx_, body.dump().c_str()));
  });
}

// ── Semantic worker ────────────────────────────────────────────────────

void App::route_semantic() {
  svr_.Get("/api/semantic/status", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_semantic_status(ctx_));
  });
  svr_.Post("/api/semantic/pause", [this](const httplib::Request&, httplib::Response& res) {
    loom_semantic_pause(ctx_);
    send_loom(res, loom_semantic_status(ctx_));
  });
  svr_.Post("/api/semantic/resume", [this](const httplib::Request&, httplib::Response& res) {
    loom_semantic_resume(ctx_);
    send_loom(res, loom_semantic_status(ctx_));
  });
  svr_.Post("/api/semantic/wake", [this](const httplib::Request&, httplib::Response& res) {
    loom_semantic_wake(ctx_);
    send_loom(res, loom_semantic_status(ctx_));
  });
}

// ── Memory tree ────────────────────────────────────────────────────────

void App::route_memory() {
  svr_.Get("/api/memory", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_list_memory(ctx_));
  });
  svr_.Post("/api/memory", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    send_loom(res, loom_create_memory(ctx_, body.dump().c_str()), 201);
  });
  svr_.Patch(R"(/api/memory/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    send_loom(res, loom_update_memory(ctx_, req.matches[1].str().c_str(), body.dump().c_str()));
  });
  svr_.Delete(R"(/api/memory/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    int rc = loom_delete_memory(ctx_, req.matches[1].str().c_str());
    send_rc(res, rc, {{"deleted", true}});
  });
  svr_.Get("/api/memory/context", [this](const httplib::Request& req, httplib::Response& res) {
    int max_chars = req.has_param("max_chars") ? std::atoi(req.get_param_value("max_chars").c_str()) : 0;
    send_loom(res, loom_get_memory_context(ctx_, max_chars));
  });
}

// ── Import / export ────────────────────────────────────────────────────

void App::route_import_export() {
  svr_.Post("/api/import", [this](const httplib::Request& req, httplib::Response& res) {
    if (!req.has_file("file")) {
      send_error(res, "invalid_argument", "multipart field \"file\" is required", 400);
      return;
    }
    const auto file = req.get_file_value("file");
    std::string title = req.has_param("title") ? req.get_param_value("title") : std::string();

    std::error_code ec;
    fs::path tmp_dir = fs::temp_directory_path(ec) / "loom-server-import";
    if (ec) tmp_dir = fs::path("/tmp/loom-server-import");
    fs::create_directories(tmp_dir, ec);

    std::string ext = fs::path(file.filename).extension().string();
    fs::path tmp_path = tmp_dir / (next_request_id() + ext);
    {
      std::ofstream out(tmp_path, std::ios::binary);
      out.write(file.content.data(), static_cast<std::streamsize>(file.content.size()));
    }

    auto queue = std::make_shared<SseQueue>();
    LoomContext* ctx = ctx_;
    std::string path_str = tmp_path.string();
    std::string title_copy = title;
    auto thread_ptr = std::make_shared<std::thread>([ctx, path_str, title_copy, queue]() {
      const char* result = loom_import_file(
          ctx, path_str.c_str(), title_copy.empty() ? nullptr : title_copy.c_str(),
          [](int current, int total, const char* status, void* ud) {
            auto* q = static_cast<SseQueue*>(ud);
            json p = {{"type", "progress"}, {"current", current}, {"total", total}, {"status", status ? status : ""}};
            q->push_data(p.dump());
          },
          queue.get());
      std::string body = own_and_free(result);
      json parsed = parse_or_empty(body);
      json out_chunk;
      if (parsed.is_object() && parsed.contains("error")) {
        out_chunk = {{"type", "error"}};
        for (auto& [k, v] : parsed["error"].items()) out_chunk[k] = v;
      } else {
        out_chunk = parsed;
        out_chunk["type"] = "done";
      }
      queue->push_data(out_chunk.dump());
      queue->close();
      std::error_code rm_ec;
      fs::remove(path_str, rm_ec);
    });

    res.set_chunked_content_provider(
        "text/event-stream",
        [queue](size_t offset, httplib::DataSink& sink) { return queue->provide(offset, sink); },
        [thread_ptr, queue](bool success) {
          if (!success) queue->cancel();
          if (thread_ptr->joinable()) thread_ptr->join();
        });
    res.set_header("Cache-Control", "no-cache");
  });

  svr_.Get(R"(/api/conversations/([^/]+)/export)", [this](const httplib::Request& req, httplib::Response& res) {
    std::string conv_id = req.matches[1].str();
    std::string fmt = req.has_param("format") ? req.get_param_value("format") : "json";
    std::string raw = own_and_free(loom_export_conversation(ctx_, conv_id.c_str(), fmt.c_str()));
    json parsed = parse_or_empty(raw);
    if (parsed.is_object() && parsed.contains("error")) {
      res.status = status_for_error_code(parsed["error"].value("code", std::string("internal")));
      res.set_content(raw, "application/json");
      return;
    }
    std::string content = parsed.value("content", std::string());
    std::string out_fmt = parsed.value("format", fmt);
    res.set_header("Content-Disposition", "attachment; filename=\"" + conv_id + ext_for_export_format(out_fmt) + "\"");
    res.set_content(content, mime_for_export_format(out_fmt));
  });
}

// ── Provenance, events & tasks ─────────────────────────────────────────

void App::route_provenance_events_tasks() {
  svr_.Get("/api/sources", [this](const httplib::Request& req, httplib::Response& res) {
    int limit = req.has_param("limit") ? std::atoi(req.get_param_value("limit").c_str()) : 100;
    send_loom(res, loom_list_sources(ctx_, limit));
  });

  svr_.Get(R"(/api/provenance/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    send_loom(res, loom_get_provenance(ctx_, req.matches[1].str().c_str()));
  });

  // One-shot poll.
  svr_.Get("/api/events", [this](const httplib::Request& req, httplib::Response& res) {
    json q = json::object();
    if (req.has_param("after_seq")) q["after_seq"] = std::atoll(req.get_param_value("after_seq").c_str());
    if (req.has_param("type")) q["type"] = req.get_param_value("type");
    if (req.has_param("subject_id")) q["subject_id"] = req.get_param_value("subject_id");
    if (req.has_param("limit")) q["limit"] = std::atoi(req.get_param_value("limit").c_str());
    send_loom(res, loom_query_events(ctx_, q.dump().c_str()));
  });

  // Live SSE feed via loom_subscribe.
  svr_.Get("/api/events/stream", [this](const httplib::Request& req, httplib::Response& res) {
    std::string event = req.has_param("event") ? req.get_param_value("event") : "*";
    auto queue = std::make_shared<SseQueue>();
    LoomContext* ctx = ctx_;

    // loom_subscribe's callback runs synchronously on the emitting thread,
    // so it must stay cheap: just hand the payload to the SSE queue.
    struct Sub {
      std::shared_ptr<SseQueue> queue;
    };
    auto* sub = new Sub{queue};
    int64_t token = loom_subscribe(
        ctx, event.c_str(),
        [](const char* event_name, const char* payload_json, void* ud) {
          auto* s = static_cast<Sub*>(ud);
          json chunk = {{"event", event_name ? event_name : ""},
                        {"payload", parse_or_empty(payload_json ? payload_json : "null")}};
          s->queue->push_data(chunk.dump());
        },
        sub);

    if (token < 0) {
      delete sub;
      send_error(res, errc_name_for_rc(static_cast<int>(token)), "subscribe failed");
      return;
    }

    res.set_chunked_content_provider(
        "text/event-stream",
        [queue](size_t offset, httplib::DataSink& sink) { return queue->provide(offset, sink); },
        [ctx, token, sub](bool /*success*/) {
          loom_unsubscribe(ctx, token);
          delete sub;
        });
    res.set_header("Cache-Control", "no-cache");
  });

  svr_.Get("/api/tasks", [this](const httplib::Request& req, httplib::Response& res) {
    json filter = json::object();
    if (req.has_param("kind")) filter["kind"] = req.get_param_value("kind");
    if (req.has_param("status")) filter["status"] = req.get_param_value("status");
    if (req.has_param("parent_id")) filter["parent_id"] = req.get_param_value("parent_id");
    if (req.has_param("limit")) filter["limit"] = std::atoi(req.get_param_value("limit").c_str());
    send_loom(res, loom_list_tasks(ctx_, filter.dump().c_str()));
  });

  svr_.Get(R"(/api/tasks/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    send_loom(res, loom_get_task(ctx_, req.matches[1].str().c_str()));
  });

  svr_.Post("/api/tasks/resume", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_resume_tasks(ctx_));
  });

  svr_.Post(R"(/api/tasks/([^/]+)/cancel)", [this](const httplib::Request& req, httplib::Response& res) {
    int rc = loom_cancel_task(ctx_, req.matches[1].str().c_str());
    send_rc(res, rc, {{"cancelled", true}});
  });
}

// ── Logs & misc ────────────────────────────────────────────────────────

void App::route_logs_misc() {
  svr_.Get("/api/logs", [this](const httplib::Request& req, httplib::Response& res) {
    int max_lines = req.has_param("max_lines") ? std::atoi(req.get_param_value("max_lines").c_str()) : 200;
    send_loom(res, loom_get_logs(max_lines));
  });
  svr_.Get("/api/version", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_version());
  });
  svr_.Get("/api/info", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_info(ctx_));
  });
  svr_.Get("/api/healthz", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(json{{"ok", true}}.dump(), "application/json");
  });
}

// ── Archive (placeholder) ──────────────────────────────────────────────
//
// Another agent is concurrently adding loom_archive_run() and friends to
// loom.h on the main branch. This server does not depend on them. Once
// they land, wire them up here as `/api/archive/...` routes and nowhere
// else - this is the one place archive endpoints belong.

void App::route_archive_placeholder() {
  // POST /api/archive/run — body: the archive config (see loom_archive_run).
  // Streams SSE: {"type":"progress","current","total","status"} ... then one
  // {"type":"done", run...} or {"type":"error", code, message}. Source paths
  // are read on the server machine (read-only). "out_dir" may only be a plain
  // name; files are written to <data_dir>/exports/archive/<name>. A client
  // disconnect pauses the run (resumable).
  svr_.Post("/api/archive/run", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    if (!body.is_object()) {
      send_error(res, "invalid_argument", "body must be a JSON object", 400);
      return;
    }
    if (body.contains("out_dir") && body["out_dir"].is_string()) {
      std::string name = body["out_dir"].get<std::string>();
      if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
          name.find("..") != std::string::npos) {
        send_error(res, "invalid_argument", "out_dir must be a plain name (written under exports/archive/)", 400);
        return;
      }
      json info = parse_or_empty(own_and_free(loom_info(ctx_)));
      std::string data_dir = info.value("data_dir", std::string());
      body["out_dir"] = (fs::path(data_dir) / "exports" / "archive" / name).string();
    }
    auto queue = std::make_shared<SseQueue>();
    LoomContext* ctx = ctx_;
    std::string cfg = body.dump();
    struct Ud {
      LoomContext* ctx;
      SseQueue* q;
      bool cancel_sent = false;
    };
    auto thread_ptr = std::make_shared<std::thread>([ctx, cfg, queue]() {
      Ud ud{ctx, queue.get()};
      const char* result = loom_archive_run(
          ctx, cfg.c_str(),
          [](int current, int total, const char* status, void* p) {
            auto* u = static_cast<Ud*>(p);
            if (u->q->cancelled() && !u->cancel_sent) {
              loom_archive_cancel(u->ctx);
              u->cancel_sent = true;
            }
            json ev = {{"type", "progress"}, {"current", current}, {"total", total}, {"status", status ? status : ""}};
            u->q->push_data(ev.dump());
          },
          &ud);
      json parsed = parse_or_empty(own_and_free(result));
      json out_chunk;
      if (parsed.is_object() && parsed.contains("error")) {
        out_chunk = {{"type", "error"}};
        for (auto& [k, v] : parsed["error"].items()) out_chunk[k] = v;
      } else {
        out_chunk = parsed;
        out_chunk["type"] = "done";
      }
      queue->push_data(out_chunk.dump());
      queue->close();
    });
    res.set_chunked_content_provider(
        "text/event-stream",
        [queue](size_t offset, httplib::DataSink& sink) { return queue->provide(offset, sink); },
        [thread_ptr, queue, ctx](bool success) {
          if (!success) {
            queue->cancel();
            loom_archive_cancel(ctx);
          }
          if (thread_ptr->joinable()) thread_ptr->join();
        });
    res.set_header("Cache-Control", "no-cache");
  });

  svr_.Post("/api/archive/cancel", [this](const httplib::Request&, httplib::Response& res) {
    int rc = loom_archive_cancel(ctx_);
    if (rc == LOOM_E_NOT_FOUND) {
      send_error(res, "not_found", "no archive run in progress", 404);
      return;
    }
    res.set_content(json{{"ok", rc == LOOM_OK}}.dump(), "application/json");
  });

  svr_.Get("/api/archive/status", [this](const httplib::Request& req, httplib::Response& res) {
    std::string run = req.has_param("run_id") ? req.get_param_value("run_id") : std::string();
    send_loom(res, loom_archive_status(ctx_, run.empty() ? nullptr : run.c_str()));
  });

  svr_.Get("/api/artifacts", [this](const httplib::Request& req, httplib::Response& res) {
    json f = json::object();
    if (req.has_param("kind")) f["kind"] = req.get_param_value("kind");
    if (req.has_param("run_id")) f["run_id"] = req.get_param_value("run_id");
    if (req.has_param("limit")) f["limit"] = std::atoi(req.get_param_value("limit").c_str());
    std::string fs_ = f.dump();
    send_loom(res, loom_list_artifacts(ctx_, fs_.c_str()));
  });

  svr_.Get(R"(/api/artifacts/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    bool content = req.has_param("content") && req.get_param_value("content") != "0";
    send_loom(res, loom_get_artifact(ctx_, id.c_str(), content ? 1 : 0));
  });

  // Raw artifact bytes with the artifact's MIME type (for download links).
  svr_.Get(R"(/api/artifacts/([^/]+)/raw)", [this](const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    json j = parse_or_empty(own_and_free(loom_get_artifact(ctx_, id.c_str(), 1)));
    if (j.is_object() && j.contains("error")) {
      std::string code = j["error"].value("code", std::string("internal"));
      send_error(res, code, j["error"].value("message", std::string()), code == "not_found" ? 404 : 400);
      return;
    }
    std::string mime = j["artifact"].value("mime", std::string("application/octet-stream"));
    if (mime.rfind("text/", 0) == 0) mime += "; charset=utf-8";
    res.set_content(j.value("content", std::string()), mime);
  });
}

}  // namespace loom_server
