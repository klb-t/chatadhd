// loom-server entry point: argument/env parsing, signal handling, startup.
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <csignal>
#include <termios.h>
#include <unistd.h>

#include "loom/accounts.h"
#include "loom/loom.h"

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
            << " [--version] [--host H] [--port P] [--data-dir DIR] [--static-dir DIR] [--token TOKEN]\n"
               "       [--accounts-root DIR] [--allow-unauthenticated-public]\n"
               "       [--max-body-bytes N (JSON routes, 0 = no limit)] [--max-upload-bytes N (import/transcribe, 0 = no limit)]\n"
               "       [--sse-keepalive-ms N (idle event feeds probe their client this often, 0 = never; preset 15000)]\n"
               "       [--allow-chunked-uploads (accept chunked/unframed import/transcribe bodies, streamed to disk)]\n"
               "       " << argv0 << " user add|passwd|disable|enable|invite|list --accounts-root DIR [LOGIN]\n"
               "         (password: --password-stdin, or prompted on a terminal)\n"
               "Environment: LOOM_SERVER_HOST, LOOM_SERVER_PORT, CHATADHD_DATA,\n"
               "             LOOM_SERVER_STATIC_DIR, LOOM_SERVER_TOKEN, LOOM_ACCOUNTS_ROOT\n";
}

bool is_loopback(const std::string& host) {
  return host == "localhost" || host == "::1" || host.rfind("127.", 0) == 0;
}

std::string read_password(bool from_stdin, const char* prompt) {
  std::string pw;
  if (!from_stdin && isatty(STDIN_FILENO)) {
    termios old{};
    tcgetattr(STDIN_FILENO, &old);
    termios quiet = old;
    quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
    std::cerr << prompt << std::flush;
    std::getline(std::cin, pw);
    std::string again;
    std::cerr << "\nRepeat: " << std::flush;
    std::getline(std::cin, again);
    std::cerr << "\n";
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    if (pw != again) {
      std::cerr << "passwords differ\n";
      std::exit(1);
    }
  } else {
    std::getline(std::cin, pw);
  }
  return pw;
}

// `loom-server user ...`: operator-side account management (works on the files; the server need not run).
int user_command(int argc, char** argv, std::string root) {
  std::string sub = argc > 2 ? argv[2] : "";
  std::string login;
  bool from_stdin = false;
  for (int i = 3; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--accounts-root" && i + 1 < argc) root = argv[++i];
    else if (a == "--password-stdin") from_stdin = true;
    else if (login.empty() && a.rfind("--", 0) != 0) login = a;
    else { std::cerr << "unknown argument: " << a << "\n"; return 2; }
  }
  if (root.empty()) { std::cerr << "--accounts-root DIR (or LOOM_ACCOUNTS_ROOT) is required\n"; return 2; }
  auto settings = loom::accounts::load_settings(root);
  if (!settings) { std::cerr << "settings: " << settings.error().message << "\n"; return 1; }
  auto store = loom::accounts::AccountStore::open(root, std::move(*settings));
  if (!store) { std::cerr << "store: " << store.error().message << "\n"; return 1; }
  auto fail = [](const loom::Error& e) { std::cerr << "error: " << e.message << "\n"; return 1; };
  if (sub == "list") {
    auto users = (*store)->list_users();
    if (!users) return fail(users.error());
    for (const auto& u : *users) std::cout << u.id << "\t" << u.login << "\t" << (u.disabled ? "disabled" : "active") << "\n";
    return 0;
  }
  if (sub == "invite") {
    auto code = (*store)->create_invite();
    if (!code) return fail(code.error());
    std::cout << *code << "\n";
    return 0;
  }
  if (login.empty()) { std::cerr << "user " << sub << " needs a LOGIN\n"; return 2; }
  if (sub == "add" || sub == "passwd") {
    std::string pw = read_password(from_stdin, "Password: ");
    if (sub == "add") {
      auto u = (*store)->create_user(login, pw);
      if (!u) return fail(u.error());
      std::cout << u->id << "\t" << u->login << "\t" << (*store)->user_dir(u->id).string() << "\n";
      return 0;
    }
    auto st = (*store)->set_password(login, pw);
    if (!st) return fail(st.error());
    auto u = (*store)->find_user(login);
    if (u && *u) (void)(*store)->revoke_user_sessions((*u)->id);
    return 0;
  }
  if (sub == "disable" || sub == "enable") {
    auto st = (*store)->set_disabled(login, sub == "disable");
    if (!st) return fail(st.error());
    return 0;
  }
  std::cerr << "unknown user command: " << sub << "\n";
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "user") return user_command(argc, argv, env_or("LOOM_ACCOUNTS_ROOT", ""));
  loom_server::ServerOptions opts;
  opts.host = env_or("LOOM_SERVER_HOST", "127.0.0.1");
  opts.port = std::atoi(env_or("LOOM_SERVER_PORT", "8787").c_str());
  opts.data_dir = env_or("CHATADHD_DATA", "");
  opts.static_dir = env_or("LOOM_SERVER_STATIC_DIR", "");
  opts.bearer_token = env_or("LOOM_SERVER_TOKEN", "");
  opts.accounts_root = env_or("LOOM_ACCOUNTS_ROOT", "");
  if (auto v = env_or("LOOM_SERVER_MAX_BODY_BYTES", ""); !v.empty()) opts.max_body_bytes = std::strtoull(v.c_str(), nullptr, 10);
  if (auto v = env_or("LOOM_SERVER_MAX_UPLOAD_BYTES", ""); !v.empty()) opts.max_upload_bytes = std::strtoull(v.c_str(), nullptr, 10);
  if (auto v = env_or("LOOM_SERVER_SSE_KEEPALIVE_MS", ""); !v.empty()) opts.sse_keepalive_ms = std::strtoull(v.c_str(), nullptr, 10);
  if (auto v = env_or("LOOM_SERVER_ALLOW_CHUNKED_UPLOADS", ""); !v.empty()) opts.allow_chunked_uploads = v != "0" && v != "false";
  bool allow_public = false;

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
    } else if (arg == "--accounts-root") {
      opts.accounts_root = next("--accounts-root");
    } else if (arg == "--max-body-bytes") {
      opts.max_body_bytes = std::strtoull(next("--max-body-bytes"), nullptr, 10);
    } else if (arg == "--max-upload-bytes") {
      opts.max_upload_bytes = std::strtoull(next("--max-upload-bytes"), nullptr, 10);
    } else if (arg == "--sse-keepalive-ms") {
      opts.sse_keepalive_ms = std::strtoull(next("--sse-keepalive-ms"), nullptr, 10);
    } else if (arg == "--allow-chunked-uploads") {
      opts.allow_chunked_uploads = true;
    } else if (arg == "--allow-unauthenticated-public") {
      allow_public = true;
    } else if (arg == "--version") {
      // Build info as JSON (same body as GET /api/version); lets a package be smoke-tested on any OS image.
      const char* v = loom_version();
      std::cout << (v ? v : "{}") << "\n";
      if (v) loom_free_string(v);
      return 0;
    } else if (arg == "-h" || arg == "--help") {
      print_usage(argv[0]);
      return 0;
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      print_usage(argv[0]);
      return 2;
    }
  }

  // A server reachable from other machines with neither accounts nor a token hands every conversation and
  // API key to anyone who can connect. Refuse unless the operator says so explicitly.
  if (!is_loopback(opts.host) && opts.accounts_root.empty() && opts.bearer_token.empty() && !allow_public) {
    std::cerr << "loom-server: refusing to listen on " << opts.host << ":" << opts.port
              << " without authentication.\n"
                 "  That address is reachable from other machines and no user accounts (--accounts-root) or\n"
                 "  token (--token / LOOM_SERVER_TOKEN) are configured, so anyone who can connect could read\n"
                 "  your conversations and use your API keys.\n"
                 "  Configure accounts or a token, bind to 127.0.0.1, or pass --allow-unauthenticated-public\n"
                 "  if you really want this.\n";
    return 2;
  }
  if (!opts.accounts_root.empty() && !opts.bearer_token.empty())
    std::cerr << "loom-server: accounts mode is on; the static token is ignored (sessions are used instead)\n";

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
  if (!opts.accounts_root.empty()) std::cerr << " (accounts: " << opts.accounts_root << ")";
  else if (!opts.bearer_token.empty()) std::cerr << " (bearer token required)";
  std::cerr << std::endl;

  app.run();
  g_app.store(nullptr);
  std::cerr << "loom-server: stopped\n";
  return 0;
}
