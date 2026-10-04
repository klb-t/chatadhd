// app.h — loom-server: REST/SSE facade over the loom.h C ABI.
#pragma once

#include <atomic>
#include <string>

#include <httplib.h>

#include "loom/loom.h"

namespace loom_server {

struct ServerOptions {
  std::string host = "127.0.0.1";
  int port = 8787;
  std::string data_dir;      // empty -> loom's own default resolution
  std::string static_dir;    // empty -> static serving disabled
  std::string bearer_token;  // empty -> no auth required
  size_t max_body_bytes = 64u * 1024 * 1024;
};

// Owns the LoomContext and the httplib::Server, and wires every /api/*
// route to one or more loom_* C ABI calls. Nothing in this class (or in
// app.cpp) touches Loom internals directly — only public JSON C ABI headers.
// The optional usage-policy adapter links the public static-kernel dispatcher.
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

  LoomContext* ctx() const { return ctx_; }

 private:
  void register_routes();
  void register_middleware();

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
  LoomContext* ctx_ = nullptr;
  httplib::Server svr_;
  std::atomic<bool> running_{false};
};

}  // namespace loom_server
