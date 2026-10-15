// The MCP kind of the tool-source adapter: JSON-RPC 2.0 over stdio (a spawned process, newline-delimited messages) or Streamable
// HTTP (POST; the answer is JSON or an SSE stream). Every operation opens its own session: initialize, notifications/initialized,
// the call, close. Server-initiated requests (sampling, roots ...) are refused: a tool source never gets to ask the model or the
// files of the user. Method names, protocol version, headers and timeouts are data (profile tool_sources, kinds.mcp).
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

#if !defined(_WIN32) && !defined(__ANDROID__)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#define LOOM_TOOL_SOURCE_STDIO 1
extern char** environ;
#endif

#include "loom/config.h"
#include "loom/log.h"
#include "loom/net/http.h"
#include "loom/net/sse.h"
#include "loom/runtime.h"
#include "loom/tool_sources.h"
#include "internal.h"



namespace loom::tool_sources {
namespace {

constexpr const char* kLog = "loom.tool_sources";

using Clock = std::chrono::steady_clock;

struct Params {
  std::string protocol_version = "2025-06-18";
  Json client_info = Json::object();
  std::string m_initialize = "initialize", m_initialized = "notifications/initialized", m_list = "tools/list", m_call = "tools/call";
  std::string accept = "application/json, text/event-stream", session_header = "Mcp-Session-Id", protocol_header = "MCP-Protocol-Version";
  std::string auth_header = "Authorization", auth_scheme = "Bearer";
  long timeout_ms = 0;
  long stop_grace_ms = 1500;
};

Params params_from(const Json& kind) {
  Params p;
  auto s = [&](const Json& o, const char* k, std::string& dst) {
    const std::string v = json::get_string(o, k);
    if (!v.empty()) dst = v;
  };
  s(kind, "protocol_version", p.protocol_version);
  if (const Json* ci = json::find(kind, "client_info"); ci && ci->is_object()) p.client_info = *ci;
  if (const Json* m = json::find(kind, "methods"); m && m->is_object()) {
    s(*m, "initialize", p.m_initialize);
    s(*m, "initialized", p.m_initialized);
    s(*m, "list", p.m_list);
    s(*m, "call", p.m_call);
  }
  if (const Json* h = json::find(kind, "http"); h && h->is_object()) {
    s(*h, "accept", p.accept);
    s(*h, "session_header", p.session_header);
    s(*h, "protocol_header", p.protocol_header);
    s(*h, "auth_header", p.auth_header);
    s(*h, "auth_scheme", p.auth_scheme);
  }
  if (const Json* t = json::find(kind, "timeout_ms"); t && t->is_number()) p.timeout_ms = t->get<long>();
  if (const Json* g = json::find(kind, "stop_grace_ms"); g && g->is_number()) p.stop_grace_ms = g->get<long>();
  return p;
}

// ── wires ────────────────────────────────────────────────────────────────────────────────────────

class Wire {
 public:
  virtual ~Wire() = default;
  // Sends a request and returns the JSON-RPC message that answers `id`.
  virtual Result<Json> request(const Json& message, std::int64_t id, const CancelToken* cancel) = 0;
  virtual Status notify(const Json& message, const CancelToken* cancel) = 0;
  virtual void set_protocol(const std::string& /*version*/) {}
  virtual void close() {}
};

bool is_response_to(const Json& m, std::int64_t id) {
  const Json* i = json::find(m, "id");
  return m.is_object() && i && i->is_number_integer() && i->get<std::int64_t>() == id && (m.contains("result") || m.contains("error"));
}

Result<Json> unwrap_rpc(const Json& m) {
  if (const Json* e = json::find(m, "error"); e && e->is_object())
    return Error(Errc::Http, "tool source error " + std::to_string(e->value("code", 0)) + ": " + json::get_string(*e, "message"));
  const Json* r = json::find(m, "result");
  return r ? *r : Json::object();
}

#ifdef LOOM_TOOL_SOURCE_STDIO
class StdioWire final : public Wire {
 public:
  StdioWire(std::vector<std::string> argv, std::vector<std::pair<std::string, std::string>> env, long timeout_ms, long grace_ms)
      : argv_(std::move(argv)), env_(std::move(env)), timeout_ms_(timeout_ms), grace_ms_(grace_ms) {}
  ~StdioWire() override { close(); }

  Status start() {
    if (argv_.empty()) return Error(Errc::InvalidArgument, "stdio tool source has no command");
    int in[2], out[2];
    if (::pipe(in) != 0) return Error(Errc::Io, "pipe failed");
    if (::pipe(out) != 0) { ::close(in[0]); ::close(in[1]); return Error(Errc::Io, "pipe failed"); }
    std::vector<char*> av;
    for (auto& a : argv_) av.push_back(a.data());
    av.push_back(nullptr);
    std::vector<std::string> envs;
    for (char** e = environ; e && *e; ++e) {
      const std::string kv = *e;
      const std::string name = kv.substr(0, kv.find('='));
      bool overridden = false;
      for (const auto& o : env_) overridden = overridden || o.first == name;
      if (!overridden) envs.push_back(kv);
    }
    for (const auto& o : env_) envs.push_back(o.first + "=" + o.second);
    std::vector<char*> ev;
    for (auto& e : envs) ev.push_back(e.data());
    ev.push_back(nullptr);
    const pid_t pid = ::fork();
    if (pid < 0) return Error(Errc::Io, "fork failed");
    if (pid == 0) {
      ::dup2(in[0], 0);
      ::dup2(out[1], 1);
      const int devnull = ::open("/dev/null", O_WRONLY);
      if (devnull >= 0) ::dup2(devnull, 2);
      for (int fd = 3; fd < 256; ++fd) ::close(fd);
      ::execvpe(av[0], av.data(), ev.data());
      ::_exit(127);
    }
    ::close(in[0]);
    ::close(out[1]);
    pid_ = pid;
    wfd_ = in[1];
    rfd_ = out[0];
    ::signal(SIGPIPE, SIG_IGN);
    return Status{};
  }

  Result<Json> request(const Json& message, std::int64_t id, const CancelToken* cancel) override {
    LOOM_TRY(write_line(message));
    const auto deadline = timeout_ms_ > 0 ? Clock::now() + std::chrono::milliseconds(timeout_ms_) : Clock::time_point::max();
    while (true) {
      LOOM_TRY_ASSIGN(std::string line, read_line(deadline, cancel));
      auto parsed = json::parse(line);
      if (!parsed || !parsed->is_object()) continue;  // not JSON-RPC (a log line on stdout): ignored
      if (is_response_to(*parsed, id)) return std::move(*parsed);
      // A request from the server: never served (no sampling, no roots); ping is answered.
      if (parsed->contains("method") && parsed->contains("id")) {
        Json reply{{"jsonrpc", "2.0"}, {"id", (*parsed)["id"]}};
        if (json::get_string(*parsed, "method") == "ping") reply["result"] = Json::object();
        else reply["error"] = Json{{"code", -32601}, {"message", "method not supported by this client"}};
        (void)write_line(reply);
      }
    }
  }
  Status notify(const Json& message, const CancelToken*) override { return write_line(message); }

  void close() override {
    if (wfd_ >= 0) { ::close(wfd_); wfd_ = -1; }
    if (pid_ > 0) {
      const auto until = Clock::now() + std::chrono::milliseconds(grace_ms_);
      int status = 0;
      bool gone = false;
      while (Clock::now() < until) {
        if (::waitpid(pid_, &status, WNOHANG) == pid_) { gone = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (!gone) {
        ::kill(pid_, SIGTERM);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (::waitpid(pid_, &status, WNOHANG) != pid_) {
          ::kill(pid_, SIGKILL);
          ::waitpid(pid_, &status, 0);
        }
      }
      pid_ = -1;
    }
    if (rfd_ >= 0) { ::close(rfd_); rfd_ = -1; }
  }

 private:
  Status write_line(const Json& m) {
    std::string s = json::dump(m) + "\n";
    std::size_t off = 0;
    while (off < s.size()) {
      const ssize_t n = ::write(wfd_, s.data() + off, s.size() - off);
      if (n < 0) return Error(Errc::Io, "the tool source closed its input");
      off += static_cast<std::size_t>(n);
    }
    return Status{};
  }
  Result<std::string> read_line(Clock::time_point deadline, const CancelToken* cancel) {
    while (true) {
      if (auto nl = buf_.find('\n'); nl != std::string::npos) {
        std::string line = buf_.substr(0, nl);
        buf_.erase(0, nl + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        return line;
      }
      if (cancel && cancel->cancelled()) return Error(Errc::Cancelled, "cancelled");
      if (Clock::now() >= deadline) return Error(Errc::Timeout, "the tool source did not answer in time");
      pollfd p{rfd_, POLLIN, 0};
      const int r = ::poll(&p, 1, 100);
      if (r < 0) return Error(Errc::Io, "poll failed");
      if (r == 0) continue;
      char tmp[8192];
      const ssize_t n = ::read(rfd_, tmp, sizeof tmp);
      if (n <= 0) return Error(Errc::Io, "the tool source closed its output before answering");
      buf_.append(tmp, static_cast<std::size_t>(n));
    }
  }

  std::vector<std::string> argv_;
  std::vector<std::pair<std::string, std::string>> env_;
  long timeout_ms_, grace_ms_;
  pid_t pid_ = -1;
  int wfd_ = -1, rfd_ = -1;
  std::string buf_;
};
#endif

class HttpWire final : public Wire {
 public:
  HttpWire(Runtime& rt, std::string url, std::string auth_name, std::string auth_value, const Params& p)
      : rt_(rt), url_(std::move(url)), auth_name_(std::move(auth_name)), auth_value_(std::move(auth_value)), p_(p) {}
  ~HttpWire() override { close(); }

  void set_protocol(const std::string& v) override { protocol_ = v; }

  Result<Json> request(const Json& message, std::int64_t id, const CancelToken* cancel) override {
    LOOM_TRY_ASSIGN(auto resp, post(message, cancel));
    if (!resp.ok()) return Error(Errc::Http, "tool source answered HTTP " + std::to_string(resp.status));
    if (const std::string sid = resp.header(p_.session_header); !sid.empty()) session_ = sid;
    const std::string type = resp.header("Content-Type");
    if (type.find("text/event-stream") != std::string::npos) {
      Json found;
      bool have = false;
      net::SseParser parser;
      auto cb = [&](const net::SseEvent& ev) {
        if (have) return;
        auto m = json::parse(ev.data);
        if (m && is_response_to(*m, id)) { found = std::move(*m); have = true; }
      };
      parser.feed(resp.body, cb);
      parser.finish(cb);
      if (!have) return Error(Errc::Parse, "the tool source stream ended without an answer");
      return found;
    }
    auto m = json::parse(resp.body);
    if (!m) return Error(Errc::Parse, "the tool source did not answer with JSON");
    if (m->is_array())  // a batch: pick our answer
      for (const auto& e : *m)
        if (is_response_to(e, id)) return e;
    if (!is_response_to(*m, id)) return Error(Errc::Parse, "the tool source answered another request");
    return std::move(*m);
  }
  Status notify(const Json& message, const CancelToken* cancel) override {
    LOOM_TRY_ASSIGN(auto resp, post(message, cancel));
    if (!resp.ok()) return Error(Errc::Http, "tool source answered HTTP " + std::to_string(resp.status));
    return Status{};
  }
  void close() override {
    if (session_.empty()) return;
    net::HttpRequest r;
    r.method = "DELETE";
    r.url = url_;
    r.timeout_ms = 5000;
    add_headers(r);
    (void)rt_.http().send(r);  // best effort: servers may answer 405
    session_.clear();
  }

 private:
  void add_headers(net::HttpRequest& r) const {
    r.headers.emplace_back("Content-Type", "application/json");
    r.headers.emplace_back("Accept", p_.accept);
    if (!protocol_.empty()) r.headers.emplace_back(p_.protocol_header, protocol_);
    if (!session_.empty()) r.headers.emplace_back(p_.session_header, session_);
    if (!auth_value_.empty()) r.headers.emplace_back(auth_name_, auth_value_);
  }
  Result<net::HttpResponse> post(const Json& message, const CancelToken* cancel) {
    net::HttpRequest r;
    r.method = "POST";
    r.url = url_;
    r.body = json::dump(message);
    if (p_.timeout_ms > 0) r.timeout_ms = static_cast<int>(p_.timeout_ms);
    add_headers(r);
    return rt_.http().send(r, nullptr, cancel);
  }
  Runtime& rt_;
  std::string url_, auth_name_, auth_value_;
  Params p_;
  std::string protocol_, session_;
};

// ── the source ───────────────────────────────────────────────────────────────────────────────────

class McpSource final : public ToolSource {
 public:
  McpSource(std::string id, Json cfg, Runtime& rt, Params p) : id_(std::move(id)), cfg_(std::move(cfg)), rt_(rt), p_(std::move(p)) {}
  std::string kind() const override { return "mcp"; }

  Result<std::vector<ToolDecl>> list_tools(const CancelToken* cancel) override {
    LOOM_TRY_ASSIGN(auto wire, open());
    LOOM_TRY(handshake(*wire, cancel));
    std::vector<ToolDecl> out;
    std::string cursor;
    for (int page = 0; page < 10000; ++page) {
      Json params = Json::object();
      if (!cursor.empty()) params["cursor"] = cursor;
      LOOM_TRY_ASSIGN(Json result, rpc(*wire, p_.m_list, params, cancel));
      if (const Json* tools = json::find(result, "tools"); tools && tools->is_array())
        for (const auto& t : *tools) {
          ToolDecl d;
          d.name = json::get_string(t, "name");
          if (d.name.empty()) continue;
          d.title = json::get_string(t, "title");
          d.description = json::get_string(t, "description");
          if (const Json* s = json::find(t, "inputSchema"); s && s->is_object()) d.input_schema = *s;
          if (const Json* a = json::find(t, "annotations"); a && a->is_object()) d.annotations = *a;
          out.push_back(std::move(d));
        }
      cursor = json::get_string(result, "nextCursor");
      if (cursor.empty()) break;
    }
    return out;
  }

  Result<ToolOutput> call_tool(const std::string& name, const Json& arguments, const CancelToken* cancel) override {
    LOOM_TRY_ASSIGN(auto wire, open());
    LOOM_TRY(handshake(*wire, cancel));
    LOOM_TRY_ASSIGN(Json result, rpc(*wire, p_.m_call, Json{{"name", name}, {"arguments", arguments.is_object() ? arguments : Json::object()}}, cancel));
    ToolOutput o;
    o.is_error = json::get_bool(result, "isError", false);
    if (const Json* c = json::find(result, "content"); c && c->is_array()) {
      o.content = *c;
      for (const auto& b : *c)
        if (json::get_string(b, "type") == "text") o.text += (o.text.empty() ? "" : "\n") + json::get_string(b, "text");
    }
    if (const Json* s = json::find(result, "structuredContent")) o.structured = *s;
    return o;
  }

 private:
  Result<std::unique_ptr<Wire>> open() {
    const std::string transport = json::get_string(cfg_, "transport");
    if (transport == "stdio") {
#ifdef LOOM_TOOL_SOURCE_STDIO
      std::vector<std::string> argv;
      const Json* cmd = json::find(cfg_, "command");
      if (cmd && cmd->is_string()) argv.push_back(cmd->get<std::string>());
      else if (cmd && cmd->is_array())
        for (const auto& a : *cmd) if (a.is_string()) argv.push_back(a.get<std::string>());
      if (const Json* args = json::find(cfg_, "args"); args && args->is_array())
        for (const auto& a : *args) if (a.is_string()) argv.push_back(a.get<std::string>());
      std::vector<std::pair<std::string, std::string>> env;
      if (const Json* e = json::find(cfg_, "env"); e && e->is_object())
        for (auto it = e->begin(); it != e->end(); ++it) {
          if (it.value().is_string()) env.emplace_back(it.key(), it.value().get<std::string>());
          else if (it.value().is_object()) {
            if (const std::string ref = json::get_string(it.value(), "secret"); !ref.empty()) {
              if (!rt_.secrets().has(ref)) return Error(Errc::Auth, "tool source '" + id_ + "': the vault has no key '" + ref + "' (set it in the settings)");
              env.emplace_back(it.key(), rt_.secrets().get_string(ref));
            } else env.emplace_back(it.key(), json::get_string(it.value(), "value"));
          }
        }
      auto wire = std::make_unique<StdioWire>(std::move(argv), std::move(env), p_.timeout_ms, p_.stop_grace_ms);
      LOOM_TRY(wire->start());
      return std::unique_ptr<Wire>(std::move(wire));
#else
      return Error(Errc::Unsupported, "the stdio transport needs a process: not available on this platform (use http)");
#endif
    }
    if (transport == "http") {
      const std::string url = json::get_string(cfg_, "url");
      if (url.empty()) return Error(Errc::InvalidArgument, "tool source '" + id_ + "' has no url");
      std::string name, value;
      if (const Json* a = json::find(cfg_, "access"); a && a->is_object()) {
        const std::string ref = json::get_string(*a, "secret");
        if (!ref.empty()) {
          if (!rt_.secrets().has(ref)) return Error(Errc::Auth, "tool source '" + id_ + "': the vault has no key '" + ref + "' (set it in the settings)");
          name = json::get_string(*a, "header", p_.auth_header);
          const std::string scheme = json::get_string(*a, "scheme", name == p_.auth_header ? p_.auth_scheme : "");
          value = (scheme.empty() ? "" : scheme + " ") + rt_.secrets().get_string(ref);
        }
      }
      return std::unique_ptr<Wire>(std::make_unique<HttpWire>(rt_, url, name, value, p_));
    }
    return Error(Errc::Unsupported, "tool source '" + id_ + "': unknown transport '" + transport + "' (stdio, http)");
  }

  Result<Json> rpc(Wire& wire, const std::string& method, const Json& params, const CancelToken* cancel) {
    const std::int64_t id = ++next_id_;
    Json m{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
    if (!params.is_null()) m["params"] = params;
    LOOM_TRY_ASSIGN(Json answer, wire.request(m, id, cancel));
    return unwrap_rpc(answer);
  }
  Status handshake(Wire& wire, const CancelToken* cancel) {
    Json params{{"protocolVersion", p_.protocol_version}, {"capabilities", Json::object()}, {"clientInfo", p_.client_info}};
    LOOM_TRY_ASSIGN(Json init, rpc(wire, p_.m_initialize, params, cancel));
    const std::string negotiated = json::get_string(init, "protocolVersion", p_.protocol_version);
    wire.set_protocol(negotiated);
    return wire.notify(Json{{"jsonrpc", "2.0"}, {"method", p_.m_initialized}}, cancel);
  }

  std::string id_;
  Json cfg_;
  Runtime& rt_;
  Params p_;
  std::int64_t next_id_ = 0;
};

}  // namespace

Result<std::unique_ptr<ToolSource>> make_mcp(const std::string& id, const Json& cfg, const ToolSourceEnv& env) {
  if (!env.rt) return Error(Errc::Internal, "tool source environment has no runtime");
  LOOM_TRY_ASSIGN(Json values, profile_values(*env.rt));
  const Json* kinds = json::find(values, "kinds");
  const Json* mcp = kinds ? json::find(*kinds, "mcp") : nullptr;
  if (!mcp) return Error(Errc::NotFound, "profile tool_sources has no kind 'mcp'");
  const std::string transport = json::get_string(cfg, "transport");
  bool known = false;
  if (const Json* t = json::find(*mcp, "transports"); t && t->is_array())
    for (const auto& e : *t) known = known || (e.is_string() && e.get<std::string>() == transport);
  if (!known) return Error(Errc::Unsupported, "tool source '" + id + "': transport '" + transport + "' is not one of the mcp transports");
  log::debug(kLog, "mcp source {} ({})", id, transport);
  return std::unique_ptr<ToolSource>(std::make_unique<McpSource>(id, cfg, *env.rt, params_from(*mcp)));
}

}  // namespace loom::tool_sources
