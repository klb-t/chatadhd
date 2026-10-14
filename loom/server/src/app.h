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
  // Largest request body of an ordinary (JSON) route, and of an upload (import, transcribe). Both are checked
  // against Content-Length before a body byte is read; chunked or unframed bodies are refused. Uploads are
  // streamed to disk, so 0 (= no limit) for them does not grow memory. 0 for JSON means no limit either.
  size_t max_body_bytes = 64u * 1024 * 1024;
  size_t max_upload_bytes = 0;
  // R20: chunked / unframed request bodies are refused (411) by default. When true they are accepted on the two
  // upload routes only (import, transcribe), streamed to disk and counted against max_upload_bytes; every
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
  void register_routes();
  void register_middleware();
  // POST /api/auth/{login,logout,register,password}, GET /api/auth/me.
  void route_auth();
  LoomContext* context_for_user(const std::string& user_id);

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
  httplib::Server svr_;
  std::atomic<bool> running_{false};
};

}  // namespace loom_server
