// loom-server entry point: argument/env parsing, signal handling, startup.
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <csignal>

#include "app.h"

namespace {

std::atomic<loom_server::App*> g_app{nullptr};

void on_signal(int /*sig*/) {
  if (loom_server::App* app = g_app.load()) app->stop();
}

std::string env_or(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  return (v && *v) ? std::string(v) : fallback;
}

void print_usage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " [--host H] [--port P] [--data-dir DIR] [--static-dir DIR] [--token TOKEN]\n"
               "Environment: LOOM_SERVER_HOST, LOOM_SERVER_PORT, CHATADHD_DATA,\n"
               "             LOOM_SERVER_STATIC_DIR, LOOM_SERVER_TOKEN\n";
}

}  // namespace

int main(int argc, char** argv) {
  loom_server::ServerOptions opts;
  opts.host = env_or("LOOM_SERVER_HOST", "127.0.0.1");
  opts.port = std::atoi(env_or("LOOM_SERVER_PORT", "8787").c_str());
  opts.data_dir = env_or("CHATADHD_DATA", "");
  opts.static_dir = env_or("LOOM_SERVER_STATIC_DIR", "");
  opts.bearer_token = env_or("LOOM_SERVER_TOKEN", "");

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto next = [&](const char* flag) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << flag << " requires a value\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "--host") {
      opts.host = next("--host");
    } else if (arg == "--port") {
      opts.port = std::atoi(next("--port"));
    } else if (arg == "--data-dir") {
      opts.data_dir = next("--data-dir");
    } else if (arg == "--static-dir") {
      opts.static_dir = next("--static-dir");
    } else if (arg == "--token") {
      opts.bearer_token = next("--token");
    } else if (arg == "-h" || arg == "--help") {
      print_usage(argv[0]);
      return 0;
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      print_usage(argv[0]);
      return 2;
    }
  }

  loom_server::App app(opts);
  if (!app.init()) {
    std::cerr << "loom-server: failed to initialize (bad data dir or already in use?)\n";
    return 1;
  }
  g_app.store(&app);
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  std::cerr << "loom-server: listening on " << opts.host << ":" << opts.port;
  if (!opts.static_dir.empty()) std::cerr << " (static: " << opts.static_dir << ")";
  if (!opts.bearer_token.empty()) std::cerr << " (bearer token required)";
  std::cerr << std::endl;

  app.run();
  g_app.store(nullptr);
  std::cerr << "loom-server: stopped\n";
  return 0;
}
