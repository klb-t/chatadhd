// loom — command-line client over the Loom C++ API.
//
//   loom [--data-dir DIR] [--json] [--quiet] <command> [args]
//
// Every command opens one Runtime on the data directory (shared with the
// ChatADHD app), without background workers, and closes it on exit.
// --json prints machine-readable JSON; otherwise output is for humans.
// Secrets and passwords are read from stdin, never from argv.
#include <unistd.h>

#include <termios.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "loom/archive.h"
#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/crypto.h"
#include "loom/db.h"
#include "loom/graph_engine.h"
#include "loom/graph_memory.h"
#include "loom/importer.h"
#include "loom/log.h"
#include "loom/media_providers.h"
#include "loom/memory_engine.h"
#include "loom/provenance.h"
#include "loom/providers.h"
#include "loom/runtime.h"
#include "loom/semantic_worker.h"
#include "loom/tasks.h"
#include "loom/util/cancel.h"
#include "loom/util/fs.h"
#include "loom/util/json.h"
#include "loom/util/utf8.h"

using namespace loom;

namespace {

const char* kUsage = R"(loom — Loom kernel command line (ChatADHD engine)

Usage: loom [--data-dir DIR] [--json] [--quiet] <command> [args]

Global options:
  --data-dir DIR   data directory (default: $CHATADHD_DATA, sentinel search, ~/.chatadhd)
  --json           print JSON instead of text
  --quiet          no progress output on stderr
  --log-level L    debug | info | warning | error (default: warning)

Commands:
  init                                  create/open the data directory, print paths
  version                               build info
  conv list [--limit N]                 conversations (newest first)
  conv create [TITLE]
  conv show ID [--all]                  messages (--all: versions, excluded, deleted)
  conv rename ID TITLE
  conv delete ID
  msg edit ID TEXT                      new version (old one kept)
  msg restore ID                        make ID the active version
  msg versions ID                       all versions of ID's group
  msg status ID active|excluded|version|deleted
  chat [--conv ID] [--model M] [--depth N] [--effort E] [--web] MESSAGE...
                                        stream an answer ("-" reads the message from stdin)
  import PATH [--title T] [--force]     universal importer (ChatGPT/Claude/HTML/MD/...)
  export CONV_ID [--format json|markdown|text|html] [--out FILE]
  graph nodes [--kind K] [--limit N]
  graph edges [--node ID] [--type T]
  graph expand ID... [--depth N]
  graph reindex [CONV_ID]
  graph stats
  context TEXT [--depth N] [--max-tokens N]      ContextSet for a prompt
  search QUERY [--limit N] [--conv ID] [--all]   full-text search (FTS5/BM25)
  semantic status|pause|resume|wake|run         background analysis (run = drain now)
  memory list | add CONTENT [--parent ID] | delete ID | context
  config get [KEY] | set KEY VALUE              VALUE is JSON (plain text = string)
  secret set KEY | has KEY | delete KEY | list  set reads the value from stdin
  models [--refresh]
  tasks list [--kind K] [--status S] [--limit N] | show ID | resume | cancel ID
  provenance ID                         sources + transformation records of an id
  sources [--limit N]
  artifacts list [--kind K] | show ID [--out FILE]
  archive run [--source PATH]... [--repo DIR] [--out DIR] [--seed TERM]...
              [--project NAME] [--max-passes N] [--max-new-terms N] [--max-hits N]
              [--rounds N] [--exclude FRAGMENT]... [--no-git] [--no-code]
              [--include-db] [--llm auto|off] [--force] [--config FILE.json]
  archive status [RUN_ID]
  crypto status | setup | unlock | lock        passwords are read from stdin

Archive example (the self-hosting run):
  loom --data-dir /tmp/loom-selfhost archive run --repo . --out docs/selfhost \
       --seed ChatADHD --seed Loom --seed provenance --seed graph --seed importer
)";

// ── argument parsing ────────────────────────────────────────────────
const std::set<std::string>& flag_names() {
  static const std::set<std::string> k = {"json", "quiet", "force", "all", "refresh", "no-git", "no-code",
                                          "include-db", "web", "help", "deep"};
  return k;
}

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

Args parse(int argc, char** argv, int start) {
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
      if (flag_names().count(k)) {
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

std::string one_line(std::string_view s, std::size_t max = 100) {
  std::string out;
  for (char c : s) out.push_back(c == '\n' || c == '\r' || c == '\t' ? ' ' : c);
  if (utf8::length(out) > max) out = std::string(utf8::prefix(out, max - 1)) + "…";
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

void progress_line(std::string_view stage, std::int64_t cur, std::int64_t total, std::string_view msg) {
  if (g_quiet) return;
  std::cerr << "\r\x1b[K[" << stage << "] ";
  if (total > 0) std::cerr << cur << "/" << total << " ";
  std::cerr << one_line(msg, 70) << std::flush;
}

// ── commands ────────────────────────────────────────────────────────
void print_conv_line(const Conversation& c) {
  std::cout << c.id << "  " << c.updated.substr(0, 16) << "  " << one_line(c.title, 70) << "\n";
}

void print_msg(const Message& m) {
  std::cout << "── " << m.role << " · " << m.id << " · " << m.created.substr(0, 19)
            << (m.status != "active" ? " · " + m.status : "")
            << (m.version_num > 1 ? " · v" + std::to_string(m.version_num) : "") << "\n"
            << m.text << "\n\n";
}

int cmd_conv(Runtime& rt, const Args& a) {
  std::string sub = need(a, 0, "conv subcommand");
  Database& db = rt.db();
  if (sub == "list") {
    auto convs = must(db.list_convs(a.get_int("limit", 50)));
    if (g_json) {
      Json j = Json::array();
      for (const auto& c : convs) j.push_back(c.to_json());
      print_json(j);
    } else {
      for (const auto& c : convs) print_conv_line(c);
    }
  } else if (sub == "create") {
    auto c = must(db.create_conv(a.pos.size() > 1 ? a.pos[1] : "New Chat"));
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
  if (a.has("depth")) o.context_depth = a.get_int("depth", 2);
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

int cmd_import(Runtime& rt, const Args& a) {
  ImportOptions o;
  if (a.has("title")) o.title = a.get("title");
  o.force = a.has("force");
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
  }
  return 0;
}

int cmd_export(Runtime& rt, const Args& a) {
  std::string id = need(a, 0, "conversation id");
  std::string fmt = a.get("format", "markdown");
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
    auto nodes = must(db.list_nodes(kind, a.get_int("limit", 200)));
    if (g_json) {
      Json j = Json::array();
      for (const auto& n : nodes) j.push_back(n.to_json());
      print_json(j);
    } else {
      for (const auto& n : nodes) std::cout << n.id << "  " << n.kind << "  " << one_line(n.label, 80) << "\n";
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
    print_json(must(rt.graph_memory().expand(seeds, a.get_int("depth", 1))));
  } else if (sub == "reindex") {
    int n = a.pos.size() > 1 ? rt.graph().reindex_conversation(a.pos[1]) : rt.graph().reindex_all();
    g_json ? print_json(Json{{"reindexed", n}}) : void(std::cout << "reindexed " << n << " messages\n");
  } else if (sub == "stats") {
    auto& c = rt.db().conn();
    auto lk = rt.db().lock();
    Json j = Json::object();
    for (const char* t : {"conversations", "messages", "nodes", "links"}) {
      j[t] = must(c.query_int(std::string("SELECT COUNT(*) FROM ") + t)).value_or(0);
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
  req.depth = a.get_int("depth", 0);
  req.max_tokens = a.get_int("max-tokens", 4000);
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
  o.limit = a.get_int("limit", 20);
  if (a.has("conv")) o.conv_id = a.get("conv");
  o.include_inactive = a.has("all");
  auto r = must(rt.db().search_messages(q, o));
  if (g_json) {
    print_json(r.to_json());
  } else {
    std::cout << r.hits.size() << " hits (" << r.mode << ")\n";
    for (const auto& h : r.hits) {
      std::cout << h.message.id << "  " << h.message.conv_id << "  " << json::format_float_py(std::round(h.score * 100) / 100)
                << "  " << one_line(h.snippet, 100) << "\n";
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
                  << "  " << one_line(n.content, 80) << "\n";
      }
    }
  } else if (sub == "add") {
    std::optional<std::string> parent;
    if (a.has("parent")) parent = a.get("parent");
    std::string id = must(m.add_node(need(a, 1, "content"), parent, a.get("type", "text")));
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
  std::cout << t.id << "  " << t.kind << "  " << t.status << "  " << t.updated.substr(0, 19)
            << (t.error.empty() ? "" : "  " + one_line(t.error, 60)) << "\n";
}

int cmd_tasks(Runtime& rt, const Args& a) {
  std::string sub = a.pos.empty() ? "list" : a.pos[0];
  auto& te = rt.tasks();
  if (sub == "list") {
    TaskFilter f;
    if (a.has("kind")) f.kind = a.get("kind");
    if (a.has("status")) f.status = a.get("status");
    f.limit = a.get_int("limit", 50);
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
    f.limit = 1000;
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
  auto v = must(rt.provenance().list_sources(a.get_int("limit", 100)));
  if (g_json) {
    Json j = Json::array();
    for (const auto& s : v) j.push_back(s.to_json());
    print_json(j);
  } else {
    for (const auto& s : v) {
      std::cout << s.id << "  " << s.kind << "  " << s.format << "  " << s.blob_hash.substr(0, 12) << "  "
                << one_line(s.uri, 70) << "\n";
    }
  }
  return 0;
}

int cmd_artifacts(Runtime& rt, const Args& a) {
  std::string sub = a.pos.empty() ? "list" : a.pos[0];
  if (sub == "list") {
    std::string kind = a.get("kind");
    auto v = must(rt.provenance().list_artifacts(a.get_int("limit", 100),
                                                 kind.empty() ? std::nullopt : std::optional<std::string_view>(kind)));
    if (g_json) {
      Json j = Json::array();
      for (const auto& x : v) j.push_back(x.to_json());
      print_json(j);
    } else {
      for (const auto& x : v) {
        std::cout << x.id << "  " << x.kind << "  " << x.created.substr(0, 19) << "  " << x.title << "\n";
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
  archive::ArchiveConfig cfg = must(archive::ArchiveConfig::from_json(cj));
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
  cfg = must(archive::ArchiveConfig::from_json(cfg.to_json()));  // validate + clamp
  if (cfg.sources.empty() && !cfg.repo && !cfg.include_db) throw UsageError{"archive run needs --source, --repo or --include-db"};
  std::signal(SIGINT, sigint_handler);
  auto r = must(rt.archive().run(cfg, progress_line, &g_cancel));
  if (!g_quiet) std::cerr << "\r\x1b[K";
  if (g_json) {
    print_json(r.to_json());
    return r.status == "done" ? 0 : 4;
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
  std::string log_level = "warning";
  int i = 1;
  for (; i < argc; ++i) {
    std::string s = argv[i];
    if (s == "--json") g_json = true;
    else if (s == "--quiet" || s == "-q") g_quiet = true;
    else if (s == "--help" || s == "-h") {
      std::cout << kUsage;
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
  if (i >= argc) {
    std::cerr << kUsage;
    return 2;
  }
  std::string cmd = argv[i];
  Args args;
  try {
    args = parse(argc, argv, i + 1);
  } catch (const UsageError& e) {
    std::cerr << "usage error: " << e.msg << "\n(see loom --help)\n";
    return 2;
  }
  if (args.has("json")) g_json = true;
  if (args.has("quiet")) g_quiet = true;
  if (args.has("help") || cmd == "help") {
    std::cout << kUsage;
    return 0;
  }
  if (cmd == "version") {
    Json v = Runtime::build_info();
    g_json ? print_json(v) : void(std::cout << "loom " << json::get_string(v, "version") << " (abi " << json::get_int(v, "abi")
                                            << ", sqlite " << json::get_string(v, "sqlite") << ")\n");
    return 0;
  }

  if (log_level == "debug") log::set_level(log::Level::Debug);
  else if (log_level == "info") log::set_level(log::Level::Info);
  else if (log_level == "error") log::set_level(log::Level::Error);
  else log::set_level(log::Level::Warning);

  RuntimeOptions ro;
  ro.data_dir = data_dir;
  ro.start_workers = false;
  auto rt_r = Runtime::open(ro);
  if (!rt_r) return fail(rt_r.error());
  Runtime& rt = **rt_r;

  try {
    if (cmd == "init") {
      Json info = rt.info();
      if (g_json) print_json(info);
      else std::cout << "data directory: " << json::get_string(info, "data_dir") << "\n"
                     << "database:       " << json::get_string(info["paths"], "db") << "\n"
                     << "fts5:           " << (json::get_bool(info, "fts5") ? "yes" : "no") << "\n";
      return 0;
    }
    if (cmd == "conv") return cmd_conv(rt, args);
    if (cmd == "msg") return cmd_msg(rt, args);
    if (cmd == "chat") return cmd_chat(rt, args);
    if (cmd == "import") return cmd_import(rt, args);
    if (cmd == "export") return cmd_export(rt, args);
    if (cmd == "graph") return cmd_graph(rt, args);
    if (cmd == "context") return cmd_context(rt, args);
    if (cmd == "search") return cmd_search(rt, args);
    if (cmd == "semantic") return cmd_semantic(rt, args);
    if (cmd == "memory") return cmd_memory(rt, args);
    if (cmd == "config") return cmd_config(rt, args);
    if (cmd == "secret") return cmd_secret(rt, args);
    if (cmd == "models") return cmd_models(rt, args);
    if (cmd == "tasks") return cmd_tasks(rt, args);
    if (cmd == "provenance") return cmd_provenance(rt, args);
    if (cmd == "sources") return cmd_sources(rt, args);
    if (cmd == "artifacts") return cmd_artifacts(rt, args);
    if (cmd == "archive") return cmd_archive(rt, args);
    if (cmd == "crypto") return cmd_crypto(rt, args);
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
