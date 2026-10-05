// loom — command-line client over the Loom C++ API.
//
//   loom [--data-dir DIR] [--json] [--quiet] <command> [args]
//
// Application commands open one Runtime on the shared data directory, without
// background workers. Profile editing only reads/writes its profile overlay.
// --json prints machine-readable JSON; otherwise output is for humans.
// Secrets and passwords are read from stdin, never from argv.
#include <unistd.h>

#include <termios.h>
#include <time.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "loom/archive.h"
#include "archive/profile.h"
#include "loom/catalog.h"
#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/crypto.h"
#include "loom/db.h"
#include "loom/graph_engine.h"
#include "loom/graph_memory.h"
#include "loom/importer.h"
#include "loom/knowledge.h"
#include "loom/log.h"
#include "loom/media_providers.h"
#include "loom/memory_engine.h"
#include "loom/provenance.h"
#include "loom/providers.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/semantic_worker.h"
#include "loom/tasks.h"
#include "loom/usage_policy.h"
#include "loom/util/cancel.h"
#include "loom/util/fs.h"
#include "loom/util/json.h"
#include "loom/util/utf8.h"

using namespace loom;

namespace {

std::optional<RuntimeProfile> g_cli_profile;
const Json& cli_data() { return g_cli_profile->values(); }
std::size_t cli_width(std::string_view key) {
  return cli_data().at("presentation").at("widths").at(std::string(key)).get<std::size_t>();
}
std::size_t cli_timestamp(std::string_view key) {
  return cli_data().at("presentation").at("timestamps").at(std::string(key)).get<std::size_t>();
}

// Same discovery precedence as resolve_data_dir, without initialization.
// Help/version must be able to inspect an existing overlay on a read-only root.
fs::path cli_data_root_read_only(const std::optional<std::string>& data_dir) {
  const auto env = PathEnv::from_process();
  const auto resolve = [&](std::string_view raw) {
    std::string path(raw);
    if (!path.empty() && path[0] == '~' && (path.size() == 1 || path[1] == '/') && env.home)
      path = *env.home + path.substr(1);
    return fsutil::resolve_path(path);
  };
  if (data_dir && !data_dir->empty()) return resolve(*data_dir);
  if (env.chatadhd_data) return resolve(*env.chatadhd_data);
  const auto candidates = data_dir_candidates(env);
  for (const auto& candidate : candidates) {
    std::error_code ec;
    if (fs::exists(candidate / kSentinelFile, ec)) return candidate;
  }
  return candidates.front();
}

Status validate_cli_consumers(const RuntimeProfile& profile) {
  const auto& presentation = profile.values().at("presentation");
  const auto check_size = [](const Json& value) -> Status {
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
      return Error(Errc::InvalidArgument, "CLI presentation size must be a nonnegative integer");
    if (value.get<std::uint64_t>() > std::numeric_limits<std::size_t>::max())
      return Error(Errc::InvalidArgument, "CLI presentation size exceeds native size_t representation");
    return {};
  };
  for (const auto* group : {"widths", "timestamps"})
    for (const auto& value : presentation.at(group)) LOOM_TRY(check_size(value));
  LOOM_TRY(check_size(presentation.at("hash_chars")));
  const auto scale = presentation.at("score_decimal_scale").get<double>();
  if (!std::isfinite(scale) || scale <= 0)
    return Error(Errc::InvalidArgument, "CLI score_decimal_scale must be finite and positive");
  return {};
}

Result<RuntimeProfile> load_cli_profile(const std::optional<std::string>& data_dir) {
  const auto root = cli_data_root_read_only(data_dir);
  std::error_code ec;
  (void)fs::symlink_status(root / "profiles" / "cli.pack", ec);
  // A non-directory path component cannot contain an overlay. Unlike a
  // permission error, this is a known absence, relevant to early help/version.
  auto profile = ec == std::errc::not_a_directory ? RuntimeProfile::builtin("cli")
                                                : RuntimeProfile::load("cli", root);
  if (!profile) return profile.error();
  LOOM_TRY(validate_cli_consumers(*profile));
  return std::move(*profile);
}

// ── argument parsing ────────────────────────────────────────────────

struct Args {
  std::vector<std::string> pos;
  std::map<std::string, std::vector<std::string>> opt;
  bool has(const std::string& k) const { return opt.count(k) > 0; }
  std::string get(const std::string& k, const std::string& def = "") const {
    auto it = opt.find(k);
    return it == opt.end() || it->second.empty() ? def : it->second.back();
  }
  std::vector<std::string> all(const std::string& k) const {
    auto it = opt.find(k);
    return it == opt.end() ? std::vector<std::string>{} : it->second;
  }
  int get_int(const std::string& k, int def) const {
    std::string v = get(k);
    if (v.empty()) return def;
    try {
      return std::stoi(v);
    } catch (...) {
      return def;
    }
  }
};

struct UsageError {
  std::string msg;
};

Args parse(int argc, char** argv, int start, const std::set<std::string>& flags) {
  Args a;
  for (int i = start; i < argc; ++i) {
    std::string s = argv[i];
    if (s.size() > 2 && s.rfind("--", 0) == 0) {
      std::string k = s.substr(2);
      std::string v;
      bool has_v = false;
      if (auto eq = k.find('='); eq != std::string::npos) {
        v = k.substr(eq + 1);
        k = k.substr(0, eq);
        has_v = true;
      }
      if (flags.count(k)) {
        a.opt[k].push_back(has_v ? v : "1");
      } else {
        if (!has_v) {
          if (i + 1 >= argc) throw UsageError{"option --" + k + " needs a value"};
          v = argv[++i];
        }
        a.opt[k].push_back(v);
      }
    } else {
      a.pos.push_back(s);
    }
  }
  return a;
}

// ── output ──────────────────────────────────────────────────────────
bool g_json = false;
bool g_quiet = false;
CancelToken g_cancel;

void sigint_handler(int) { g_cancel.cancel(); }

void print_json(const Json& j) { std::cout << json::dump(j, 2) << "\n"; }

std::string one_line(std::string_view s, std::optional<std::size_t> requested_max = std::nullopt) {
  const std::size_t max = requested_max.value_or(cli_width("default"));
  std::string out;
  for (char c : s) out.push_back(c == '\n' || c == '\r' || c == '\t' ? ' ' : c);
  if (!max) return "";
  if (utf8::length(out) > max) out = std::string(utf8::prefix(out, max - 1)) + cli_data().at("presentation").at("ellipsis").get<std::string>();
  return out;
}

int fail(const Error& e) {
  if (g_json) {
    print_json(Json{{"error", Json{{"code", std::string(errc_name(e.code))}, {"message", e.message}}}});
  } else {
    std::cerr << "error: " << e.message << " (" << errc_name(e.code) << ")\n";
  }
  return 1;
}

template <class T>
T must(Result<T> r) {
  if (!r) throw r.error();
  return std::move(r).value();
}
void must(const Status& s) {
  if (!s) throw s.error();
}

std::string need(const Args& a, std::size_t i, const char* what) {
  if (i >= a.pos.size()) throw UsageError{std::string("missing ") + what};
  return a.pos[i];
}

std::string read_stdin_secret(const char* prompt) {
  bool tty = ::isatty(STDIN_FILENO);
  termios old{};
  if (tty) {
    std::cerr << prompt << std::flush;
    ::tcgetattr(STDIN_FILENO, &old);
    termios quiet = old;
    quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    ::tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
  }
  std::string v;
  std::getline(std::cin, v);
  if (tty) {
    ::tcsetattr(STDIN_FILENO, TCSANOW, &old);
    std::cerr << "\n";
  }
  while (!v.empty() && (v.back() == '\r' || v.back() == '\n')) v.pop_back();
  return v;
}

std::string read_all_stdin() {
  std::ostringstream os;
  os << std::cin.rdbuf();
  return os.str();
}

Result<Json> read_json_input(const Args& a) {
  if (a.has("file")) {
    LOOM_TRY_ASSIGN(auto text, fsutil::read_file(a.get("file")));
    return json::parse(text);
  }
  return json::parse(read_all_stdin());
}

void progress_line(std::string_view stage, std::int64_t cur, std::int64_t total, std::string_view msg) {
  if (g_quiet) return;
  std::cerr << "\r\x1b[K[" << stage << "] ";
  if (total > 0) std::cerr << cur << "/" << total << " ";
  std::cerr << one_line(msg, cli_width("short")) << std::flush;
}

int cli_limit(Runtime& rt, std::string_view key) {
  auto profile = must(RuntimeProfile::load("cli", rt.paths().root));
  return profile.values().at("limits").at(std::string(key)).get<int>();
}

// ── commands ────────────────────────────────────────────────────────
void print_conv_line(const Conversation& c) {
  std::cout << c.id << "  " << c.updated.substr(0, cli_timestamp("conversation")) << "  " << one_line(c.title, cli_width("short")) << "\n";
}

void print_msg(const Message& m) {
  std::cout << "── " << m.role << " · " << m.id << " · " << m.created.substr(0, cli_timestamp("message"))
            << (m.status != "active" ? " · " + m.status : "")
            << (m.version_num > 1 ? " · v" + std::to_string(m.version_num) : "") << "\n"
            << m.text << "\n\n";
}

int cmd_conv(Runtime& rt, const Args& a) {
  std::string sub = need(a, 0, "conv subcommand");
  Database& db = rt.db();
  if (sub == "list") {
    auto convs = must(db.list_convs(a.get_int("limit", cli_limit(rt, "conversation_list"))));
    if (g_json) {
      Json j = Json::array();
      for (const auto& c : convs) j.push_back(c.to_json());
      print_json(j);
    } else {
      for (const auto& c : convs) print_conv_line(c);
    }
  } else if (sub == "create") {
    auto c = must(db.create_conv(a.pos.size() > 1 ? a.pos[1] : cli_data().at("creation").at("conversation_title").get<std::string>()));
    g_json ? print_json(c.to_json()) : print_conv_line(c);
  } else if (sub == "show") {
    std::string id = need(a, 1, "conversation id");
    auto c = must(db.get_conv(id));
    if (!c) throw Error(Errc::NotFound, "no conversation " + id);
    auto msgs = must(db.get_msgs(id, a.has("all")));
    if (g_json) {
      Json m = Json::array();
      for (const auto& x : msgs) m.push_back(x.to_json());
      print_json(Json{{"conversation", c->to_json()}, {"messages", m}});
    } else {
      std::cout << "# " << c->title << "  (" << c->id << ", " << msgs.size() << " messages)\n\n";
      for (const auto& x : msgs) print_msg(x);
    }
  } else if (sub == "rename") {
    ConvPatch p;
    p.title = need(a, 2, "title");
    must(db.update_conv(need(a, 1, "conversation id"), p));
    if (!g_quiet) std::cout << "renamed\n";
  } else if (sub == "delete") {
    must(db.delete_conv(need(a, 1, "conversation id")));
    if (!g_quiet) std::cout << "deleted\n";
  } else {
    throw UsageError{"unknown conv subcommand: " + sub};
  }
  return 0;
}

int cmd_msg(Runtime& rt, const Args& a) {
  std::string sub = need(a, 0, "msg subcommand");
  std::string id = need(a, 1, "message id");
  Database& db = rt.db();
  if (sub == "edit") {
    std::string text = need(a, 2, "text");
    if (text == "-") text = read_all_stdin();
    auto nid = must(db.edit_msg(id, text));
    if (!nid) throw Error(Errc::NotFound, "no message " + id);
    auto m = must(db.get_msg(*nid));
    g_json ? print_json(m->to_json()) : print_msg(*m);
  } else if (sub == "restore") {
    if (!must(db.restore_version(id))) throw Error(Errc::NotFound, "no message " + id);
    if (!g_quiet) std::cout << "restored\n";
  } else if (sub == "versions") {
    std::string group = id;
    if (id.rfind("vg_", 0) != 0) {
      auto m = must(db.get_msg(id));
      if (!m || !m->version_group_id) throw Error(Errc::NotFound, "no message " + id);
      group = *m->version_group_id;
    }
    auto v = must(db.get_versions(group));
    if (g_json) {
      Json j = Json::array();
      for (const auto& m : v) j.push_back(m.to_json());
      print_json(j);
    } else {
      for (const auto& m : v) print_msg(m);
    }
  } else if (sub == "status") {
    must(db.set_msg_status(id, need(a, 2, "status")));
    if (!g_quiet) std::cout << "ok\n";
  } else {
    throw UsageError{"unknown msg subcommand: " + sub};
  }
  return 0;
}

int cmd_chat(Runtime& rt, const Args& a) {
  std::string text;
  for (const auto& p : a.pos) text += (text.empty() ? "" : " ") + p;
  if (text == "-" || text.empty()) text = read_all_stdin();
  if (utf8::is_blank(text)) throw UsageError{"empty message"};
  ChatOptions o;
  if (a.has("conv")) o.conv_id = a.get("conv");
  if (a.has("model")) o.model = a.get("model");
  if (a.has("depth")) o.context_depth = a.get_int("depth", cli_limit(rt, "chat_depth"));
  if (a.has("effort")) o.reasoning_effort = a.get("effort");
  o.web_search = a.has("web");
  o.deep_research = a.has("deep");
  bool tty = ::isatty(STDOUT_FILENO);
  bool in_reasoning = false;
  ChatCallbacks cb;
  cb.on_reasoning = [&](std::string_view s) {
    if (g_json) return;
    if (!in_reasoning) in_reasoning = true;
    if (tty) std::cout << "\x1b[2m" << s << "\x1b[0m" << std::flush;
    else std::cerr << s << std::flush;
  };
  cb.on_chunk = [&](std::string_view s) {
    if (g_json) return;
    if (in_reasoning) {
      std::cout << (tty ? "\n\n" : "");
      in_reasoning = false;
    }
    std::cout << s << std::flush;
  };
  std::signal(SIGINT, sigint_handler);
  auto r = rt.chat().send(text, o, cb, &g_cancel);
  if (!r) throw r.error();
  if (g_json) print_json(r->to_json());
  else std::cout << (r->cancelled ? "\n[cancelled]\n" : "\n");
  return 0;
}

// Readable summary of the lossless-export report (import --audit).
void print_export_audit(const Json& rep) {
  auto num = [](const Json& j, const char* k) { return j.contains(k) && j[k].is_number() ? j[k].get<std::int64_t>() : 0; };
  auto list = [](const Json& j, const char* k) {
    std::cout << "  " << k << ": " << (j.contains(k) ? j[k].size() : 0) << "\n";
    if (j.contains(k) && j[k].is_array()) {
      for (const auto& x : j[k]) std::cout << "    - " << (x.is_string() ? x.get<std::string>() : x.dump()) << "\n";
    }
  };
  std::cout << "export audit: provider " << (rep.contains("provider") ? rep["provider"].dump() : "?") << ", "
            << num(rep, "member_count") << " archive members" << (rep.value("partial", false) ? " (PARTIAL)" : "") << "\n";
  const Json& c = rep["counts"];
  std::cout << "  conversations " << num(c, "conversation") << ", messages " << num(c, "message") << ", blocks "
            << num(c, "block") << ", attachments " << num(c, "attachment") << ", branches " << num(c, "branch")
            << ", fork points " << num(c, "fork_points") << "\n";
  std::cout << "  json leaves preserved verbatim: " << num(rep, "leaves_preserved") << "/" << num(rep, "json_leaves") << "\n";
  std::cout << "  attachment pointers linked to archive files: " << (rep.contains("pointer_links") ? rep["pointer_links"].size() : 0)
            << "\n";
  list(rep, "unresolved_keys");
  list(rep, "unreferenced_members");
  list(rep, "unknown_members");
  list(rep, "errors");
  list(rep, "warnings");
  if (rep.contains("repairs") && !rep["repairs"].empty()) std::cout << "  repairs: " << rep["repairs"].dump() << "\n";
}

int cmd_import(Runtime& rt, const Args& a) {
  ImportOptions o;
  if (a.has("title")) o.title = a.get("title");
  o.force = a.has("force");
  if (a.has("export-mode")) {
    const std::string m = a.get("export-mode");
    if (m == "on") o.export_mode = ExportMode::On;
    else if (m == "off") o.export_mode = ExportMode::Off;
    else if (m != "auto") throw UsageError{"--export-mode must be auto, off or on"};
  }
  o.progress = [](std::int64_t c, std::int64_t t, std::string_view s) { progress_line("import", c, t, s); };
  auto r = must(rt.importer().import_file(need(a, 0, "path"), o));
  if (!g_quiet) std::cerr << "\n";
  if (g_json) {
    print_json(r.to_json());
  } else {
    std::cout << "format " << r.format << ": " << r.conversations.size() << " conversations, " << r.messages
              << " messages" << (r.already_imported ? " (already imported; --force re-imports)" : "") << "\n";
    for (const auto& c : r.conversations) print_conv_line(c);
    for (const auto& w : r.warnings) std::cerr << "warning: " << w << "\n";
    if (a.has("audit") && r.export_report.is_object()) print_export_audit(r.export_report);
    else if (a.has("audit")) std::cout << "audit: not an OpenAI/Anthropic export (no export report)\n";
  }
  return 0;
}

int cmd_export(Runtime& rt, const Args& a) {
  std::string id = need(a, 0, "conversation id");
  std::string fmt = a.get("format", cli_data().at("creation").at("export_format").get<std::string>());
  auto s = must(rt.exporter().export_conversation(id, fmt, a.has("all")));
  if (a.has("out")) {
    must(fsutil::atomic_write(a.get("out"), s));
    if (!g_quiet) std::cerr << "wrote " << a.get("out") << "\n";
  } else {
    std::cout << s;
  }
  return 0;
}

int cmd_graph(Runtime& rt, const Args& a) {
  std::string sub = need(a, 0, "graph subcommand");
  Database& db = rt.db();
  if (sub == "nodes") {
    std::optional<std::string_view> kind;
    std::string k = a.get("kind");
    if (!k.empty()) kind = k;
    auto nodes = must(db.list_nodes(kind, a.get_int("limit", cli_limit(rt, "graph_node_list"))));
    if (g_json) {
      Json j = Json::array();
      for (const auto& n : nodes) j.push_back(n.to_json());
      print_json(j);
    } else {
      for (const auto& n : nodes) std::cout << n.id << "  " << n.kind << "  " << one_line(n.label, cli_width("wide")) << "\n";
    }
  } else if (sub == "edges") {
    std::string node = a.get("node"), type = a.get("type");
    auto links = must(db.get_links(node.empty() ? std::nullopt : std::optional<std::string_view>(node),
                                   type.empty() ? std::nullopt : std::optional<std::string_view>(type)));
    if (g_json) {
      Json j = Json::array();
      for (const auto& l : links) j.push_back(l.to_json());
      print_json(j);
    } else {
      for (const auto& l : links) std::cout << l.src << " -" << l.link_type << "-> " << l.dst << "  (" << l.weight << ")\n";
    }
  } else if (sub == "expand") {
    std::vector<std::string> seeds(a.pos.begin() + 1, a.pos.end());
    if (seeds.empty()) throw UsageError{"graph expand needs node or message ids"};
    print_json(must(rt.graph_memory().expand(seeds, a.get_int("depth", cli_limit(rt, "graph_expansion_depth")))));
  } else if (sub == "reindex") {
    int n = a.pos.size() > 1 ? rt.graph().reindex_conversation(a.pos[1]) : rt.graph().reindex_all();
    g_json ? print_json(Json{{"reindexed", n}}) : void(std::cout << "reindexed " << n << " messages\n");
  } else if (sub == "stats") {
    auto& c = rt.db().conn();
    auto lk = rt.db().lock();
    Json j = Json::object();
    const std::map<std::string, std::string> metrics{
      {"conversations", "SELECT COUNT(*) FROM conversations"}, {"messages", "SELECT COUNT(*) FROM messages"},
      {"nodes", "SELECT COUNT(*) FROM nodes"}, {"links", "SELECT COUNT(*) FROM links"}};
    for (const auto& entry : cli_data().at("graph_stats")) {
      const auto key = entry.get<std::string>();
      auto metric = metrics.find(key);
      if (metric == metrics.end()) throw Error(Errc::Unavailable, "graph metric is unavailable: " + key);
      j[key] = must(c.query_int(metric->second)).value_or(0);
    }
    Json kinds = Json::object();
    auto st = must(c.prepare("SELECT kind, COUNT(*) FROM nodes GROUP BY kind ORDER BY 2 DESC"));
    while (must(st.step())) kinds[st.get_text(0)] = st.get_int(1);
    j["node_kinds"] = kinds;
    Json types = Json::object();
    auto st2 = must(c.prepare("SELECT link_type, COUNT(*) FROM links GROUP BY link_type ORDER BY 2 DESC"));
    while (must(st2.step())) types[st2.get_text(0)] = st2.get_int(1);
    j["link_types"] = types;
    print_json(j);
  } else {
    throw UsageError{"unknown graph subcommand: " + sub};
  }
  return 0;
}

int cmd_context(Runtime& rt, const Args& a) {
  ContextRequest req;
  for (const auto& p : a.pos) req.text += (req.text.empty() ? "" : " ") + p;
  if (req.text.empty()) throw UsageError{"context needs text"};
  req.depth = a.get_int("depth", cli_limit(rt, "context_depth"));
  req.max_tokens = a.get_int("max-tokens", cli_limit(rt, "context_tokens"));
  auto cs = must(rt.context().select(req));
  if (g_json) print_json(cs.to_json());
  else std::cout << cs.prompt_text << "\n\n(" << cs.items.size() << " items, ~" << cs.token_estimate << " tokens"
                 << (cs.truncated ? ", truncated" : "") << ")\n";
  return 0;
}

int cmd_search(Runtime& rt, const Args& a) {
  std::string q;
  for (const auto& p : a.pos) q += (q.empty() ? "" : " ") + p;
  SearchOptions o;
  o.limit = a.get_int("limit", cli_limit(rt, "search_results"));
  if (a.has("conv")) o.conv_id = a.get("conv");
  o.include_inactive = a.has("all");
  auto r = must(rt.db().search_messages(q, o));
  if (g_json) {
    print_json(r.to_json());
  } else {
    std::cout << r.hits.size() << " hits (" << r.mode << ")\n";
    for (const auto& h : r.hits) {
      std::cout << h.message.id << "  " << h.message.conv_id << "  " << json::format_float_py(std::round(h.score * cli_data().at("presentation").at("score_decimal_scale").get<double>()) / cli_data().at("presentation").at("score_decimal_scale").get<double>())
                << "  " << one_line(h.snippet, cli_width("default")) << "\n";
    }
  }
  return 0;
}

int cmd_semantic(Runtime& rt, const Args& a) {
  std::string sub = need(a, 0, "semantic subcommand");
  auto& w = rt.worker();
  if (sub == "pause") w.pause();
  else if (sub == "resume") w.resume();
  else if (sub == "wake") w.wake();
  else if (sub == "run") {
    int total = 0;
    std::signal(SIGINT, sigint_handler);
    while (!g_cancel.cancelled()) {
      int n = must(w.drain_once());
      if (n <= 0) break;
      total += n;
      progress_line("semantic", total, -1, "messages analysed");
    }
    if (!g_quiet) std::cerr << "\n";
  } else if (sub != "status") {
    throw UsageError{"unknown semantic subcommand: " + sub};
  }
  print_json(w.status().to_json());
  return 0;
}

int cmd_memory(Runtime& rt, const Args& a) {
  std::string sub = need(a, 0, "memory subcommand");
  auto& m = rt.memory();
  if (sub == "list") {
    auto all = m.get_all();
    if (g_json) {
      Json j = Json::array();
      for (const auto& n : all) j.push_back(n.to_json());
      print_json(j);
    } else {
      for (const auto& n : all) {
        std::cout << std::string(static_cast<std::size_t>(n.depth) * 2, ' ') << (n.active ? "● " : "○ ") << n.id
                  << "  " << one_line(n.content, cli_width("wide")) << "\n";
      }
    }
  } else if (sub == "add") {
    std::optional<std::string> parent;
    if (a.has("parent")) parent = a.get("parent");
    std::string id = must(m.add_node(need(a, 1, "content"), parent, a.get("type", cli_data().at("creation").at("memory_node_type").get<std::string>())));
    g_json ? print_json(Json{{"id", id}}) : void(std::cout << id << "\n");
  } else if (sub == "delete") {
    must(m.delete_node(need(a, 1, "memory id"), true));
  } else if (sub == "context") {
    std::cout << m.get_active_context() << "\n";
  } else {
    throw UsageError{"unknown memory subcommand: " + sub};
  }
  return 0;
}

int cmd_config(Runtime& rt, const Args& a) {
  std::string sub = need(a, 0, "config subcommand");
  auto& c = rt.config();
  if (sub == "get") {
    if (a.pos.size() > 1) print_json(c.get(a.pos[1]));
    else print_json(c.all());
  } else if (sub == "set") {
    std::string key = need(a, 1, "key"), raw = need(a, 2, "value");
    auto parsed = json::parse(raw);
    c.set(key, parsed ? *parsed : Json(raw));
    must(c.save());
    if (!g_quiet) std::cout << key << " = " << json::dump(c.get(key)) << "\n";
  } else {
    throw UsageError{"unknown config subcommand: " + sub};
  }
  return 0;
}

int cmd_secret(Runtime& rt, const Args& a) {
  std::string sub = need(a, 0, "secret subcommand");
  auto& s = rt.secrets();
  if (sub == "list") {
    Json j = Json::array();
    for (const auto& k : s.keys()) j.push_back(k);
    print_json(j);
    return 0;
  }
  std::string key = need(a, 1, "key");
  if (a.pos.size() > 2) throw UsageError{"secret values are read from stdin, never from the command line"};
  if (sub == "set") {
    std::string v = read_stdin_secret(("value for " + key + ": ").c_str());
    if (v.empty()) throw UsageError{"empty secret"};
    s.set(key, v);
    must(s.save());
    rt.media().refresh();
    if (!g_quiet) std::cout << key << " set\n";
  } else if (sub == "has") {
    bool h = s.has(key);
    g_json ? print_json(Json{{"key", key}, {"set", h}}) : void(std::cout << (h ? "yes" : "no") << "\n");
    return h ? 0 : 3;
  } else if (sub == "delete") {
    if (!s.contains(key)) throw Error(Errc::NotFound, "no secret " + key);
    s.erase(key);
    must(s.save());
    if (!g_quiet) std::cout << key << " deleted\n";
  } else {
    throw UsageError{"unknown secret subcommand: " + sub};
  }
  return 0;
}

int cmd_models(Runtime& rt, const Args& a) {
  if (a.has("refresh")) must(rt.models().update_from_api());
  Json all = rt.models().all();
  if (g_json) {
    print_json(all);
  } else {
    for (const auto& m : all) std::cout << json::get_string(m, "id") << "  " << json::get_string(m, "name") << "\n";
    std::cout << all.size() << " models\n";
  }
  return 0;
}

void print_task(const TaskRecord& t) {
  std::cout << t.id << "  " << t.kind << "  " << t.status << "  " << t.updated.substr(0, cli_timestamp("task"))
            << (t.error.empty() ? "" : "  " + one_line(t.error, cli_width("brief"))) << "\n";
}

int cmd_tasks(Runtime& rt, const Args& a) {
  std::string sub = a.pos.empty() ? "list" : a.pos[0];
  auto& te = rt.tasks();
  if (sub == "list") {
    TaskFilter f;
    if (a.has("kind")) f.kind = a.get("kind");
    if (a.has("status")) f.status = a.get("status");
    f.limit = a.get_int("limit", cli_limit(rt, "task_list"));
    auto v = must(te.list(f));
    if (g_json) {
      Json j = Json::array();
      for (const auto& t : v) {
        Json tj = t.to_json();
        tj.erase("params");
        tj.erase("checkpoint");
        j.push_back(tj);
      }
      print_json(j);
    } else {
      for (const auto& t : v) print_task(t);
    }
  } else if (sub == "show") {
    auto t = must(te.get(need(a, 1, "task id")));
    if (!t) throw Error(Errc::NotFound, "no task");
    print_json(t->to_json());
  } else if (sub == "resume") {
    int rec = must(te.recover_interrupted());
    // paused tasks too
    TaskFilter f;
    f.status = "paused";
    f.limit = a.get_int("limit", cli_limit(rt, "task_resume_scan"));
    for (const auto& t : must(te.list(f))) must(te.resume(t.id));
    int runs = must(te.run_pending());
    print_json(Json{{"recovered", rec}, {"runs", runs}});
  } else if (sub == "cancel") {
    must(te.cancel(need(a, 1, "task id")));
    if (!g_quiet) std::cout << "cancelled\n";
  } else {
    throw UsageError{"unknown tasks subcommand: " + sub};
  }
  return 0;
}

int cmd_provenance(Runtime& rt, const Args& a) {
  std::string id = need(a, 0, "id");
  auto recs = must(rt.provenance().for_subject(id));
  Json r = Json::array();
  Json sources = Json::object();
  for (const auto& p : recs) {
    r.push_back(p.to_json());
    if (!p.source_id.empty() && !sources.contains(p.source_id)) {
      auto s = must(rt.provenance().get_source(p.source_id));
      if (s) sources[p.source_id] = s->to_json();
    }
  }
  print_json(Json{{"subject_id", id}, {"records", r}, {"sources", sources}});
  return 0;
}

int cmd_sources(Runtime& rt, const Args& a) {
  auto v = must(rt.provenance().list_sources(a.get_int("limit", cli_limit(rt, "source_list"))));
  if (g_json) {
    Json j = Json::array();
    for (const auto& s : v) j.push_back(s.to_json());
    print_json(j);
  } else {
    for (const auto& s : v) {
      std::cout << s.id << "  " << s.kind << "  " << s.format << "  " << s.blob_hash.substr(0, cli_data().at("presentation").at("hash_chars").get<std::size_t>()) << "  "
                << one_line(s.uri, cli_width("short")) << "\n";
    }
  }
  return 0;
}

int cmd_artifacts(Runtime& rt, const Args& a) {
  std::string sub = a.pos.empty() ? "list" : a.pos[0];
  if (sub == "list") {
    std::string kind = a.get("kind");
    auto v = must(rt.provenance().list_artifacts(a.get_int("limit", cli_limit(rt, "artifact_list")),
                                                 kind.empty() ? std::nullopt : std::optional<std::string_view>(kind)));
    if (g_json) {
      Json j = Json::array();
      for (const auto& x : v) j.push_back(x.to_json());
      print_json(j);
    } else {
      for (const auto& x : v) {
        std::cout << x.id << "  " << x.kind << "  " << x.created.substr(0, cli_timestamp("artifact")) << "  " << x.title << "\n";
      }
    }
  } else if (sub == "show") {
    auto x = must(rt.provenance().get_artifact(need(a, 1, "artifact id")));
    if (!x) throw Error(Errc::NotFound, "no artifact");
    std::string content = must(rt.blobs().read(x->blob_hash));
    if (a.has("out")) must(fsutil::atomic_write(a.get("out"), content));
    else if (g_json) print_json(Json{{"artifact", x->to_json()}, {"content", content}});
    else std::cout << content;
  } else {
    throw UsageError{"unknown artifacts subcommand: " + sub};
  }
  return 0;
}

int cmd_archive(Runtime& rt, const Args& a) {
  std::string sub = need(a, 0, "archive subcommand");
  if (sub == "status") {
    print_json(must(rt.archive().status(a.pos.size() > 1 ? a.pos[1] : "")));
    return 0;
  }
  if (sub != "run") throw UsageError{"unknown archive subcommand: " + sub};
  Json cj = Json::object();
  if (a.has("config")) cj = must(json::parse(must(fsutil::read_file(a.get("config")))));
  auto archive_profile = must(archive::ArchiveProfile::load(rt.paths().root));
  archive::ArchiveConfig cfg = must(archive::ArchiveConfig::from_json_with_profile(cj, archive_profile));
  for (const auto& s : a.all("source")) cfg.sources.push_back(s);
  if (a.has("repo")) cfg.repo = a.get("repo");
  if (a.has("out")) cfg.out_dir = a.get("out");
  for (const auto& s : a.all("seed")) cfg.seed_terms.push_back(s);
  for (const auto& s : a.all("exclude")) cfg.exclude.push_back(s);
  if (a.has("project")) cfg.project = a.get("project");
  cfg.max_passes = a.get_int("max-passes", cfg.max_passes);
  cfg.max_new_terms = a.get_int("max-new-terms", cfg.max_new_terms);
  cfg.max_hits_per_term = a.get_int("max-hits", cfg.max_hits_per_term);
  cfg.max_synthesis_rounds = a.get_int("rounds", cfg.max_synthesis_rounds);
  if (a.has("no-git")) cfg.git = false;
  if (a.has("no-code")) cfg.code = false;
  if (a.has("include-db")) cfg.include_db = true;
  if (a.has("llm")) cfg.llm = a.get("llm");
  if (a.has("force")) cfg.force = true;
  cfg = must(archive::ArchiveConfig::from_json_with_profile(cfg.to_json(), archive_profile));
  if (cfg.sources.empty() && !cfg.repo && !cfg.include_db) throw UsageError{"archive run needs --source, --repo or --include-db"};
  std::signal(SIGINT, sigint_handler);
  auto r = must(rt.archive().run(cfg, progress_line, &g_cancel));
  if (!g_quiet) std::cerr << "\r\x1b[K";

  // --knowledge: also run the knowledge pipeline (catalog -> extract ->
  // resolve -> assess -> generalize -> materialize) over the same sources;
  // its products go to <out>/knowledge when --out is given.
  Json kr_json = Json(nullptr);
  bool knowledge_done = true;
  if (a.has("knowledge") && r.status == "done") {
    knowledge::KnowledgeConfig kcfg;
    kcfg.sources = cfg.sources;
    kcfg.repo = cfg.repo;
    kcfg.force = cfg.force;
    kcfg.project = cfg.project;
    kcfg.llm = cfg.llm;
    if (!cfg.out_dir.empty()) kcfg.out_dir = (fsutil::expand_user(cfg.out_dir) / "knowledge").string();
    auto kr = rt.knowledge().run(kcfg, progress_line, &g_cancel);
    if (!g_quiet) std::cerr << "\r\x1b[K";
    kr_json = kr ? kr->to_json() : Json{{"error", Json{{"code", std::string(errc_name(kr.error().code))}, {"message", kr.error().message}}}};
    knowledge_done = kr && kr->status == "done";
  }

  if (g_json) {
    Json out = r.to_json();
    if (!kr_json.is_null()) out["knowledge"] = kr_json;
    print_json(out);
    return r.status == "done" && knowledge_done ? 0 : 4;
  }
  std::cout << "archive run " << r.run_id << ": " << r.status << "\n";
  for (const auto& s : r.stages) {
    std::cout << "  " << s.stage << (s.cache_hit ? "  (cache hit)" : s.resumed ? "  (resumed)" : "") << "  "
              << json::dump(s.stats) << "\n";
  }
  if (r.status != "done") {
    std::cout << "Run the same command again to resume from the last checkpoint.\n";
    return 4;
  }
  const Json& sm = r.summary;
  std::cout << "project " << json::get_string(sm, "project") << ": " << json::get_int(sm, "docs") << " documents, "
            << json::get_int(sm, "hits") << " retrieved, " << json::get_int(sm, "vocabulary") << " vocabulary terms\n";
  for (const auto& t : sm["themes"]) {
    std::cout << "  " << json::get_string(t, "id") << " " << json::get_string(t, "label") << " ("
              << json::get_int(t, "size") << ")\n";
  }
  std::string od = json::get_string(sm, "out_dir");
  if (!od.empty()) std::cout << "artifacts written to " << od << "\n";
  for (const auto& art : sm["artifacts"]) std::cout << "  " << json::get_string(art, "id") << "  " << json::get_string(art, "name") << "\n";
  for (const auto& w : sm["warnings"]) std::cerr << "warning: " << w.get<std::string>() << "\n";
  if (!kr_json.is_null()) {
    std::cout << "knowledge run " << json::get_string(kr_json, "run", "") << ": " << json::get_string(kr_json, "status", "") << "\n";
    if (!knowledge_done) std::cerr << "knowledge pipeline did not complete: " << json::dump(kr_json["error"]) << "\n";
  }
  return knowledge_done ? 0 : 4;
}

// `loom knowledge run|status`: the knowledge pipeline on its own
// (knowledge.h). Options map onto KnowledgeConfig; --config FILE.json gives
// the whole config (options given on the command line are added to it).
int cmd_knowledge(Runtime& rt, const Args& a) {
  std::string sub = need(a, 0, "knowledge subcommand");
  if (sub == "status") {
    print_json(must(rt.knowledge().status(a.pos.size() > 1 ? a.pos[1] : "")));
    return 0;
  }
  if (sub != "run") throw UsageError{"unknown knowledge subcommand: " + sub};
  Json cj = Json::object();
  if (a.has("config")) cj = must(json::parse(must(fsutil::read_file(a.get("config")))));
  const auto knowledge_profile = must(RuntimeProfile::load("knowledge", rt.paths().root));
  knowledge::KnowledgeConfig cfg = must(knowledge::KnowledgeConfig::from_json_with_profile(cj, knowledge_profile));
  for (const auto& s : a.all("source")) cfg.sources.push_back(s);
  if (a.has("repo")) cfg.repo = a.get("repo");
  if (a.has("out")) cfg.out_dir = a.get("out");
  if (a.has("project")) cfg.project = a.get("project");
  if (a.has("cut")) cfg.prior_cut = a.get("cut");
  if (a.has("llm")) cfg.llm = a.get("llm");
  if (a.has("no-priors")) cfg.priors = false;
  if (a.has("force")) cfg.force = true;
  for (const auto& s : a.all("stage")) cfg.stages.push_back(s);
  if (a.has("snapshot")) {
    Json snaps = Json::array();
    for (const auto& s : a.all("snapshot")) {
      auto eq = s.find('=');
      if (eq == std::string::npos) snaps.push_back(s);
      else snaps.push_back(Json{{"dir", s.substr(0, eq)}, {"label", s.substr(eq + 1)}});
    }
    if (!cfg.stage_params.is_object()) cfg.stage_params = Json::object();
    cfg.stage_params["resolve"]["snapshots"] = snaps;
  }
  cfg = must(knowledge::KnowledgeConfig::from_json_with_profile(cfg.to_json(), knowledge_profile));  // validate + order stages
  if (cfg.sources.empty() && !cfg.repo) throw UsageError{"knowledge run needs --source or --repo"};
  std::signal(SIGINT, sigint_handler);
  auto r = must(rt.knowledge().run(cfg, progress_line, &g_cancel));
  if (!g_quiet) std::cerr << "\r\x1b[K";
  if (g_json) {
    print_json(r.to_json());
    return r.status == "done" ? 0 : 4;
  }
  std::cout << "knowledge run " << r.run << ": " << r.status << (r.error.empty() ? "" : "  (" + r.error + ")") << "\n";
  for (const auto& s : r.stages) {
    std::cout << "  " << s.stage << (s.cache_hit ? "  (cache hit)" : s.resumed ? "  (resumed)" : "") << "  "
              << one_line(json::dump(s.stats), cli_width("summary")) << "\n";
  }
  if (r.status == "paused") std::cout << "Run the same command again to resume from the last checkpoint.\n";
  if (!cfg.out_dir.empty() && r.status == "done") std::cout << "products written to " << cfg.out_dir << "\n";
  return r.status == "done" ? 0 : 4;
}

void print_catalog_unit_line(const catalog::CatalogUnit& u) {
  std::cout << u.unit.id << "  " << u.platform << "  " << u.unit.date.substr(0, cli_timestamp("date")) << "  " << u.n_msgs << "msg  "
            << one_line(u.unit.title, cli_width("brief")) << "\n";
}

catalog::Catalog make_catalog(Runtime& rt) { return catalog::Catalog(rt, must(rt.knowledge().pack())); }

// `loom catalog eval --truth ground_truth.json`: recall/precision/noise-trap
// FP against a synthetic_dev-shaped ground truth (units.relevant/noise_traps/
// noise_generic, each with a "conv_id" that matches a catalogued unit's
// ext_id). Not a Catalog method (catalog.h has no evaluate()); implemented
// directly over query()/loom_cat_decisions so it needs no extra C ABI.
int cmd_catalog_eval(Runtime& rt, const Args& a) {
  std::string truth_path = a.get("truth");
  if (truth_path.empty()) throw UsageError{"catalog eval needs --truth ground_truth.json"};
  Json truth = must(json::parse(must(fsutil::read_file(truth_path))));
  const auto& eval = cli_data().at("evaluation");
  const Json& units = truth[eval.at("units_key").get<std::string>()];

  catalog::UnitQuery q;
  q.run_id = a.get("run");
  q.limit = a.get_int("limit", cli_limit(rt, "catalog_eval_scan"));
  auto cat = make_catalog(rt);
  auto all = must(cat.query(q));

  std::map<std::string, bool> selected_by_ext;
  {
    auto lk = rt.db().lock();
    std::string run_id = q.run_id;
    if (run_id.empty()) {
      auto r = rt.db().conn().query_text("SELECT run_id FROM loom_cat_decisions ORDER BY rowid DESC LIMIT 1");
      if (r && *r) run_id = **r;
    }
    for (const auto& u : all) {
      auto d = rt.db().conn().query_int("SELECT selected FROM loom_cat_decisions WHERE run_id = ? AND unit_id = ?",
                                        run_id, u.unit.id);
      selected_by_ext[u.ext_id] = d && d->has_value() && **d != 0;
    }
  }

  auto count_selected = [&](const Json& arr) {
    int n = 0;
    for (const auto& r : arr) {
      auto it = selected_by_ext.find(json::get_string(r, eval.at("external_id_key").get<std::string>()));
      if (it != selected_by_ext.end() && it->second) ++n;
    }
    return n;
  };
  int relevant_total = static_cast<int>(units[eval.at("relevant_key").get<std::string>()].size());
  int relevant_selected = count_selected(units[eval.at("relevant_key").get<std::string>()]);
  int traps_total = static_cast<int>(units[eval.at("traps_key").get<std::string>()].size());
  int traps_selected = count_selected(units[eval.at("traps_key").get<std::string>()]);
  int generic_selected = count_selected(units[eval.at("generic_key").get<std::string>()]);
  int selected_total = 0;
  for (auto& [k, v] : selected_by_ext) selected_total += v ? 1 : 0;
  int false_positives = traps_selected + generic_selected + std::max(0, selected_total - relevant_selected - traps_selected - generic_selected);
  double recall = relevant_total ? static_cast<double>(relevant_selected) / relevant_total : 0.0;
  double precision = selected_total ? static_cast<double>(relevant_selected) / selected_total : 0.0;
  double trap_fpr = traps_total ? static_cast<double>(traps_selected) / traps_total : 0.0;

  Json out{{"relevant_total", relevant_total},   {"relevant_selected", relevant_selected},
          {"traps_total", traps_total},          {"traps_selected", traps_selected},
          {"noise_generic_selected", generic_selected}, {"selected_total", selected_total},
          {"false_positives", false_positives},  {"recall", recall},
          {"precision", precision},              {"trap_fpr", trap_fpr}};
  print_json(out);
  return 0;
}

int cmd_catalog(Runtime& rt, const Args& a) {
  std::string sub = need(a, 0, "catalog subcommand");
  auto cat = make_catalog(rt);
  std::signal(SIGINT, sigint_handler);

  if (sub == "scan") {
    catalog::ScanConfig cfg;
    cfg.sources = a.all("source");
    if (a.has("mobile")) cfg.sketch = catalog::SketchParams::mobile();
    cfg.threads = a.get_int("threads", 0);
    cfg.force = a.has("force");
    if (cfg.sources.empty()) throw UsageError{"catalog scan needs at least one --source"};
    auto r = must(cat.scan(cfg, progress_line, &g_cancel));
    if (!g_quiet) std::cerr << "\r\x1b[K";
    print_json(r);
  } else if (sub == "profile") {
    catalog::ProfileConfig cfg;
    if (a.has("repo")) cfg.repo = a.get("repo");
    for (const auto& t : a.all("extra-term")) cfg.extra_terms.push_back(t);
    print_json(must(cat.build_profile(cfg)).to_json());
  } else if (sub == "score") {
    catalog::ScoreConfig cfg;
    cfg.profile_id = a.get("profile");
    cfg.max_passes = a.get_int("max-passes", cfg.max_passes);
    auto r = must(cat.score(cfg, progress_line, &g_cancel));
    if (!g_quiet) std::cerr << "\r\x1b[K";
    print_json(r);
    must(cat.select(json::get_string(r, "run_id")));
  } else if (sub == "list") {
    catalog::UnitQuery q;
    if (a.has("label")) q.label = a.get("label");
    if (a.has("project")) q.project = a.get("project");
    if (a.has("text")) q.text = a.get("text");
    q.run_id = a.get("run");
    q.sort = a.get("sort", "score");
    q.limit = a.get_int("limit", cli_limit(rt, "catalog_list"));
    q.offset = a.get_int("offset", 0);
    auto v = must(cat.query(q));
    if (g_json) {
      Json j = Json::array();
      for (auto& u : v) j.push_back(u.to_json());
      print_json(j);
    } else {
      for (auto& u : v) print_catalog_unit_line(u);
    }
  } else if (sub == "show") {
    print_json(must(cat.preview(need(a, 1, "unit id"))));
  } else if (sub == "include" || sub == "exclude" || sub == "pin") {
    catalog::Override o;
    o.unit_id = need(a, 1, "unit id");
    o.action = sub;
    o.reason = a.get("reason");
    must(cat.set_override(o));
    if (!g_quiet) std::cout << "ok\n";
  } else if (sub == "import") {
    catalog::ImportOptions opts;
    opts.run_id = a.get("run");
    opts.dry_run = a.has("dry-run");
    opts.mode = a.get("mode", "selective");
    opts.include_project_siblings = a.has("include-project-siblings");
    opts.related_time_window_hours = a.get_int("time-window-hours", 0);
    opts.store_mode = a.get("store-mode", "copy");
    auto r = must(cat.import_selected(opts, progress_line, &g_cancel));
    if (!g_quiet) std::cerr << "\r\x1b[K";
    print_json(r);
  } else if (sub == "status") {
    print_json(must(cat.status()));
  } else if (sub == "eval") {
    return cmd_catalog_eval(rt, a);
  } else if (sub == "watch") {
    // A minimal "periodic watch of source folders for new exports": loops
    // scan() (already incremental/idempotent -- unchanged sources are a
    // per-source checkpoint skip, a new or changed file gets a new content
    // hash and is picked up automatically) on an interval until Ctrl-C.
    // Deliberately just a loop over the existing, already-resumable scan(),
    // not a new background subsystem: a real filesystem-event watch (inotify
    // et al) is future work, noted in the final report.
    std::vector<std::string> sources = a.all("source");
    if (sources.empty()) throw UsageError{"catalog watch needs at least one --source"};
    int interval = a.get_int("interval", cli_limit(rt, "catalog_watch_seconds"));
    if (interval < 1) throw UsageError{"catalog watch --interval must be >= 1 second"};
    catalog::ScanConfig cfg;
    cfg.sources = sources;
    while (!g_cancel.cancelled()) {
      auto r = cat.scan(cfg, g_quiet ? catalog::ProgressFn{} : progress_line, &g_cancel);
      if (!g_quiet) std::cerr << "\r\x1b[K";
      if (r) {
        if (g_json) print_json(*r);
        else std::cout << "watch: scanned " << json::get_int(*r, "new") << " new unit(s) of "
                       << json::get_int(*r, "units") << " total\n";
      } else if (!g_quiet) {
        std::cerr << "watch: scan error: " << r.error().message << "\n";
      }
      std::int64_t remaining_ms = static_cast<std::int64_t>(interval) * 1000;
      const auto slice_ms = cli_data().at("watch").at("sleep_slice_ms").get<std::int64_t>();
      while (remaining_ms > 0 && !g_cancel.cancelled()) {
        const auto duration_ms = std::min(remaining_ms, slice_ms);
        timespec duration{static_cast<time_t>(duration_ms / 1000),
                          static_cast<long>((duration_ms % 1000) * 1000000)};
        ::nanosleep(&duration, nullptr);
        remaining_ms -= duration_ms;
      }
    }
    if (!g_quiet) std::cout << "watch: stopped\n";
  } else {
    throw UsageError{"unknown catalog subcommand: " + sub};
  }
  return 0;
}

struct LoadedProfile {
  RuntimeProfile effective;
  Json overlay;
};

Result<LoadedProfile> load_profile_document(const fs::path& root, std::string_view domain) {
  LOOM_TRY_ASSIGN(auto base, RuntimeProfile::builtin(domain));
  Json document{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", domain}, {"overrides", Json::object()}};
  const auto path = root / "profiles" / (std::string(domain) + ".pack");
  std::error_code ec;
  const auto status = fs::symlink_status(path, ec);
  if (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory)
    return Error(Errc::Io, "cannot inspect runtime profile overlay: " + ec.message());
  if (ec != std::errc::not_a_directory && status.type() != fs::file_type::not_found) {
    LOOM_TRY_ASSIGN(auto bytes, fsutil::read_file(path));
    if (!utf8::is_valid(bytes)) return Error(Errc::InvalidArgument, "runtime profile overlay is not UTF-8");
    LOOM_TRY_ASSIGN(document, json::parse(bytes));
    LOOM_TRY_ASSIGN(base, base.with_overlay(document));
  }
  // One read supplies both effective values and saved user choices.
  return LoadedProfile{std::move(base), std::move(document)};
}

void merge_profile_choices(Json& target, const Json& changes) {
  for (auto it = changes.begin(); it != changes.end(); ++it) {
    if (it->is_object() && target.contains(it.key()) && target[it.key()].is_object())
      merge_profile_choices(target[it.key()], it.value());
    else target[it.key()] = it.value();
  }
}

// Keep the user's selected fields, including choices equal to the preset. A
// later patch can remove or replace them; removed fields must not reappear.
Json effective_profile_choices(const Json& choices, const Json& values) {
  Json selected = Json::object();
  for (auto it = choices.begin(); it != choices.end(); ++it) {
    if (!values.contains(it.key())) continue;
    const auto& value = values.at(it.key());
    selected[it.key()] = it->is_object() && value.is_object()
                            ? effective_profile_choices(it.value(), value) : value;
  }
  return selected;
}

void profile_patch_choices(const Json& document, std::set<std::string>& paths) {
  const auto* patch = json::find(document, "patch");
  if (!patch) return;
  for (const auto& operation : *patch) {
    const auto op = operation.at("op").get<std::string>();
    if (op == "add" || op == "replace" || op == "copy" || op == "move")
      paths.insert(operation.at("path").get<std::string>());
  }
}

bool profile_overlay_envelope(const Json& document) {
  return document.is_object() && json::get_string(document, "schema") == "loom.runtime_profile_overlay/1";
}

Json saved_profile_document(const RuntimeProfile& base, const Json& previous,
                            const Json& changes, const RuntimeProfile& effective) {
  Json choices = previous.value("overrides", Json::object());
  const bool envelope = profile_overlay_envelope(changes);
  merge_profile_choices(choices, envelope ? changes.value("overrides", Json::object()) : changes);
  choices = effective_profile_choices(choices, effective.values());
  auto selected = must(base.with_overrides(choices));
  Json patch = Json::diff(selected.values(), effective.values());
  std::set<std::string> explicit_paths;
  profile_patch_choices(previous, explicit_paths);
  if (envelope) profile_patch_choices(changes, explicit_paths);
  for (const auto& path : explicit_paths) {
    const Json::json_pointer pointer(path);
    if (effective.values().contains(pointer)) {
      // replace is also inert for arrays: add would insert another element.
      patch.push_back(Json{{"op", "replace"}, {"path", path}, {"value", effective.values().at(pointer)}});
    }
  }
  Json document{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", base.domain()},
                {"overrides", std::move(choices)}, {"patch", std::move(patch)}};
  auto checked = must(base.with_overlay(document));
  if (checked.hash() != effective.hash())
    throw Error(Errc::Internal, "saved profile does not reproduce the validated values");
  return document;
}

int cmd_profile(const fs::path& root, const Args& a) {
  const std::string op = a.pos.empty() ? "list" : a.pos[0];
  if (op == "list") { print_json(RuntimeProfile::domains()); return 0; }
  if (a.pos.size() < 2) throw UsageError{"profile needs a domain"};
  const std::string domain = a.pos[1];
  auto loaded = must(load_profile_document(root, domain));
  const auto& profile = loaded.effective;
  if (op == "inspect") { print_json(profile.inspection()); return 0; }
  if (op != "validate" && op != "save") throw UsageError{"unknown profile operation: " + op};
  Json changes = must(read_json_input(a));
  RuntimeProfile effective = profile;
  if (profile_overlay_envelope(changes)) {
    effective = must(profile.with_overlay(changes));
  } else effective = must(profile.with_overrides(changes));
  if (domain == "cli") must(validate_cli_consumers(effective));
  // Editor validation does not activate this file in Config's usage policy,
  // open a ledger, or authorize an operation.
  if (domain == "usage_policy") must(validate_usage_policy_options(effective.values()));
  if (op == "save") {
    auto base = must(RuntimeProfile::builtin(domain));
    auto doc = saved_profile_document(base, loaded.overlay, changes, effective);
    must(fsutil::atomic_write(root / "profiles" / (domain + ".pack"), json::dump(doc, 2) + "\n"));
  }
  print_json(effective.inspection());
  return 0;
}

int cmd_crypto(Runtime& rt, const Args& a) {
  std::string sub = a.pos.empty() ? "status" : a.pos[0];
  auto& v = rt.crypto();
  if (sub == "setup") {
    std::string p1 = read_stdin_secret("new password: ");
    if (::isatty(STDIN_FILENO)) {
      std::string p2 = read_stdin_secret("repeat: ");
      if (p1 != p2) throw UsageError{"passwords differ"};
    }
    must(v.setup(p1));
  } else if (sub == "unlock") {
    must(v.unlock(read_stdin_secret("password: ")));
  } else if (sub == "lock") {
    v.lock();
  } else if (sub != "status") {
    throw UsageError{"unknown crypto subcommand: " + sub};
  }
  print_json(v.status());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  // global options (before the command)
  std::optional<std::string> data_dir;
  std::string log_level;
  int i = 1;
  for (; i < argc; ++i) {
    std::string s = argv[i];
    if (s == "--json") g_json = true;
    else if (s == "--quiet" || s == "-q") g_quiet = true;
    else if (s == "--help" || s == "-h") {
      auto p = load_cli_profile(data_dir);
      if (!p) return fail(p.error());
      std::cout << p->values().at("help").get<std::string>();
      return 0;
    } else if (s == "--data-dir" && i + 1 < argc) {
      data_dir = argv[++i];
    } else if (s == "--log-level" && i + 1 < argc) {
      log_level = argv[++i];
    } else if (s.rfind("--data-dir=", 0) == 0) {
      data_dir = s.substr(11);
    } else if (s.rfind("--log-level=", 0) == 0) {
      log_level = s.substr(12);
    } else {
      break;
    }
  }
  auto cli_profile_r = load_cli_profile(data_dir);
  if (!cli_profile_r) return fail(cli_profile_r.error());
  g_cli_profile = *cli_profile_r;
  const auto& cli_values = cli_data();
  if (i >= argc) {
    std::cerr << cli_values.at("help").get<std::string>();
    return 2;
  }
  std::string cmd = json::get_string(cli_values.at("commands"), argv[i], argv[i]);
  Args args;
  try {
    const auto names = cli_values.at("flags").get<std::vector<std::string>>();
    args = parse(argc, argv, i + 1, std::set<std::string>(names.begin(), names.end()));
  } catch (const UsageError& e) {
    std::cerr << "usage error: " << e.msg << "\n(see loom --help)\n";
    return 2;
  }
  if (args.has("json")) g_json = true;
  if (args.has("quiet")) g_quiet = true;
  if (args.has("help") || cmd == "help") {
    std::cout << cli_values.at("help").get<std::string>();
    return 0;
  }
  if (cmd == "version") {
    Json v = Runtime::build_info();
    g_json ? print_json(v) : void(std::cout << "loom " << json::get_string(v, "version") << " (abi " << json::get_int(v, "abi")
                                            << ", sqlite " << json::get_string(v, "sqlite") << ")\n");
    return 0;
  }

  const auto& logging = cli_values.at("logging");
  if (log_level.empty()) log_level = logging.at("default_level").get<std::string>();
  const auto& levels = logging.at("levels");
  if (!levels.contains(log_level)) log_level = logging.at("unknown_level").get<std::string>();
  if (!levels.contains(log_level)) return fail(Error(Errc::InvalidArgument, "CLI logging fallback is not registered"));
  log::set_level(static_cast<log::Level>(levels.at(log_level).get<int>()));

  try {
    if (!args.pos.empty()) {
      const auto* aliases = json::find(cli_values.at("subcommands"), cmd);
      if (aliases) args.pos[0] = json::get_string(*aliases, args.pos[0], args.pos[0]);
    }
    if (cmd == "profile") return cmd_profile(cli_data_root_read_only(data_dir), args);
    RuntimeOptions ro;
    ro.data_dir = data_dir;
    ro.start_workers = false;
    auto rt_r = Runtime::open(ro);
    if (!rt_r) return fail(rt_r.error());
    Runtime& rt = **rt_r;
    if (cmd == "init") {
      Json info = rt.info();
      if (g_json) print_json(info);
      else std::cout << "data directory: " << json::get_string(info, "data_dir") << "\n"
                     << "database:       " << json::get_string(info["paths"], "db") << "\n"
                     << "fts5:           " << (json::get_bool(info, "fts5") ? "yes" : "no") << "\n";
      return 0;
    }
    using Handler = int (*)(Runtime&, const Args&);
    const std::map<std::string, Handler> handlers{
      {"conv", cmd_conv},
      {"msg", cmd_msg},
      {"chat", cmd_chat},
      {"import", cmd_import},
      {"export", cmd_export},
      {"graph", cmd_graph},
      {"context", cmd_context},
      {"search", cmd_search},
      {"semantic", cmd_semantic},
      {"memory", cmd_memory},
      {"config", cmd_config},
      {"secret", cmd_secret},
      {"models", cmd_models},
      {"tasks", cmd_tasks},
      {"provenance", cmd_provenance},
      {"sources", cmd_sources},
      {"artifacts", cmd_artifacts},
      {"archive", cmd_archive},
      {"catalog", cmd_catalog},
      {"knowledge", cmd_knowledge},
      {"crypto", cmd_crypto} };
    if (auto handler = handlers.find(cmd); handler != handlers.end()) return handler->second(rt, args);
    std::cerr << "unknown command: " << cmd << "\n(see loom --help)\n";
    return 2;
  } catch (const UsageError& e) {
    std::cerr << "usage error: " << e.msg << "\n(see loom --help)\n";
    return 2;
  } catch (const Error& e) {
    return fail(e);
  } catch (const std::exception& e) {
    return fail(Error(Errc::Internal, e.what()));
  }
}
