// app.h — loom-server: REST/SSE facade over the loom.h C ABI.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <httplib.h>

#include "loom/accounts.h"
#include "loom/loom.h"
#include "guest_guard.h"
#include "provider_runtime.h"

namespace loom_server {

struct ServerOptions {
  std::string host = "127.0.0.1";
  int port = 8787;
  std::string data_dir;      // empty -> loom's own default resolution
  std::string static_dir;    // empty -> static serving disabled
  std::string bearer_token;  // empty -> no auth required
  // Non-empty: accounts mode (P1). Users live in <accounts_root>/<accounts_file>, each with its own
  // data dir <accounts_root>/<users_subdir>/<id>/. Empty: single-user mode exactly as before.
  std::string accounts_root;
  // Largest request body of an ordinary (JSON) route, and of an upload (import, transcribe, a device's blob). Both are checked
  // against Content-Length before a body byte is read; chunked or unframed bodies are refused. Uploads are
  // streamed to disk, so 0 (= no limit) for them does not grow memory. 0 for JSON means no limit either.
  size_t max_body_bytes = 64u * 1024 * 1024;
  size_t max_upload_bytes = 0;
  // R23: after an upload passes max_upload_bytes, up to this many further bytes are read and discarded so the 413 reaches
  // the client (closing a socket with unread bytes sends a reset that can destroy the answer); past it the connection is
  // cut. 0 = drain without limit.
  size_t upload_drain_bytes = 64u * 1024 * 1024;
  // An idle event feed (SSE) writes a comment this often so a client that went away is noticed and its thread freed (0 = never).
  size_t sse_keepalive_ms = 5000;
  // Threads that serve requests (0 = the library's own count, at least 8). Every open page holds one for its event feed (P14 4) and
  // a closed one holds it until the next probe, so the preset leaves room for many pages.
  size_t worker_threads = 64;
  // Seconds an idle persistent (keep-alive) connection is kept open; the library advertises it on every answer ("Keep-Alive: timeout=N").
  // A client that cannot run its event loop for longer than this (blocked, suspended, throttled in the background) finds the connection
  // closed under it and cannot tell until it writes: a request that is not safe to repeat then fails ("other side closed"). Every idle
  // connection holds one of worker_threads while it waits, so a long preset is not free. 0 = no persistent connections: every answer
  // closes its connection. The same time also bounds how long a NEW connection may wait before it sends its first request (the library
  // checks it before every request, the first one included), so a very small value resets slow-starting clients too. main.cpp accepts
  // whole seconds only and clamps to 2147483647: the library counts the idle time in 64-bit nanoseconds.
  size_t keep_alive_timeout_seconds = 5;
  // Seconds a request may go without a byte arriving while its headers or body are read; past it the library gives up on the request.
  // An upload whose client stalls that long before its body ends is answered 408 with that reason (not "malformed"). A long preset
  // lets a slow or briefly suspended client finish a large upload; a short one frees a thread held by a client that went silent
  // sooner. Whole seconds; 0 = the library's own value (CPPHTTPLIB_READ_TIMEOUT_SECOND, 5 s).
  size_t read_timeout_seconds = 30;
  // R20: chunked / unframed request bodies are refused (411) by default. When true they are accepted on the two
  // upload routes only (import, transcribe, a device's blob), streamed to disk and counted against max_upload_bytes; every
  // other route still needs a Content-Length because its body is held in memory.
  bool allow_chunked_uploads = false;
};

// Owns the LoomContext and the httplib::Server, and wires every /api/*
// route to existing native operations. Ordinary routes use loom_* C ABI calls;
// server-owned UI bridges borrow the same static Runtime through its actual
// context declaration until these operations have an exported embedded ABI.
// The usage-policy adapter links the public static-kernel dispatcher.
class App {
 public:
  explicit App(ServerOptions opts);
  ~App();

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // Returns false if the context or the listen socket could not be set up.
  bool init();
  // Blocks until stop() is called (or the socket fails).
  void run();
  // Safe to call from a signal handler's async-safe wrapper or another
  // thread; makes run() return.
  void stop();

  // The context of the request being handled (accounts mode: the signed-in user's), else the single one.
  LoomContext* rctx() const;
  LoomContext* ctx() const { return rctx(); }
  bool accounts_mode() const { return accounts_ != nullptr; }

 private:
  // The read timeout the library applies (opts_.read_timeout_seconds, or its own 5 s when that is 0).
  size_t effective_read_timeout_s() const { return opts_.read_timeout_seconds ? opts_.read_timeout_seconds : 5; }
  void register_routes();
  void register_middleware();
  // POST /api/auth/{login,logout,register,password}, GET /api/auth/me.
  void route_auth();
  LoomContext* context_for_user(const std::string& user_id);
  // R103 B1: a guest's runtime over its own working directory, with the same R18 isolation as an account and the
  // operator's free-provider key handed over in memory only (credential_overlay).
  LoomContext* context_for_guest(const std::string& guest_id);
  // Removes guests idle longer than guests.workdir.idle_ttl_seconds (runtime closed, directory deleted); runs at
  // most every guests.sweep_interval_seconds, on requests.
  void sweep_guests_if_due();
  nlohmann::json guest_source(const std::string& guest_id);
  // R103 B3: the free/paid source for a new guest runtime (overlay receives the key or access token, memory only).
  nlohmann::json choose_guest_source(std::map<std::string, std::string>& overlay);
  // R103 B3: before a guest's model request: refresh a short-lived token, or switch the guest to the next source when
  // its provider is exhausted, over budget or without a token (§4.4, told in model_source.switched).
  void prepare_guest_source(const std::string& guest_id);
  // R103 B3: after a guest's answer: spend from usage × price, exhaustion markers → cool-down.
  void settle_guest_answer(const std::string& provider, const nlohmann::json& result);
  // R103 B2: admission of one guest request (sizes, working-folder ceiling, buckets); true = answered (refused).
  bool refuse_guest_request(const httplib::Request& req, httplib::Response& res);
  // R103 B2: a 429 (or other status) with retry_after, the mechanism and words in the request's language.
  void send_limit(const httplib::Request& req, httplib::Response& res, const std::string& mechanism, std::int64_t retry_after,
                  int status = 429, const std::string& limit = {});

  // Route groups (implemented in app.cpp), split only for readability -
  // all register handlers on svr_.
  void route_conversations();
  void route_messages();
  void route_search();
  void route_chat();
  void route_models_providers();
  void route_config_secrets();
  void route_graph_context();
  void route_semantic();
  void route_memory();
  void route_import_export();
  void route_provenance_events_tasks();
  void route_logs_misc();
  // Archive Intelligence + artifacts: POST /api/archive/run (SSE progress),
  // POST /api/archive/cancel, GET /api/archive/status, GET /api/artifacts,
  // GET /api/artifacts/{id}[?content=1], GET /api/artifacts/{id}/raw.
  void route_archive_placeholder();
  // Knowledge, catalog and goal-directed context: the same C ABI models
  // used by the CLI, with no HTTP-specific evidence or inference semantics.
  void route_knowledge();

  ServerOptions opts_;
  LoomContext* ctx_ = nullptr;  // single-user mode only
  std::unique_ptr<loom::accounts::AccountStore> accounts_;
  std::mutex pool_mu_;
  std::unordered_map<std::string, LoomContext*> user_ctx_;  // lazily opened, kept for the process lifetime
  std::unordered_map<std::string, LoomContext*> guest_ctx_;  // closed by the guest sweep
  std::unordered_map<std::string, nlohmann::json> guest_source_;  // which free provider/model a guest runtime got, and why
  std::atomic<std::int64_t> next_guest_sweep_{0};
  GuestGuard guard_;  // R103 B2: guest protection (buckets, proof of work, rejection counters)
  // R103 B3: the guests' paid/service-account sources: access tokens (memory only), spend per day, exhausted providers.
  MetadataTokens tokens_;
  SpendLedger spend_;
  ProviderCooldowns cooldowns_;
  std::mutex budget_note_mu_;
  std::map<std::string, std::string> budget_note_day_;  // provider -> day the advisory was last written to the log
  httplib::Server svr_;
  std::atomic<bool> running_{false};
};

}  // namespace loom_server
