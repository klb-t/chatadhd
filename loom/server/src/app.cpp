#include "app.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

#include "loom/action_table.h"
#include "sse_stream.h"
#include "onboarding-ui-routes.h"
#include "method-ui-routes.h"
#include "analysis-ui-routes.h"

#if __has_include("loom/usage_policy.h")
#include "loom/usage_policy.h"
#define LOOM_SERVER_HAS_USAGE_POLICY 1
#endif

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
  res.set_content(err.dump(-1, ' ', false, json::error_handler_t::replace), "application/json");
}

// Unlike parse_body (retained for the legacy routes), a malformed request
// must not silently become {} and start a default scan/import/run.
bool object_body(const httplib::Request& req, httplib::Response& res, json& body) {
  body = req.body.empty() ? json::object() : json::parse(req.body, nullptr, false);
  if (body.is_discarded()) {
    send_error(res, "parse", "body must contain valid JSON");
    return false;
  }
  if (!body.is_object()) {
    send_error(res, "invalid_argument", "body must be a JSON object");
    return false;
  }
  return true;
}

bool integer_param(const httplib::Request& req, httplib::Response& res, const char* name,
                   int& value, int minimum) {
  if (!req.has_param(name)) return true;
  const auto text = req.get_param_value(name);
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < minimum) {
    send_error(res, "invalid_argument", std::string(name) + " must be an integer >= " + std::to_string(minimum));
    return false;
  }
  return true;
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
    res.set_content(ok_body.dump(-1, ' ', false, json::error_handler_t::replace), "application/json");
    return;
  }
  send_error(res, errc_name_for_rc(rc), "operation failed (code " + std::to_string(rc) + ")");
}

// Constant-time comparison: guards the bearer-token check against a timing
// side-channel that would otherwise let a remote attacker recover the token
// byte-by-byte from response-time differences (std::string::operator!=
// short-circuits on the first mismatching byte and on a length mismatch).
// Every byte of `b` (the expected value, fixed length) is compared
// regardless of where `a` first diverges; a length mismatch still takes the
// same number of comparisons as a full match would.
bool constant_time_equal(std::string_view a, std::string_view b) {
  unsigned char diff = static_cast<unsigned char>(a.size() != b.size());
  std::size_t n = std::max(a.size(), b.size());
  for (std::size_t i = 0; i < n; ++i) {
    unsigned char ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
    unsigned char cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
    diff |= static_cast<unsigned char>(ca ^ cb);
  }
  return diff == 0;
}

// Session token: Authorization: Bearer <token>, else the named cookie.
std::string session_token_of(const httplib::Request& req, const std::string& cookie_name) {
  const std::string auth = req.get_header_value("Authorization");
  if (auth.rfind("Bearer ", 0) == 0) return auth.substr(7);
  const std::string cookies = req.get_header_value("Cookie");
  std::size_t pos = 0;
  while (pos < cookies.size()) {
    while (pos < cookies.size() && (cookies[pos] == ' ' || cookies[pos] == ';')) ++pos;
    auto end = cookies.find(';', pos);
    if (end == std::string::npos) end = cookies.size();
    auto eq = cookies.find('=', pos);
    if (eq != std::string::npos && eq < end && cookies.compare(pos, eq - pos, cookie_name) == 0)
      return cookies.substr(eq + 1, end - eq - 1);
    pos = end;
  }
  return {};
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

// Safe for a quoted Content-Disposition filename: conv_id is normally
// `c_<12 hex chars>`, but it is taken verbatim from the URL, so defend
// against a value crafted to break out of the quotes (e.g. embedding `"`)
// by keeping only the characters a real id ever has.
std::string sanitize_filename_component(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') out += c;
  }
  return out.empty() ? "export" : out;
}

std::string ext_for_export_format(const std::string& fmt) {
  if (fmt == "markdown") return ".md";
  if (fmt == "text") return ".txt";
  if (fmt == "html") return ".html";
  return ".json";
}


// ── Uploads: streamed to disk, never held in memory (R18) ─────────────────

struct Upload {
  bool has_file = false;
  std::string filename;
  std::string content_type;                   // of the file part, as the sender declared it
  fs::path path;
  std::map<std::string, std::string> fields;  // small non-file multipart fields
  std::string error;
  bool too_large = false;
};

// Extension of an untrusted upload name, kept only when it is a plain alphanumeric suffix.
std::string safe_extension(const std::string& filename) {
  std::string ext = fs::path(filename).extension().string();
  if (ext.size() < 2 || ext.size() > 17) return {};
  for (std::size_t i = 1; i < ext.size(); ++i)
    if (!std::isalnum(static_cast<unsigned char>(ext[i]))) return {};
  return ext;
}

// Per-user scratch folder (owner-only) inside the data dir: uploads never land in a shared /tmp.
fs::path upload_dir_for(LoomContext* ctx) {
  json info = parse_or_empty(own_and_free(loom_info(ctx)));
  const std::string data_dir = info.value("data_dir", std::string());
  std::error_code ec;
  fs::path dir = data_dir.empty() ? fs::temp_directory_path(ec) / "loom-server-uploads"
                                  : fs::path(data_dir) / "tmp" / "uploads";
  fs::create_directories(dir, ec);
  fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
  return dir;
}

// Streams the multipart part "file" into `dir`/<random name> as it arrives. Other parts are collected as
// strings up to `field_limit` bytes each (they are options, not payload).
bool receive_upload(const httplib::Request& req, const httplib::ContentReader& reader, const fs::path& dir,
                    std::size_t field_limit, std::size_t total_limit, std::size_t drain_limit, Upload& up) {
  if (!req.is_multipart_form_data()) {
    up.error = "multipart/form-data with a field \"file\" is required";
    return false;
  }
  std::ofstream out;
  std::string current;
  bool in_file = false, failed = false;
  std::size_t total = 0;  // counts every body byte (total_limit 0 = no limit), so chunked bodies are bounded too
  const bool ok = reader(
      [&](const httplib::MultipartFormData& part) {
        current = part.name;
        in_file = part.name == "file" && !up.has_file;
        if (in_file) {
          up.has_file = true;
          up.filename = part.filename;
          up.content_type = part.content_type;
          up.path = dir / (next_request_id() + safe_extension(part.filename));
          out.open(up.path, std::ios::binary);
          if (!out) { failed = true; up.error = "cannot write the upload"; return false; }
        } else {
          up.fields[current];
        }
        return true;
      },
      [&](const char* data, std::size_t n) {
        total += n;
        // Over the limit: keep reading and discard the rest of the body instead of aborting. Closing a socket
        // that still has unread bytes makes the kernel send a RST, which can destroy the 413 before the client
        // reads it (and a client that retries then resends an already consumed body, i.e. an empty one -> 400).
        if (up.too_large) return drain_limit == 0 || total - total_limit <= drain_limit;  // bounded by --upload-drain-bytes
        if (total_limit != 0 && total > total_limit) {
          failed = true;
          up.too_large = true;
          up.error = "upload is larger than this server accepts (see --max-upload-bytes)";
          out.close();
          std::error_code rm;
          if (!up.path.empty()) fs::remove(up.path, rm);
          return true;
        }
        if (in_file) {
          out.write(data, static_cast<std::streamsize>(n));
          if (!out) { failed = true; up.error = "cannot write the upload (disk full?)"; return false; }
          return true;
        }
        auto& f = up.fields[current];
        if (f.size() + n > field_limit) { failed = true; up.error = "form field too large"; return false; }
        f.append(data, n);
        return true;
      });
  out.close();
  if (!ok || failed) {
    if (up.error.empty()) up.error = "malformed multipart body";
    std::error_code ec;
    if (!up.path.empty()) fs::remove(up.path, ec);
    return false;
  }
  return true;
}

}  // namespace

App::App(ServerOptions opts) : opts_(std::move(opts)) {}

namespace {
// Set by the pre-routing handler of the worker thread that is about to run the route handler (httplib runs
// both on one thread) and cleared at the start of every request.
thread_local LoomContext* t_request_ctx = nullptr;
thread_local std::optional<loom::accounts::User> t_user;
thread_local std::string t_session_token;
}  // namespace

LoomContext* App::rctx() const { return accounts_ ? t_request_ctx : ctx_; }

App::~App() {
  if (ctx_) loom_shutdown(ctx_);
  for (auto& [id, c] : user_ctx_) loom_shutdown(c);
}

LoomContext* App::context_for_user(const std::string& user_id) {
  std::lock_guard lk(pool_mu_);
  auto it = user_ctx_.find(user_id);
  if (it != user_ctx_.end()) return it->second;
  // Each account's runtime is confined (R18): request-supplied paths stay in the account's own data dir (plus
  // the operator's path_roots) and outbound requests cannot reach the server's private network.
  const auto& iso = accounts_->settings().isolation;
  json options = {{"data_dir", accounts_->user_dir(user_id).string()}};
  if (iso.confine_paths) {
    options["confine_paths"] = true;
    options["path_roots"] = iso.path_roots;
  }
  options["zip_max_total_bytes"] = iso.zip_max_total_bytes;
  options["zip_max_ratio"] = iso.zip_max_ratio;
  options["git_allow_repo_config"] = iso.git_allow_repo_config;
  if (iso.block_private_hosts) {
    options["block_private_hosts"] = true;
    options["allow_hosts"] = iso.allow_private_hosts;
  }
  const char* err = nullptr;
  LoomContext* c = loom_init_ex(options.dump().c_str(), &err);
  if (!c) std::cerr << "loom-server: cannot open the data directory of " << user_id << ": " << own_and_free(err) << std::endl;
  else if (err) loom_free_string(err);
  if (c) user_ctx_[user_id] = c;
  return c;
}

bool App::init() {
  if (!opts_.accounts_root.empty()) {
    auto settings = loom::accounts::load_settings(opts_.accounts_root);
    if (!settings) {
      std::cerr << "loom-server: accounts settings: " << settings.error().message << std::endl;
      return false;
    }
    auto store = loom::accounts::AccountStore::open(opts_.accounts_root, std::move(*settings));
    if (!store) {
      std::cerr << "loom-server: accounts store: " << store.error().message << std::endl;
      return false;
    }
    accounts_ = std::move(*store);
  } else {
    const char* data_dir = opts_.data_dir.empty() ? nullptr : opts_.data_dir.c_str();
    ctx_ = loom_init(data_dir);
    if (!ctx_) return false;
  }

  // httplib enforces this only for bodies that announce a Content-Length; the middleware below rejects every
  // other framing and applies the per-route limits before a body is read.
  svr_.set_payload_max_length(opts_.max_upload_bytes == 0 ? (std::numeric_limits<size_t>::max)() : opts_.max_upload_bytes);
  svr_.set_keep_alive_max_count(100);
  SseQueue::keepalive_ms().store(static_cast<long>(opts_.sse_keepalive_ms));
  if (opts_.worker_threads != 0) {
    const std::size_t threads = opts_.worker_threads;
    svr_.new_task_queue = [threads] { return new httplib::ThreadPool(threads); };
  }

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
  SseQueue::stopping().store(true);  // the idle event feeds end within a tick, so the server's threads can be joined
  if (running_.exchange(false)) svr_.stop();
}

void App::register_middleware() {
  // Auth for every /api/ route. Single-user mode: optional static bearer token (off by default, "bind
  // 127.0.0.1, no auth" for local use). Accounts mode (P1): a session token, from the HttpOnly cookie or an
  // Authorization: Bearer header, selects the user and that user's own data directory; the static token is
  // not used then.
  auto body_refusal = [this](const httplib::Request& req) -> int {
    const bool upload = req.method == "POST" && (req.path == "/api/import" || req.path == "/api/media/transcribe" || req.path == "/api/client/blob/upload");
    // Setting (allow_chunked_uploads): such a body is streamed to disk and counted by receive_upload.
    if (opts_.allow_chunked_uploads && upload && !req.has_header("Content-Length")) return 0;
    if (!req.get_header_value("Transfer-Encoding").empty()) return 411;
    const bool writes = req.method == "POST" || req.method == "PUT" || req.method == "PATCH";
    if (!req.has_header("Content-Length")) return writes ? 411 : 0;
    const std::string text = req.get_header_value("Content-Length");
    unsigned long long len = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), len);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return 411;
    const std::size_t limit = upload ? opts_.max_upload_bytes : opts_.max_body_bytes;  // 0 = no limit
    return limit != 0 && len > limit ? 413 : 0;
  };
  auto cross_site = [this](const httplib::Request& req) -> bool {
    if (req.method == "GET" || req.method == "HEAD" || req.method == "OPTIONS") return false;
    if (!req.get_header_value("Authorization").empty()) return false;
    const auto& iso = accounts_->settings().isolation;
    const std::string origin = req.get_header_value("Origin");
    if (origin.empty()) return req.get_header_value("Sec-Fetch-Site") == "cross-site";
    if (origin == "null") return true;
    for (const auto& allowed : iso.csrf_allowed_origins)
      if (allowed == origin) return false;
    const auto scheme = origin.find("://");
    std::string origin_host = scheme == std::string::npos ? origin : origin.substr(scheme + 3);
    std::string host = req.get_header_value("Host");
    if (accounts_->settings().throttle.trust_forwarded_for && !req.get_header_value("X-Forwarded-Host").empty())
      host = req.get_header_value("X-Forwarded-Host");
    auto lower = [](std::string v) {
      std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
      return v;
    };
    return lower(origin_host) != lower(host);
  };
  svr_.set_pre_routing_handler([this, body_refusal, cross_site](const httplib::Request& req, httplib::Response& res) {
    t_request_ctx = nullptr;
    t_user.reset();
    t_session_token.clear();
    // Request framing and size are judged before any body byte is read, for every path (a rejected request
    // closes the connection so unread body bytes cannot be taken for the next request).
    if (const int refused = body_refusal(req); refused != 0) {
      res.set_header("Connection", "close");
      send_error(res, refused == 411 ? "invalid_argument" : "invalid_argument",
                 refused == 411 ? "send a Content-Length; chunked or unframed request bodies are not accepted"
                                : "request body is larger than this server accepts (see --max-body-bytes / --max-upload-bytes)",
                 refused);
      return httplib::Server::HandlerResponse::Handled;
    }
    if (req.path.rfind("/api/", 0) != 0) return httplib::Server::HandlerResponse::Unhandled;
    if (accounts_) {
      std::string token = session_token_of(req, accounts_->settings().cookie.name);
      if (!token.empty()) {
        auto u = accounts_->find_session(token);
        if (u && *u) {
          t_user = **u;
          t_session_token = token;
          t_request_ctx = context_for_user((*u)->id);
        }
      }
      const bool is_public =
          (req.method == "GET" && (req.path == "/api/auth/me" || req.path == "/api/healthz")) ||
          (req.method == "POST" && (req.path == "/api/auth/login" || req.path == "/api/auth/register"));
      // CSRF: a cookie is attached by the browser to whatever site asks; a request that names a foreign
      // origin never acts with it. Bearer-token requests carry no ambient credential and are exempt.
      if (accounts_->settings().isolation.csrf_check_origin && cross_site(req)) {
        res.set_header("Connection", "close");
        send_error(res, "auth", "cross-site request refused", 403);
        return httplib::Server::HandlerResponse::Handled;
      }
      if (!is_public && !t_request_ctx) {
        res.set_header("Connection", "close");
        send_error(res, "auth", "sign in required", 401);
        return httplib::Server::HandlerResponse::Handled;
      }
      return httplib::Server::HandlerResponse::Unhandled;
    }
    if (!opts_.bearer_token.empty()) {
      std::string want = "Bearer " + opts_.bearer_token;
      if (!constant_time_equal(req.get_header_value("Authorization"), want)) {
        // This early handler runs before request-body consumption. Close the
        // connection so rejected POST bytes cannot poison its next request.
        res.set_header("Connection", "close");
        send_error(res, "auth", "missing or invalid bearer token", 401);
        return httplib::Server::HandlerResponse::Handled;
      }
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  // Every response: no sniffing, no framing, no referrer, and API answers (sessions, config, secrets lists)
  // are never cached by a browser or an intermediary.
  svr_.set_post_routing_handler([](const httplib::Request& req, httplib::Response& res) {
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_header("X-Frame-Options", "DENY");
    res.set_header("Referrer-Policy", "no-referrer");
    if (req.path.rfind("/api/", 0) == 0 && !res.has_header("Cache-Control")) res.set_header("Cache-Control", "no-store");
  });

  // CORS is off by default: no Access-Control-* headers are ever added, so
  // browsers enforce same-origin. The web app is always served by this
  // same process, so this never needs to be relaxed for normal use.

  // Defense in depth: cpp-httplib's default reaction to a handler throwing
  // is to set status 500 and echo e.what() back in an EXCEPTION_WHAT
  // response header (see routing() in httplib.h), which can leak internal
  // detail (library messages, occasionally fragments of the offending
  // input) to the client. Every route here is already wrapped by loom's
  // own exception firewall (guard_json/guard_int in src/capi/context.h) so
  // this should never fire in practice, but it is the last line of defence
  // against anything that doesn't go through loom_* (e.g. a future bug in
  // this file). Log server-side, tell the client nothing but a generic
  // error.
  svr_.set_exception_handler([](const httplib::Request& req, httplib::Response& res, const std::exception_ptr& ep) {
    std::string what = "unknown exception";
    try {
      if (ep) std::rethrow_exception(ep);
    } catch (const std::exception& e) {
      what = e.what();
    } catch (...) {
    }
    std::cerr << "loom-server: unhandled exception on " << req.method << " " << req.path << ": " << what
              << std::endl;
    send_error(res, "internal", "internal server error", 500);
  });
}

void App::register_routes() {
  route_auth();
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
  route_knowledge();
  register_onboarding_ui_routes(svr_, [this] { return rctx(); });
  register_method_ui_routes(svr_, [this] { return rctx(); });
  register_analysis_ui_routes(svr_, [this] { return rctx(); });
}

// ── Conversations ──────────────────────────────────────────────────────

void App::route_conversations() {
  svr_.Get("/api/conversations", [this](const httplib::Request& req, httplib::Response& res) {
    int limit = req.has_param("limit") ? std::atoi(req.get_param_value("limit").c_str()) : 50;
    send_loom(res, loom_list_conversations(rctx(), limit));
  });

  svr_.Post("/api/conversations", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    std::string title = body.value("title", std::string());
    send_loom(res, loom_create_conversation(rctx(), title.empty() ? nullptr : title.c_str()), 201);
  });

  svr_.Get(R"(/api/conversations/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    send_loom(res, loom_get_conversation(rctx(), req.matches[1].str().c_str()));
  });

  svr_.Patch(R"(/api/conversations/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    send_loom(res, loom_update_conversation(rctx(), req.matches[1].str().c_str(), body.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
  });

  svr_.Delete(R"(/api/conversations/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    int rc = loom_delete_conversation(rctx(), req.matches[1].str().c_str());
    send_rc(res, rc, {{"deleted", true}});
  });
}

// ── Messages ───────────────────────────────────────────────────────────

void App::route_messages() {
  svr_.Get(R"(/api/conversations/([^/]+)/view)", [this](const httplib::Request& req, httplib::Response& res) {
    res.set_header("Cache-Control", "no-store");
    // P19 additive paging: ?limit= (newest N messages; absent/0 = all) and ?before= (message id cursor).
    json options = json::object();
    if (req.has_param("limit")) options["limit"] = std::atoi(req.get_param_value("limit").c_str());
    if (req.has_param("before")) options["before"] = req.get_param_value("before");
    send_loom(res, loom_read_conversation_view(rctx(), req.matches[1].str().c_str(),
                                               options.dump(-1, ' ', false, json::error_handler_t::replace).c_str(), 0));
  });
  svr_.Post(R"(/api/conversations/([^/]+)/view)", [this](const httplib::Request& req, httplib::Response& res) {
    res.set_header("Cache-Control", "no-store");
    json body;
    if (!object_body(req, res, body)) return;
    // Existing host auth middleware admits this explicit local-read request.
    // Neither JSON nor source metadata supplies the grant; no egress is granted.
    send_loom(res, loom_read_conversation_view(rctx(), req.matches[1].str().c_str(), body.dump().c_str(), 1));
  });
  svr_.Get(R"(/api/conversations/([^/]+)/messages)", [this](const httplib::Request& req, httplib::Response& res) {
    std::string conv_id = req.matches[1].str();
    bool all = req.has_param("all") && req.get_param_value("all") != "0" && req.get_param_value("all") != "false";
    if (req.has_param("limit") || req.has_param("before")) {
      // P19 additive paging: the answer is then {messages, total, truncated, next_before} (without it, the plain array).
      json request = json::object();
      request["conv_id"] = conv_id;
      request["include_all"] = all;
      if (req.has_param("limit")) request["limit"] = std::atoi(req.get_param_value("limit").c_str());
      if (req.has_param("before")) request["before"] = req.get_param_value("before");
      send_loom(res, loom_get_messages_ex(rctx(), request.dump(-1, ' ', false, json::error_handler_t::replace).c_str(), all ? 1 : 0));
      return;
    }
    send_loom(res, all ? loom_get_messages_ex(rctx(), conv_id.c_str(), 1) : loom_get_messages(rctx(), conv_id.c_str()));
  });

  svr_.Get(R"(/api/messages/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    send_loom(res, loom_get_message(rctx(), req.matches[1].str().c_str()));
  });

  svr_.Post(R"(/api/messages/([^/]+)/edit)", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    std::string text = body.value("text", std::string());
    send_loom(res, loom_edit_message(rctx(), req.matches[1].str().c_str(), text.c_str()));
  });

  svr_.Post(R"(/api/messages/([^/]+)/restore)", [this](const httplib::Request& req, httplib::Response& res) {
    int rc = loom_restore_version(rctx(), req.matches[1].str().c_str());
    send_rc(res, rc, {{"restored", true}});
  });

  svr_.Get(R"(/api/messages/([^/]+)/versions)", [this](const httplib::Request& req, httplib::Response& res) {
    send_loom(res, loom_get_versions(rctx(), req.matches[1].str().c_str()));
  });

  svr_.Post(R"(/api/messages/([^/]+)/status)", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    std::string status = body.value("status", std::string());
    int rc = loom_set_message_status(rctx(), req.matches[1].str().c_str(), status.c_str());
    send_rc(res, rc, {{"status", status}});
  });

  svr_.Patch(R"(/api/messages/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    std::string id = req.matches[1].str();
    int rc = loom_update_message(rctx(), id.c_str(), body.dump(-1, ' ', false, json::error_handler_t::replace).c_str());
    if (rc != LOOM_OK) {
      send_rc(res, rc, json::object());
      return;
    }
    send_loom(res, loom_get_message(rctx(), id.c_str()));
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
    send_loom(res, loom_search(rctx(), q.c_str(), opts.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
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
    std::string request_json = body.dump(-1, ' ', false, json::error_handler_t::replace);

    auto queue = std::make_shared<SseQueue>();
    LoomContext* ctx = rctx();
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

  // Read-only usage preview for a would-be send (nothing persisted or sent).
  svr_.Post("/api/chat/usage-preview", [this](const httplib::Request& req, httplib::Response& res) {
    json body;
    if (!object_body(req, res, body)) return;
#ifdef LOOM_SERVER_HAS_USAGE_POLICY
    body["action"] = "chat_preview";
    send_loom(res, loom_usage_policy_json(rctx(), body.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
#else
    send_error(res, "unavailable", "Native usage policy is not installed in this build");
#endif
  });

  svr_.Post("/api/chat/cancel", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    std::string request_id = body.value("request_id", req.get_param_value("request_id"));
    if (request_id.empty()) {
      send_error(res, "invalid_argument", "\"request_id\" is required", 400);
      return;
    }
    int rc = loom_chat_cancel(rctx(), request_id.c_str());
    send_rc(res, rc, {{"cancelled", true}});
  });
}

// ── Models & providers ─────────────────────────────────────────────────

void App::route_models_providers() {
  svr_.Get("/api/media/status", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_media_status(rctx()));
  });
  svr_.Post("/api/media/transcribe", [this](const httplib::Request& req, httplib::Response& res,
                                             const httplib::ContentReader& reader) {
    LoomContext* ctx = rctx();
    Upload up;
    struct Cleanup {
      fs::path path;
      ~Cleanup() { std::error_code ignored; if (!path.empty()) fs::remove(path, ignored); }
    } cleanup;
    const bool received = receive_upload(req, reader, upload_dir_for(ctx), opts_.max_body_bytes ? opts_.max_body_bytes : (std::numeric_limits<std::size_t>::max)(), opts_.max_upload_bytes, opts_.upload_drain_bytes, up);
    cleanup.path = up.path;
    if (!received) {
      res.set_header("Connection", "close");  // the unread rest of the body must not be parsed as the next request
      send_error(res, "invalid_argument", up.error, up.too_large ? 413 : 400);
      return;
    }
    if (!up.has_file) {
      send_error(res, "invalid_argument", "multipart field file is required");
      return;
    }
    json options = json::object();
    if (up.fields.count("options")) {
      options = json::parse(up.fields["options"], nullptr, false);
      if (options.is_discarded() || !options.is_object()) {
        send_error(res, "invalid_argument", "options must be a JSON object");
        return;
      }
    }
    const auto encoded = options.dump();
    send_loom(res, loom_transcribe(ctx, up.path.string().c_str(), encoded.c_str()));
  });
  // Client blobs (P14 6): the recording or picture of a device step reaches the kernel as an upload streamed to disk (never base64 in JSON)
  // and is handed back as a stream of the stored file. The bytes of a blob are named by their hash in the user's own store; the kernel's
  // client.blob_* actions (client_blobs.h) do the checks, these routes only move bytes.
  svr_.Post("/api/client/blob/upload", [this](const httplib::Request& req, httplib::Response& res,
                                              const httplib::ContentReader& reader) {
    LoomContext* ctx = rctx();
    Upload up;
    struct Cleanup {
      fs::path path;
      ~Cleanup() { std::error_code ignored; if (!path.empty()) fs::remove(path, ignored); }
    } cleanup;
    const bool received = receive_upload(req, reader, upload_dir_for(ctx), opts_.max_body_bytes ? opts_.max_body_bytes : (std::numeric_limits<std::size_t>::max)(), opts_.max_upload_bytes, opts_.upload_drain_bytes, up);
    cleanup.path = up.path;
    if (!received) {
      res.set_header("Connection", "close");  // the unread rest of the body must not be parsed as the next request
      send_error(res, "invalid_argument", up.error);
      return;
    }
    if (!up.has_file) {
      send_error(res, "invalid_argument", "multipart field file is required");
      return;
    }
    std::string mime = up.fields.count("mime") ? up.fields["mime"] : up.content_type;
    if (mime.empty()) mime = "application/octet-stream";
    const json request = {{"path", up.path.string()}, {"mime", mime}};
    send_loom(res, loom_invoke(ctx, "actions", "client.blob_put", request.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
  });
  svr_.Get(R"(/api/client/blob/([0-9a-fA-F]{64})/raw)", [this](const httplib::Request& req, httplib::Response& res) {
    const json request = {{"hash", req.matches[1].str()}};
    const std::string raw = own_and_free(loom_invoke(rctx(), "actions", "client.blob_path", request.dump().c_str()));
    json found = parse_or_empty(raw);
    if (found.is_object() && found.contains("error")) {
      res.status = status_for_error_code(found["error"].value("code", std::string("internal")));
      res.set_content(raw, "application/json");
      return;
    }
    auto file = std::make_shared<std::ifstream>(found.value("path", std::string()), std::ios::binary);
    if (!*file) {
      send_error(res, "not_found", "the blob has no readable file", 404);
      return;
    }
    const std::string mime = found.value("mime", std::string());
    // What a device brought is bytes to play or show, never a page of this origin: whatever its type says, it runs no script here.
    res.set_header("Content-Security-Policy", "sandbox; default-src 'none'");
    res.set_content_provider(found.value("size", static_cast<std::size_t>(0)), mime.empty() ? "application/octet-stream" : mime,
                             [file](std::size_t offset, std::size_t length, httplib::DataSink& sink) {
                               std::vector<char> buffer((std::min)(length, static_cast<std::size_t>(1) << 16));
                               file->clear();
                               file->seekg(static_cast<std::streamoff>(offset));
                               file->read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                               const auto got = static_cast<std::size_t>(file->gcount());
                               return got != 0 && sink.write(buffer.data(), got);
                             });
  });
  svr_.Get("/api/models", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_get_models(rctx()));
  });
  svr_.Post("/api/models/refresh", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_refresh_models(rctx()));
  });
  svr_.Get("/api/providers", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_get_providers(rctx()));
  });
}

// ── Config & secrets ───────────────────────────────────────────────────

void App::route_config_secrets() {
  // Static-kernel command adapter. No parallel HTTP ledger or approval engine.
  // Capture the context only in builds that contain the native policy adapter.
#ifdef LOOM_SERVER_HAS_USAGE_POLICY
  svr_.Post("/api/usage-policy", [this](const httplib::Request& req, httplib::Response& res) {
    [[maybe_unused]] LoomContext* ctx = rctx();
#else
  svr_.Post("/api/usage-policy", [](const httplib::Request& req, httplib::Response& res) {
#endif
    json body;
    if (!object_body(req, res, body)) return;
#ifdef LOOM_SERVER_HAS_USAGE_POLICY
    send_loom(res, loom_usage_policy_json(ctx, req.body.c_str()));
#else
    send_error(res, "unavailable", "Native usage policy is not installed in this build");
#endif
  });
  // Field metadata (default presence + edit rule) of every config/secret key; no secret values.
  svr_.Get("/api/settings/fields", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_settings_fields(rctx()));
  });
  svr_.Get("/api/config", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_get_config(rctx()));
  });

  // Merge-patch: {"key": value, ...} -> loom_set_config_json.
  svr_.Patch("/api/config", [this](const httplib::Request& req, httplib::Response& res) {
    json body;
    if (!object_body(req, res, body)) return;
    std::lock_guard config_guard(native_ui::config_mutex());
    // Validation must not reorder opaque method/profile DTOs before the
    // native ordered-json config reader sees them.
    const std::string patch_bytes = req.body.empty() ? "{}" : req.body;
    int rc = loom_set_config_json(rctx(), patch_bytes.c_str());
    if (rc != LOOM_OK) {
      send_rc(res, rc, json::object());
      return;
    }
    send_loom(res, loom_get_config(rctx()));
  });

  // Single-key set: PUT /api/config/:key {"value": <json or raw text>}.
  svr_.Put(R"(/api/config/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    json body;
    if (!object_body(req, res, body)) return;
    if (!body.contains("value")) {
      send_error(res, "invalid_argument", "Configuration request requires a value");
      return;
    }
    const std::string key = req.matches[1].str();
    const auto ordered_body = nlohmann::ordered_json::parse(req.body);
    const nlohmann::ordered_json patch = {{key, ordered_body["value"]}};
    std::lock_guard config_guard(native_ui::config_mutex());
    const int rc = loom_set_config_json(rctx(), patch.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace).c_str());
    if (rc < 0) {
      send_error(res, errc_name_for_rc(rc), "Configuration value was rejected");
      return;
    }
    send_loom(res, loom_get_config(rctx()));
  });

  // Secrets are write-only: values never round-trip back to the client.
  svr_.Get("/api/secrets", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_list_secret_keys(rctx()));
  });
  svr_.Post(R"(/api/secrets/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    std::string value = body.value("value", std::string());
    int rc = loom_set_secret(rctx(), req.matches[1].str().c_str(), value.c_str());
    send_rc(res, rc, {{"set", true}});
  });
  svr_.Get(R"(/api/secrets/([^/]+)/has)", [this](const httplib::Request& req, httplib::Response& res) {
    int rc = loom_has_secret(rctx(), req.matches[1].str().c_str());
    if (rc < 0) {
      send_rc(res, rc, json::object());
      return;
    }
    res.set_content(json{{"has", rc == 1}}.dump(-1, ' ', false, json::error_handler_t::replace), "application/json");
  });
  svr_.Delete(R"(/api/secrets/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    int rc = loom_delete_secret(rctx(), req.matches[1].str().c_str());
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
    send_loom(res, loom_get_nodes(rctx(), filter.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
  });

  svr_.Get("/api/graph/edges", [this](const httplib::Request& req, httplib::Response& res) {
    json filter = json::object();
    if (req.has_param("node_id")) filter["node_id"] = req.get_param_value("node_id");
    if (req.has_param("link_type")) filter["link_type"] = req.get_param_value("link_type");
    if (req.has_param("limit")) filter["limit"] = std::atoi(req.get_param_value("limit").c_str());
    send_loom(res, loom_get_edges(rctx(), filter.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
  });

  svr_.Post("/api/graph/expand", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    json seeds = body.value("seed_ids", json::array());
    int depth = body.value("depth", 1);
    send_loom(res, loom_expand_graph(rctx(), seeds.dump(-1, ' ', false, json::error_handler_t::replace).c_str(), depth));
  });

  svr_.Get("/api/graph/data", [this](const httplib::Request& req, httplib::Response& res) {
    // conv_id; R27 additive paging: limit (message nodes; 0 = whole conversation; absent = config
    // loom_graph_data_limit) and before (message id cursor, from the previous page's next_before).
    std::string conv_id = req.get_param_value("conv_id");
    if (conv_id.empty() || (!req.has_param("limit") && !req.has_param("before"))) {
      send_loom(res, loom_get_graph_data(rctx(), conv_id.empty() ? nullptr : conv_id.c_str()));
      return;
    }
    json request = json::object();
    request["conv_id"] = conv_id;
    if (req.has_param("limit")) request["limit"] = std::atoi(req.get_param_value("limit").c_str());
    if (req.has_param("before")) request["before"] = req.get_param_value("before");
    send_loom(res, loom_get_graph_data(rctx(), request.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
  });

  svr_.Get("/api/graph/entry", [this](const httplib::Request& req, httplib::Response& res) {
    json request = json::object();
    if (req.has_param("lang")) request["lang"] = req.get_param_value("lang");
    if (req.has_param("include_hidden")) request["include_hidden"] = req.get_param_value("include_hidden") == "true";
    send_loom(res, loom_graph_entry(rctx(), request.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
  });

  // P24: graph views as data (effective view of edge kinds / perspectives / scopes ..., perspectives side by side, the scope resolver).
  auto dump_body = [](const json& body) { return body.dump(-1, ' ', false, json::error_handler_t::replace); };
  svr_.Get(R"(/api/views/([A-Za-z0-9_.-]+))", [this, dump_body](const httplib::Request& req, httplib::Response& res) {
    json request = json::object();
    if (req.has_param("include_hidden")) request["include_hidden"] = req.get_param_value("include_hidden") == "true";
    send_loom(res, loom_view_effective(rctx(), req.matches[1].str().c_str(), dump_body(request).c_str()));
  });
  svr_.Post("/api/views/write", [this, dump_body](const httplib::Request& req, httplib::Response& res) {
    send_loom(res, loom_view_write(rctx(), dump_body(parse_body(req)).c_str()));
  });
  svr_.Post("/api/edge-kinds", [this, dump_body](const httplib::Request& req, httplib::Response& res) {
    send_loom(res, loom_edge_kinds(rctx(), dump_body(parse_body(req)).c_str()));
  });
  svr_.Get("/api/edge-kinds", [this, dump_body](const httplib::Request& req, httplib::Response& res) {
    json request = json::object();
    if (req.has_param("kind")) request["kind"] = req.get_param_value("kind");
    if (req.has_param("classify")) request["classify"] = req.get_param_value("classify") == "true";
    send_loom(res, loom_edge_kinds(rctx(), dump_body(request).c_str()));
  });
  svr_.Get("/api/perspectives", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_perspectives(rctx(), "{}"));
  });
  svr_.Post("/api/perspectives", [this, dump_body](const httplib::Request& req, httplib::Response& res) {
    send_loom(res, loom_perspectives(rctx(), dump_body(parse_body(req)).c_str()));
  });
  svr_.Post("/api/scopes/resolve", [this, dump_body](const httplib::Request& req, httplib::Response& res) {
    send_loom(res, loom_scope_resolve(rctx(), dump_body(parse_body(req)).c_str()));
  });

  // Storage plugins (P4) and the action/element matching queries (wave 2: /api/actions/for, explain,
  // propose_chain; R54: /api/actions/run|preview|confirm|cancel|status and /api/chat/route): routes come from the action table (loom/action_table.h) and go through
  // loom_invoke. Rides on rctx(), so with accounts enabled every account sees and changes only its own
  // placement. Responses are the same as the former per-route loom_github_sync calls.
  for (const auto& row : json::parse(loom::action_table().dump())) {
    const std::string row_port = row.value("port", std::string());
    if (row_port != "storage" && row_port != "actions" && row_port != "chat" && row_port != "environments") continue;
    const std::string verb = row.value("verb", std::string());
    const std::string path = row.value("path", std::string());
    const std::string port = row.value("port", std::string());
    const std::string action = row.value("action", std::string());
    json query = row.value("query", json::array());
    auto handler = [this, port, action, verb, query](const httplib::Request& req, httplib::Response& res) {
      json body = json::object();
      if (verb == "POST") {
        body = parse_body(req);
      } else {
        for (const auto& q : query) {
          const std::string name = q.value("name", std::string());
          if (req.has_param(name)) body[name] = req.get_param_value(name);
          else if (q.value("always", false)) body[name] = std::string();
        }
      }
      send_loom(res, loom_invoke(rctx(), port.c_str(), action.c_str(),
                                 body.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
    };
    if (verb == "POST") svr_.Post(path, handler);
    else svr_.Get(path, handler);
  }

  svr_.Post("/api/graph/reindex", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    std::string conv_id = body.value("conv_id", std::string());
    send_loom(res, loom_graph_reindex(rctx(), conv_id.empty() ? nullptr : conv_id.c_str()));
  });

  svr_.Post("/api/context/select", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    send_loom(res, loom_select_context_ex(rctx(), body.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
  });
}

// ── Semantic worker ────────────────────────────────────────────────────

void App::route_semantic() {
  // Read-only resource-economy report (R50). ?mode=off|report|propose overrides the preset for this call.
  svr_.Get("/api/economy/report", [this](const httplib::Request& req, httplib::Response& res) {
#ifdef LOOM_SERVER_HAS_USAGE_POLICY
    json command{{"action", "economy_report"}};
    if (req.has_param("mode")) command["overrides"] = json{{"mode", req.get_param_value("mode")}};
    send_loom(res, loom_usage_policy_json(rctx(), command.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
#else
    (void)req;
    send_error(res, "unavailable", "Native usage policy is not installed in this build");
#endif
  });
  svr_.Get("/api/semantic/status", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_semantic_status(rctx()));
  });
  svr_.Post("/api/semantic/pause", [this](const httplib::Request&, httplib::Response& res) {
    loom_semantic_pause(rctx());
    send_loom(res, loom_semantic_status(rctx()));
  });
  svr_.Post("/api/semantic/resume", [this](const httplib::Request&, httplib::Response& res) {
    loom_semantic_resume(rctx());
    send_loom(res, loom_semantic_status(rctx()));
  });
  // Answer a batch submission paused by the shared usage guard.
  svr_.Post("/api/semantic/usage-confirm", [this](const httplib::Request& req, httplib::Response& res) {
    json body;
    if (!object_body(req, res, body)) return;
#ifdef LOOM_SERVER_HAS_USAGE_POLICY
    body["action"] = "semantic_confirm";
    send_loom(res, loom_usage_policy_json(rctx(), body.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
#else
    send_error(res, "unavailable", "Native usage policy is not installed in this build");
#endif
  });
  // Bulk requeue of failed and/or stalled items (history of earlier attempts is kept).
  svr_.Post("/api/semantic/requeue", [this](const httplib::Request& req, httplib::Response& res) {
    json body = json::object();
    if (!req.body.empty() && !object_body(req, res, body)) return;
#ifdef LOOM_SERVER_HAS_USAGE_POLICY
    json command{{"action", "semantic_requeue"}, {"filter", body}};
    send_loom(res, loom_usage_policy_json(rctx(), command.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
#else
    send_error(res, "unavailable", "Native usage policy is not installed in this build");
#endif
  });
  // retry_mode: explicit_requeue | automatic (a setting, never a limit).
  svr_.Post("/api/semantic/retry-mode", [this](const httplib::Request& req, httplib::Response& res) {
    json body;
    if (!object_body(req, res, body)) return;
#ifdef LOOM_SERVER_HAS_USAGE_POLICY
    body["action"] = "semantic_retry_mode";
    send_loom(res, loom_usage_policy_json(rctx(), body.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
#else
    send_error(res, "unavailable", "Native usage policy is not installed in this build");
#endif
  });
  svr_.Post("/api/semantic/wake", [this](const httplib::Request&, httplib::Response& res) {
    loom_semantic_wake(rctx());
    send_loom(res, loom_semantic_status(rctx()));
  });
}

// ── Memory tree ────────────────────────────────────────────────────────

void App::route_memory() {
  svr_.Get("/api/memory", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_list_memory(rctx()));
  });
  svr_.Post("/api/memory", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    send_loom(res, loom_create_memory(rctx(), body.dump(-1, ' ', false, json::error_handler_t::replace).c_str()), 201);
  });
  svr_.Patch(R"(/api/memory/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    json body = parse_body(req);
    send_loom(res, loom_update_memory(rctx(), req.matches[1].str().c_str(), body.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
  });
  svr_.Delete(R"(/api/memory/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    int rc = loom_delete_memory(rctx(), req.matches[1].str().c_str());
    send_rc(res, rc, {{"deleted", true}});
  });
  svr_.Get("/api/memory/context", [this](const httplib::Request& req, httplib::Response& res) {
    int max_chars = req.has_param("max_chars") ? std::atoi(req.get_param_value("max_chars").c_str()) : 0;
    send_loom(res, loom_get_memory_context(rctx(), max_chars));
  });
}

// ── Import / export ────────────────────────────────────────────────────

void App::route_import_export() {
  svr_.Post("/api/import", [this](const httplib::Request& req, httplib::Response& res,
                                   const httplib::ContentReader& reader) {
    LoomContext* ctx = rctx();
    Upload up;
    if (!receive_upload(req, reader, upload_dir_for(ctx), opts_.max_body_bytes ? opts_.max_body_bytes : (std::numeric_limits<std::size_t>::max)(), opts_.max_upload_bytes, opts_.upload_drain_bytes, up)) {
      res.set_header("Connection", "close");  // the unread rest of the body must not be parsed as the next request
      send_error(res, "invalid_argument", up.error, up.too_large ? 413 : 400);
      return;
    }
    if (!up.has_file) {
      send_error(res, "invalid_argument", "multipart field \"file\" is required", 400);
      return;
    }
    auto field = [&](const char* name) {
      if (req.has_param(name)) return req.get_param_value(name);
      auto it = up.fields.find(name);
      return it == up.fields.end() ? std::string() : it->second;
    };
    std::string title = field("title");
    // R50: optional duplicate_policy (reimport_and_link | reuse_prior | reimport);
    // absent = the import pack default.
    std::string duplicate_policy = field("duplicate_policy");
    fs::path tmp_path = up.path;

    auto queue = std::make_shared<SseQueue>();
    std::string path_str = tmp_path.string();
    std::string title_copy = title;
    json options = json::object();
    if (!title_copy.empty()) options["title"] = title_copy;
    if (!duplicate_policy.empty()) options["duplicate_policy"] = duplicate_policy;
    std::string options_json = options.dump();
    auto thread_ptr = std::make_shared<std::thread>([ctx, path_str, options_json, queue]() {
      const char* result = loom_import_file_ex(
          ctx, path_str.c_str(), options_json.c_str(),
          [](int current, int total, const char* status, void* ud) {
            auto* q = static_cast<SseQueue*>(ud);
            json p = {{"type", "progress"}, {"current", current}, {"total", total}, {"status", status ? status : ""}};
            q->push_data(p.dump(-1, ' ', false, json::error_handler_t::replace));
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
      queue->push_data(out_chunk.dump(-1, ' ', false, json::error_handler_t::replace));
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
    std::string raw = own_and_free(loom_export_conversation(rctx(), conv_id.c_str(), fmt.c_str()));
    json parsed = parse_or_empty(raw);
    if (parsed.is_object() && parsed.contains("error")) {
      res.status = status_for_error_code(parsed["error"].value("code", std::string("internal")));
      res.set_content(raw, "application/json");
      return;
    }
    std::string content = parsed.value("content", std::string());
    std::string out_fmt = parsed.value("format", fmt);
    res.set_header("Content-Disposition", "attachment; filename=\"" + sanitize_filename_component(conv_id) +
                                              ext_for_export_format(out_fmt) + "\"");
    res.set_content(content, mime_for_export_format(out_fmt));
  });
}

// ── Provenance, events & tasks ─────────────────────────────────────────

void App::route_provenance_events_tasks() {
  svr_.Get("/api/sources", [this](const httplib::Request& req, httplib::Response& res) {
    int limit = req.has_param("limit") ? std::atoi(req.get_param_value("limit").c_str()) : 100;
    send_loom(res, loom_list_sources(rctx(), limit));
  });

  svr_.Get(R"(/api/provenance/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    send_loom(res, loom_get_provenance(rctx(), req.matches[1].str().c_str()));
  });

  // One-shot poll.
  svr_.Get("/api/events", [this](const httplib::Request& req, httplib::Response& res) {
    json q = json::object();
    if (req.has_param("after_seq")) q["after_seq"] = std::atoll(req.get_param_value("after_seq").c_str());
    if (req.has_param("type")) q["type"] = req.get_param_value("type");
    if (req.has_param("subject_id")) q["subject_id"] = req.get_param_value("subject_id");
    if (req.has_param("limit")) q["limit"] = std::atoi(req.get_param_value("limit").c_str());
    send_loom(res, loom_query_events(rctx(), q.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
  });

  // Live SSE feed via loom_subscribe.
  svr_.Get("/api/events/stream", [this](const httplib::Request& req, httplib::Response& res) {
    std::string event = req.has_param("event") ? req.get_param_value("event") : "*";
    auto queue = std::make_shared<SseQueue>();
    LoomContext* ctx = rctx();

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
          s->queue->push_data(chunk.dump(-1, ' ', false, json::error_handler_t::replace));
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
    send_loom(res, loom_list_tasks(rctx(), filter.dump(-1, ' ', false, json::error_handler_t::replace).c_str()));
  });

  svr_.Get(R"(/api/tasks/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    send_loom(res, loom_get_task(rctx(), req.matches[1].str().c_str()));
  });

  svr_.Post("/api/tasks/resume", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_resume_tasks(rctx()));
  });

  svr_.Post(R"(/api/tasks/([^/]+)/cancel)", [this](const httplib::Request& req, httplib::Response& res) {
    int rc = loom_cancel_task(rctx(), req.matches[1].str().c_str());
    send_rc(res, rc, {{"cancelled", true}});
  });
}

// ── Logs & misc ────────────────────────────────────────────────────────

void App::route_logs_misc() {
  svr_.Get("/api/logs", [this](const httplib::Request& req, httplib::Response& res) {
    // The log buffer is process-wide: with accounts it holds every user's lines (paths, ids, errors).
    if (accounts_ && !accounts_->settings().isolation.expose_server_logs) {
      send_error(res, "auth", "the server log is not available to accounts (server_accounts: expose_server_logs)", 403);
      return;
    }
    int max_lines = req.has_param("max_lines") ? std::atoi(req.get_param_value("max_lines").c_str()) : 200;
    send_loom(res, loom_get_logs(max_lines));
  });
  svr_.Get("/api/version", [](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_version());
  });
  svr_.Get("/api/info", [this](const httplib::Request&, httplib::Response& res) {
    if (accounts_) {  // the server's directory layout is not the account's business
      json info = parse_or_empty(own_and_free(loom_info(rctx())));
      if (info.is_object()) { info.erase("data_dir"); info.erase("paths"); }
      res.set_content(info.dump(-1, ' ', false, json::error_handler_t::replace), "application/json");
      return;
    }
    send_loom(res, loom_info(rctx()));
  });
  svr_.Get("/api/healthz", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(json{{"ok", true}}.dump(-1, ' ', false, json::error_handler_t::replace), "application/json");
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
      json info = parse_or_empty(own_and_free(loom_info(rctx())));
      std::string data_dir = info.value("data_dir", std::string());
      body["out_dir"] = (fs::path(data_dir) / "exports" / "archive" / name).string();
    }
    auto queue = std::make_shared<SseQueue>();
    LoomContext* ctx = rctx();
    std::string cfg = body.dump(-1, ' ', false, json::error_handler_t::replace);
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
            u->q->push_data(ev.dump(-1, ' ', false, json::error_handler_t::replace));
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
      queue->push_data(out_chunk.dump(-1, ' ', false, json::error_handler_t::replace));
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
    int rc = loom_archive_cancel(rctx());
    if (rc == LOOM_E_NOT_FOUND) {
      send_error(res, "not_found", "no archive run in progress", 404);
      return;
    }
    res.set_content(json{{"ok", rc == LOOM_OK}}.dump(-1, ' ', false, json::error_handler_t::replace), "application/json");
  });

  svr_.Get("/api/archive/status", [this](const httplib::Request& req, httplib::Response& res) {
    std::string run = req.has_param("run_id") ? req.get_param_value("run_id") : std::string();
    send_loom(res, loom_archive_status(rctx(), run.empty() ? nullptr : run.c_str()));
  });

  svr_.Get("/api/artifacts", [this](const httplib::Request& req, httplib::Response& res) {
    json f = json::object();
    if (req.has_param("kind")) f["kind"] = req.get_param_value("kind");
    if (req.has_param("run_id")) f["run_id"] = req.get_param_value("run_id");
    if (req.has_param("limit")) f["limit"] = std::atoi(req.get_param_value("limit").c_str());
    std::string fs_ = f.dump(-1, ' ', false, json::error_handler_t::replace);
    send_loom(res, loom_list_artifacts(rctx(), fs_.c_str()));
  });

  svr_.Get(R"(/api/artifacts/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    bool content = req.has_param("content") && req.get_param_value("content") != "0";
    send_loom(res, loom_get_artifact(rctx(), id.c_str(), content ? 1 : 0));
  });

  // Raw artifact bytes with the artifact's MIME type (for download links).
  svr_.Get(R"(/api/artifacts/([^/]+)/raw)", [this](const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    json j = parse_or_empty(own_and_free(loom_get_artifact(rctx(), id.c_str(), 1)));
    if (j.is_object() && j.contains("error")) {
      std::string code = j["error"].value("code", std::string("internal"));
      send_error(res, code, j["error"].value("message", std::string()), code == "not_found" ? 404 : 400);
      return;
    }
    std::string mime = j["artifact"].value("mime", std::string("application/octet-stream"));
    if (mime.rfind("text/", 0) == 0) mime += "; charset=utf-8";
    // Artifact bytes come from imported archives, not from this app: served as inert data (an html/svg
    // artifact must not run script with the session of whoever opens it).
    res.set_header("Content-Security-Policy", "sandbox; default-src 'none'; style-src 'unsafe-inline'; img-src data:");
    res.set_content(j.value("content", std::string()), mime);
  });
}

// ── Knowledge, catalog and goal-directed context ───────────────────────

void App::route_knowledge() {
  // Common transport plumbing only: validation and meaning of the options
  // belong to the C ABI. Results keep their native shape, including the
  // assessment/provenance on claims and per-stage pipeline failure status.
  auto post_json = [this](const char* path, const char* (*call)(LoomContext*, const char*)) {
    svr_.Post(path, [this, call](const httplib::Request& req, httplib::Response& res) {
      json body;
      if (!object_body(req, res, body)) return;
      const std::string payload = body.dump();
      send_loom(res, call(rctx(), payload.c_str()));
    });
  };
  auto post_progress = [this](const char* path,
                             const char* (*call)(LoomContext*, const char*, LoomProgressCallback, void*)) {
    svr_.Post(path, [this, call](const httplib::Request& req, httplib::Response& res) {
      json body;
      if (!object_body(req, res, body)) return;
      const std::string payload = body.dump();
      send_loom(res, call(rctx(), payload.c_str(), nullptr, nullptr));
    });
  };

  svr_.Get("/api/knowledge/pack", [this](const httplib::Request&, httplib::Response& res) {
    send_loom(res, loom_kb_pack(rctx()));
  });
  svr_.Get("/api/knowledge/policy", [this](const httplib::Request& req, httplib::Response& res) {
    const auto name = req.get_param_value("name");
    send_loom(res, loom_kb_policy(rctx(), name.c_str()));
  });
  svr_.Get("/api/knowledge/runs", [this](const httplib::Request& req, httplib::Response& res) {
    int limit = 50;
    if (!integer_param(req, res, "limit", limit, 1)) return;
    send_loom(res, loom_kb_runs(rctx(), limit));
  });
  post_json("/api/knowledge/query", loom_kb_query);
  // Explicit selection/acceptance and immutable receipt read/replay retain
  // their existing C ABI validation, transactions and error envelopes. Pass
  // the original JSON bytes rather than reordering selected DTO object fields.
  svr_.Post("/api/graph/packets/store", [this](const httplib::Request& req, httplib::Response& res) {
    json body;
    if (!object_body(req, res, body)) return;
    send_loom(res, loom_graph_packet_store(rctx(), req.body.c_str()));
  });
  // Keep exact embedded reply bytes; strict parser rejects duplicate keys.
  svr_.Post("/api/packet", [this](const httplib::Request& req, httplib::Response& res) {
    // Literal NUL is invalid JSON and would truncate the C-string ABI input.
    if (req.body.find('\0') != std::string::npos) {
      send_error(res, "parse", "packet request contains literal NUL");
      return;
    }
    send_loom(res, loom_packet(rctx(), req.body.c_str()));
    const auto value = json::parse(res.body, nullptr, false);
    if (value.is_object() && value.contains("error") && value["error"].is_object())
      res.status = status_for_error_code(value["error"].value("code", std::string("internal")));
  });
  post_json("/api/knowledge/judge", loom_kb_judge);
  post_json("/api/knowledge/materialize", loom_materialize);
  post_json("/api/knowledge/predict", loom_generalize_predict);
  post_json("/api/context/build", loom_context_build);

  svr_.Post("/api/knowledge/run", [this](const httplib::Request& req, httplib::Response& res) {
    json body;
    if (!object_body(req, res, body)) return;
    // Match the existing archive endpoint's output boundary. The client
    // supplies a name, not an arbitrary path on the server's filesystem.
    // Empty/absent out_dir keeps the native artifacts-only behavior.
    if (body.contains("out_dir")) {
      if (!body["out_dir"].is_string()) {
        send_error(res, "invalid_argument", "out_dir must be a string");
        return;
      }
      const auto name = body["out_dir"].get<std::string>();
      if (!name.empty()) {
        if (name == "." || name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
            name.find("..") != std::string::npos || name.find('\0') != std::string::npos) {
          send_error(res, "invalid_argument", "out_dir must be a plain name (written under exports/knowledge/)");
          return;
        }
        const auto info = parse_or_empty(own_and_free(loom_info(rctx())));
        const auto data_dir = info.value("data_dir", std::string());
        if (data_dir.empty()) {
          send_error(res, "internal", "data directory unavailable");
          return;
        }
        body["out_dir"] = (fs::path(data_dir) / "exports" / "knowledge" / name).string();
      }
    }
    const std::string payload = body.dump();
    // Synchronous JSON, as in the C ABI; status/cancel remain available on
    // other HTTP threads while this request runs. A RunResult with status
    // failed/paused is preserved rather than mislabeled as a successful run.
    send_loom(res, loom_knowledge_run(rctx(), payload.c_str(), nullptr, nullptr));
  });
  svr_.Get("/api/knowledge/status", [this](const httplib::Request& req, httplib::Response& res) {
    const auto task = req.get_param_value("task_id");
    send_loom(res, loom_knowledge_status(rctx(), task.empty() ? nullptr : task.c_str()));
  });
  svr_.Post("/api/knowledge/cancel", [this](const httplib::Request&, httplib::Response& res) {
    const int rc = loom_knowledge_cancel(rctx());
    if (rc == LOOM_E_NOT_FOUND) {
      send_error(res, "not_found", "no knowledge run in progress");
      return;
    }
    send_rc(res, rc, {{"ok", true}});
  });

  post_progress("/api/catalog/scan", loom_catalog_scan);
  post_progress("/api/catalog/score", loom_catalog_score);
  svr_.Post("/api/catalog/select", [this](const httplib::Request& req, httplib::Response& res) {
    json body;
    if (!object_body(req, res, body)) return;
    if (body.contains("run_id") && !body["run_id"].is_string()) {
      send_error(res, "invalid_argument", "run_id must be a string");
      return;
    }
    const auto run = body.value("run_id", std::string());
    send_loom(res, loom_catalog_select(rctx(), run.empty() ? nullptr : run.c_str()));
  });
  post_progress("/api/catalog/import", loom_catalog_import);
  post_json("/api/catalog/query", loom_catalog_query);
  post_json("/api/catalog/override", loom_catalog_override);
  svr_.Get("/api/catalog/units", [this](const httplib::Request& req, httplib::Response& res) {
    json query = json::object();
    for (const char* key : {"label", "project", "text", "run_id", "sort"}) {
      if (req.has_param(key)) query[key] = req.get_param_value(key);
    }
    for (const char* key : {"limit", "offset"}) {
      if (!req.has_param(key)) continue;
      int value = 0;
      if (!integer_param(req, res, key, value, std::string(key) == "limit" ? 1 : 0)) return;
      query[key] = value;
    }
    if (req.has_param("selected")) {
      const auto value = req.get_param_value("selected");
      if (value != "true" && value != "false" && value != "1" && value != "0") {
        send_error(res, "invalid_argument", "selected must be true, false, 1 or 0");
        return;
      }
      query["selected"] = value == "true" || value == "1";
    }
    const std::string payload = query.dump();
    send_loom(res, loom_catalog_query(rctx(), payload.c_str()));
  });
  svr_.Get(R"(/api/catalog/units/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    send_loom(res, loom_catalog_preview(rctx(), req.matches[1].str().c_str()));
  });
}


// ── Accounts (P1) ──────────────────────────────────────────────────────

namespace {
std::string cookie_header(const loom::accounts::CookieSettings& c, const std::string& value, std::int64_t max_age,
                          bool secure) {
  std::string h = c.name + "=" + value + "; Path=" + c.path + "; Max-Age=" + std::to_string(max_age) +
                  "; SameSite=" + c.same_site;
  if (c.http_only) h += "; HttpOnly";
  if (secure) h += "; Secure";
  return h;
}
bool wants_secure(const loom::accounts::CookieSettings& c, const httplib::Request& req) {
  if (c.secure == "always") return true;
  if (c.secure == "never") return false;
  return req.get_header_value("X-Forwarded-Proto") == "https";
}
std::string client_address(const httplib::Request& req, bool trust_forwarded) {
  if (trust_forwarded) {
    std::string f = req.get_header_value("X-Forwarded-For");
    f = f.substr(0, f.find(','));
    while (!f.empty() && f.front() == ' ') f.erase(f.begin());
    if (!f.empty()) return f;
  }
  return req.remote_addr;
}
// R20: every password hash is gated (hashing.max_concurrent); a request that found no free slot within
// hashing.wait_seconds is answered 503 + Retry-After instead of an error of its own.
bool reply_if_busy(const loom::Error& e, int retry_after, httplib::Response& res) {
  if (e.code != loom::Errc::Busy) return false;
  res.set_header("Retry-After", std::to_string(std::max(1, retry_after)));
  send_error(res, "busy", e.message, 503);
  return true;
}

json user_json(const loom::accounts::User& u) { return {{"id", u.id}, {"login", u.login}}; }
}  // namespace

void App::route_auth() {
  // Single-user mode: the endpoint exists so the web UI can tell the two modes apart.
  svr_.Get("/api/auth/me", [this](const httplib::Request&, httplib::Response& res) {
    json out = {{"accounts", accounts_ != nullptr}};
    if (accounts_) {
      out["authenticated"] = t_user.has_value();
      out["registration"] = accounts_->settings().registration;
      if (t_user) out["user"] = user_json(*t_user);
    }
    res.set_content(out.dump(), "application/json");
  });
  {
    auto start_session = [this](const httplib::Request& req, httplib::Response& res,
                                const loom::accounts::User& u, int status) {
      auto s = accounts_->create_session(u.id);
      if (!s) {
        send_error(res, "internal", "cannot create session", 500);
        return;
      }
      const auto& cs = accounts_->settings().cookie;
      res.set_header("Set-Cookie", cookie_header(cs, s->token, accounts_->settings().session_lifetime_seconds,
                                                 wants_secure(cs, req)));
      res.status = status;
      res.set_content(json{{"user", user_json(u)}, {"token", s->token}, {"expires", s->expires}}.dump(),
                      "application/json");
    };
    svr_.Post("/api/auth/login", [this, start_session](const httplib::Request& req, httplib::Response& res) {
      if (!accounts_) { send_error(res, "not_found", "accounts are not enabled", 404); return; }
      json body;
      if (!object_body(req, res, body)) return;
      const std::string who = body.value("login", std::string());
      if (accounts_->login_too_long(who)) {  // before any throttle bookkeeping
        send_error(res, "invalid_argument", "login is longer than " + std::to_string(accounts_->settings().login_max_length) + " characters", 400);
        return;
      }
      const std::string client = client_address(req, accounts_->settings().throttle.trust_forwarded_for);
      if (const auto wait = accounts_->throttle_wait(who, client); wait > 0) {
        res.set_header("Retry-After", std::to_string(wait));
        send_error(res, "rate_limited", "too many failed attempts; try again in " + std::to_string(wait) + " s", 429);
        return;
      }
      auto u = accounts_->verify_password(who, body.value("password", std::string()));
      if (u) accounts_->record_success(who);
      if (!u) {
        if (reply_if_busy(u.error(), static_cast<int>(accounts_->settings().hash_wait_seconds), res)) return;
        if (u.error().code == loom::Errc::Auth) accounts_->record_failure(who, client);
        if (u.error().code == loom::Errc::Auth) send_error(res, "auth", "invalid login or password", 401);
        else send_error(res, "internal", "login failed", 500);
        return;
      }
      start_session(req, res, *u, 200);
    });
    svr_.Post("/api/auth/register", [this, start_session](const httplib::Request& req, httplib::Response& res) {
      if (!accounts_) { send_error(res, "not_found", "accounts are not enabled", 404); return; }
      json body;
      if (!object_body(req, res, body)) return;
      const std::string mode = accounts_->settings().registration;
      if (mode == "closed") { send_error(res, "auth", "registration is closed", 403); return; }
      // Every attempt counts (it costs an Argon2id evaluation and, in invite mode, is a guess at a code), so the
      // login throttle applies to registration under its own key per client address.
      const std::string client = client_address(req, accounts_->settings().throttle.trust_forwarded_for);
      if (accounts_->login_too_long(body.value("login", std::string()))) {
        send_error(res, "invalid_argument", "login is longer than " + std::to_string(accounts_->settings().login_max_length) + " characters", 400);
        return;
      }
      const std::string key = "register@" + client;
      if (const auto wait = accounts_->throttle_wait(key, client); wait > 0) {
        res.set_header("Retry-After", std::to_string(wait));
        send_error(res, "rate_limited", "too many registration attempts; try again in " + std::to_string(wait) + " s", 429);
        return;
      }
      accounts_->record_failure(key, client);
      const std::string login = body.value("login", std::string());
      if (mode == "invite") {
        if (body.value("invite", std::string()).empty()) { send_error(res, "auth", "an invite code is required", 403); return; }
      }
      auto u = accounts_->create_user(login, body.value("password", std::string()),
                                      mode == "invite" ? body.value("invite", std::string()) : std::string());
      if (!u) {
        if (reply_if_busy(u.error(), static_cast<int>(accounts_->settings().hash_wait_seconds), res)) return;
        send_error(res, std::string(loom::errc_name(u.error().code)), u.error().message,
                   u.error().code == loom::Errc::Auth ? 403 : -1);
        return;
      }
      start_session(req, res, *u, 201);
    });
    svr_.Post("/api/auth/logout", [this](const httplib::Request& req, httplib::Response& res) {
      if (!accounts_) { send_error(res, "not_found", "accounts are not enabled", 404); return; }
      (void)accounts_->revoke_session(t_session_token);
      const auto& cs = accounts_->settings().cookie;
      res.set_header("Set-Cookie", cookie_header(cs, "", 0, wants_secure(cs, req)));
      res.set_content(json{{"ok", true}}.dump(), "application/json");
    });
    svr_.Post("/api/auth/password", [this, start_session](const httplib::Request& req, httplib::Response& res) {
      if (!accounts_ || !t_user) { send_error(res, "auth", "sign in required", 401); return; }
      json body;
      if (!object_body(req, res, body)) return;
      const std::string client = client_address(req, accounts_->settings().throttle.trust_forwarded_for);
      if (const auto wait = accounts_->throttle_wait(t_user->login, client); wait > 0) {
        res.set_header("Retry-After", std::to_string(wait));
        send_error(res, "rate_limited", "too many failed attempts; try again in " + std::to_string(wait) + " s", 429);
        return;
      }
      auto ok = accounts_->verify_password(t_user->login, body.value("current", std::string()));
      if (!ok) {
        if (reply_if_busy(ok.error(), static_cast<int>(accounts_->settings().hash_wait_seconds), res)) return;
        accounts_->record_failure(t_user->login, client);
        send_error(res, "auth", "current password is wrong", 401);
        return;
      }
      accounts_->record_success(t_user->login);
      auto st = accounts_->set_password(t_user->login, body.value("new", std::string()));
      if (!st) {
        if (reply_if_busy(st.error(), static_cast<int>(accounts_->settings().hash_wait_seconds), res)) return;
        send_error(res, std::string(loom::errc_name(st.error().code)), st.error().message);
        return;
      }
      (void)accounts_->revoke_user_sessions(t_user->id);  // every old session ends; this one is replaced
      start_session(req, res, *t_user, 200);
    });
  }
}

}  // namespace loom_server
