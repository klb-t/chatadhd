// The operations of the application as actions (action_ops.h): the executors behind search.find, graph.related, memory.*, conversation.*,
// message.*, export/import, storage.connect, ports.*, usage.*, models.*, semantic.*, tasks.*, help.* and the rest. Each is a thin layer over
// the engine that does the work (the calls the C ABI makes); what it says back is written from runtime/chat_ops.pack in the language of the run.
#include "loom/action_ops.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>

#include "loom/action_overlay.h"
#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/economy.h"
#include "loom/event_bus.h"
#include "loom/graph_engine.h"
#include "loom/graph_entry.h"
#include "loom/graph_memory.h"
#include "loom/importer.h"
#include "loom/log.h"
#include "loom/memory_engine.h"
#include "loom/path_policy.h"
#include "loom/port_profile.h"
#include "loom/provenance.h"
#include "loom/providers.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/semantic_worker.h"
#include "loom/settings_fields.h"
#include "loom/storage_backend.h"
#include "loom/tasks.h"
#include "loom/usage_policy.h"
#include "loom/github_sync.h"
#include "loom/crypto.h"
#include "loom/media_providers.h"
#include "loom/re/regex.h"
#include "loom/util/fs.h"
#include "loom/util/ids.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom::actions {

namespace {

// ── the context of one call ─────────────────────────────────────────

struct Ctx {
  Runtime& rt;
  const StepCall& c;
  Json cfg;
  std::string lang;

  Ctx(Runtime& r, const StepCall& call) : rt(r), c(call) {
    if (auto p = RuntimeProfile::load("chat_ops", rt.paths().root)) cfg = p->values();
    else cfg = Json::object();
    lang = c.lang.empty() ? std::string("en") : c.lang.substr(0, c.lang.find_first_of("-_"));
  }

  Json arg(const char* name) const {
    if (const Json* v = json::find(c.with, name); v && !v->is_null()) return *v;
    if (const Json* v = json::find(c.args, name); v && !v->is_null()) return *v;
    return Json(nullptr);
  }
  bool has(const char* name) const { return !arg(name).is_null(); }
  std::string str(const char* name, const std::string& fallback = std::string()) const {
    const Json v = arg(name);
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number() || v.is_boolean()) return json::dump(v);
    return fallback;
  }
  std::int64_t integer(const char* name, std::int64_t fallback) const {
    const Json v = arg(name);
    if (v.is_number_integer()) return v.get<std::int64_t>();
    if (v.is_number()) return static_cast<std::int64_t>(v.get<double>());
    if (v.is_string()) {
      try {
        return std::stoll(v.get<std::string>());
      } catch (...) {
      }
    }
    return fallback;
  }
  std::int64_t limit(const char* key, std::int64_t fallback) const { return std::max<std::int64_t>(1, json::get_int(cfg.value("limits", Json::object()), key, fallback)); }

  // The element the step acts on: its id (a conversation, a message ...) or empty.
  std::string element_id() const { return json::get_string(c.element, "id"); }
  std::string element_class() const { return json::get_string(c.element, "class"); }
  std::string conv_id() const {
    if (element_class() == "conversation" && !element_id().empty()) return element_id();
    return json::get_string(c.context, "conv_id");
  }

  std::string say(const std::string& id, const Json& vars = Json::object()) const {
    const Json* texts = json::find(cfg, "texts");
    if (!texts || !texts->is_object()) return id;
    const Json* table = json::find(*texts, lang);
    const Json* t = table ? json::find(*table, id) : nullptr;
    if (!t || !t->is_string()) {
      table = json::find(*texts, "en");
      t = table ? json::find(*table, id) : nullptr;
    }
    if (!t || !t->is_string()) return id;
    auto rendered = render_profile_template(t->get<std::string>(), vars);
    return rendered ? *rendered : t->get<std::string>();
  }
  // A {en, pl} object in the language of the run.
  std::string pick(const Json& v) const {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_object())
      for (const std::string& k : {lang, std::string("en")})
        if (const Json* s = json::find(v, k); s && s->is_string()) return s->get<std::string>();
    return std::string();
  }

  std::string fold(std::string_view text) const {
    const Json map = cfg.value("fold", Json::object());
    std::string out;
    for (char32_t cp : utf8::decode(utf8::to_lower(text))) {
      const std::string one = utf8::encode(cp);
      const Json* repl = json::find(map, one);
      out += repl && repl->is_string() ? repl->get<std::string>() : one;
    }
    return out;
  }
};

StepOutcome fail(Errc code, std::string message) {
  StepOutcome o;
  o.state = StepOutcome::State::Failed;
  o.error = Error(code, std::move(message));
  return o;
}
StepOutcome fail(const Error& e) {
  StepOutcome o;
  o.state = StepOutcome::State::Failed;
  o.error = e;
  return o;
}

// The answer: a text for the conversation and the data behind it.
StepOutcome answer(std::string text, Json data = nullptr) {
  StepOutcome o;
  o.state = StepOutcome::State::Ok;
  llm::Part part;
  part.kind = "text";
  part.text = std::move(text);
  o.parts.push_back(std::move(part));
  o.data = std::move(data);
  return o;
}

std::string join_lines(const std::vector<std::string>& lines) {
  std::string out;
  for (const auto& l : lines) out += (out.empty() ? "" : "\n") + l;
  return out;
}

// A number in words: 20 and not 20.0, 3.5 as it is.
std::string number_text(double v) {
  if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) return std::to_string(static_cast<long long>(v));
  std::string s = json::dump(Json(v));
  return s;
}

std::string trim_quotes(std::string v) {
  static const std::vector<std::string> marks = {"\"", "'", "`", "„", "”", "“", "«", "»", ".", "!", " ", "\t", "\n"};
  bool changed = true;
  while (changed && !v.empty()) {
    changed = false;
    for (const auto& m : marks) {
      if (v.size() >= m.size() && v.compare(0, m.size(), m) == 0 && m != "." && m != "!") {
        v.erase(0, m.size());
        changed = true;
      }
      if (v.size() >= m.size() && v.compare(v.size() - m.size(), m.size(), m) == 0) {
        v.erase(v.size() - m.size());
        changed = true;
      }
    }
  }
  return v;
}

std::string clip(const std::string& text, std::size_t codepoints) {
  std::string flat;
  for (char ch : text) flat += (ch == '\n' || ch == '\r' || ch == '\t') ? ' ' : ch;
  const std::string_view cut = utf8::prefix(flat, codepoints);
  return std::string(cut) + (cut.size() < flat.size() ? "…" : "");
}

bool contains_ci(const std::string& hay, const std::string& needle_lower) { return utf8::to_lower(hay).find(needle_lower) != std::string::npos; }

bool looks_like_id(const std::string& s, const char* prefix = nullptr) {
  std::string rest = s;
  if (prefix && !rest.empty()) {
    if (rest.rfind(prefix, 0) != 0) return false;
    rest = rest.substr(std::string(prefix).size());
  }
  if (rest.size() != 12) return false;
  return std::all_of(rest.begin(), rest.end(), [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)) != 0; });
}

// ── conversations ───────────────────────────────────────────────────

StepOutcome conversations_list(Ctx& x) {
  const int limit = static_cast<int>(x.integer("limit", x.limit("list", 20)));
  auto convs = x.rt.db().list_convs(limit);
  if (!convs) return fail(convs.error());
  std::vector<std::string> lines;
  Json items = Json::array();
  for (const auto& cv : *convs) {
    lines.push_back("• " + x.say("conv_line", Json{{"title", cv.title}, {"id", cv.id}, {"updated", cv.updated.substr(0, 10)}}));
    items.push_back(Json{{"id", cv.id}, {"title", cv.title}, {"updated", cv.updated}});
  }
  if (lines.empty()) return answer(x.say("conversations_none"), Json{{"items", items}});
  return answer(x.say("conversations_head", Json{{"count", lines.size()}}) + "\n" + join_lines(lines), Json{{"items", items}});
}

StepOutcome conversation_create(Ctx& x) {
  std::string title = trim_quotes(x.str("title"));
  if (title.empty()) title = "New Chat";
  auto made = x.rt.db().create_conv(title);
  if (!made) return fail(made.error());
  x.rt.bus().emit(events::kConvCreated, Json{{"id", made->id}, {"title", made->title}});
  return answer(x.say("conversation_created", Json{{"title", made->title}, {"id", made->id}}), Json{{"id", made->id}, {"title", made->title}});
}

StepOutcome conversation_rename(Ctx& x) {
  const std::string id = x.conv_id();
  const std::string title = trim_quotes(x.str("title"));
  if (id.empty()) return fail(Errc::InvalidArgument, "no conversation named");
  if (title.empty()) return fail(Errc::InvalidArgument, "title is required");
  ConvPatch patch;
  patch.title = title;
  if (auto st = x.rt.db().update_conv(id, patch); !st) return fail(st.error());
  return answer(x.say("conversation_renamed", Json{{"title", title}, {"id", id}}), Json{{"id", id}, {"title", title}});
}

StepOutcome conversation_delete(Ctx& x) {
  const std::string id = x.conv_id();
  if (id.empty()) return fail(Errc::InvalidArgument, "no conversation named");
  auto existing = x.rt.db().get_conv(id);
  if (!existing) return fail(existing.error());
  if (!*existing) return fail(Errc::NotFound, "conversation not found: " + id);
  const std::string title = (*existing)->title;
  if (auto st = x.rt.db().delete_conv(id); !st) return fail(st.error());
  return answer(x.say("conversation_deleted", Json{{"title", title}, {"id", id}}), Json{{"id", id}, {"title", title}, {"deleted", true}});
}

// ── messages ────────────────────────────────────────────────────────

Result<Message> message_of(Ctx& x) {
  std::string id = x.element_class() == "message" ? x.element_id() : x.str("id");
  if (id.empty()) id = x.element_id();
  if (id.empty()) return Error(Errc::InvalidArgument, "no message named");
  auto m = x.rt.db().get_msg(id);
  if (!m) return m.error();
  if (!*m) return Error(Errc::NotFound, "message not found: " + id);
  return **m;
}

StepOutcome message_edit(Ctx& x) {
  auto m = message_of(x);
  if (!m) return fail(m.error());
  const std::string text = x.str("text");
  if (utf8::is_blank(text)) return fail(Errc::InvalidArgument, "the new text is required");
  auto edited = x.rt.db().edit_msg(m->id, text);
  if (!edited) return fail(edited.error());
  if (!*edited) return fail(Errc::NotFound, "message not found: " + m->id);
  auto fresh = x.rt.db().get_msg(**edited);
  if (!fresh || !*fresh) return fail(Errc::Internal, "edited message vanished");
  x.rt.bus().emit(events::kMsgUpdated, Json{{"id", (*fresh)->id}, {"previous_id", m->id}, {"conv_id", (*fresh)->conv_id}});
  return answer(x.say("message_edited", Json{{"id", (*fresh)->id}, {"previous", m->id}, {"version", (*fresh)->version_num}}),
                Json{{"id", (*fresh)->id}, {"previous_id", m->id}, {"version_num", (*fresh)->version_num}, {"version_group_id", (*fresh)->version_group_id ? Json(*(*fresh)->version_group_id) : Json(nullptr)}});
}

Result<std::vector<Message>> versions_of(Ctx& x, const Message& m) {
  if (!m.version_group_id) return std::vector<Message>{m};
  return x.rt.db().get_versions(*m.version_group_id);
}

StepOutcome message_versions(Ctx& x) {
  auto m = message_of(x);
  if (!m) return fail(m.error());
  auto versions = versions_of(x, *m);
  if (!versions) return fail(versions.error());
  const std::size_t chars = static_cast<std::size_t>(x.limit("snippet_chars", 160));
  std::vector<std::string> lines;
  Json items = Json::array();
  for (const auto& v : *versions) {
    lines.push_back("• " + x.say("version_line", Json{{"n", v.version_num}, {"id", v.id}, {"status", v.status}, {"text", clip(v.text, chars)}}));
    items.push_back(Json{{"id", v.id}, {"version_num", v.version_num}, {"status", v.status}});
  }
  return answer(x.say("versions_head", Json{{"count", lines.size()}}) + "\n" + join_lines(lines), Json{{"items", items}});
}

StepOutcome message_restore(Ctx& x) {
  auto m = message_of(x);
  if (!m) return fail(m.error());
  auto versions = versions_of(x, *m);
  if (!versions) return fail(versions.error());
  std::vector<Message> list = *versions;
  std::sort(list.begin(), list.end(), [](const Message& a, const Message& b) { return a.version_num < b.version_num; });
  const std::string which = x.fold(x.str("version", "previous"));
  const Message* target = nullptr;
  if (looks_like_id(x.str("version"), "m_")) {
    for (const auto& v : list) if (v.id == x.str("version")) target = &v;
  } else if (!which.empty() && std::all_of(which.begin(), which.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; })) {
    const std::int64_t n = std::stoll(which);
    for (const auto& v : list) if (v.version_num == n) target = &v;
  } else {
    // "previous": the version before the one that is active now (or before the message named when none is).
    std::int64_t current = m->version_num;
    for (const auto& v : list) if (v.status == "active") current = v.version_num;
    for (const auto& v : list) if (v.version_num < current) target = &v;
  }
  if (!target) return fail(Errc::NotFound, x.say("version_missing"));
  auto restored = x.rt.db().restore_version(target->id);
  if (!restored) return fail(restored.error());
  if (!*restored) return fail(Errc::NotFound, "message not found: " + target->id);
  x.rt.bus().emit(events::kMsgUpdated, Json{{"id", target->id}, {"restored", true}});
  return answer(x.say("message_restored", Json{{"id", target->id}, {"n", target->version_num}}), Json{{"id", target->id}, {"version_num", target->version_num}});
}

StepOutcome message_set_status(Ctx& x) {
  auto m = message_of(x);
  if (!m) return fail(m.error());
  const std::string status = x.str("status");
  if (status != "active" && status != "excluded" && status != "deleted") return fail(Errc::InvalidArgument, "status must be active, excluded or deleted");
  if (auto st = x.rt.db().set_msg_status(m->id, status); !st) return fail(st.error());
  x.rt.bus().emit(events::kMsgUpdated, Json{{"id", m->id}, {"status", status}});
  return answer(x.say("message_status_" + status, Json{{"id", m->id}}), Json{{"id", m->id}, {"status", status}, {"previous", m->status}});
}

StepOutcome message_weight(Ctx& x) {
  auto m = message_of(x);
  if (!m) return fail(m.error());
  double weight = 0;
  bool have = false;
  const Json given = x.arg("weight");
  if (given.is_number()) {
    weight = given.get<double>();
    have = true;
  } else if (given.is_string()) {
    std::string s = given.get<std::string>();
    std::replace(s.begin(), s.end(), ',', '.');
    try {
      std::size_t used = 0;
      weight = std::stod(s, &used);
      have = used == s.size();
    } catch (...) {
    }
  }
  if (!have) {
    const std::string word = x.fold(x.str("importance"));
    const Json* table = json::find(x.cfg, "weights");
    if (table && table->is_array())
      for (const auto& row : *table)
        for (const auto& lang : {std::string("en"), std::string("pl")})
          for (const auto& w : row.value("words", Json::object()).value(lang, Json::array()))
            if (x.fold(w.get<std::string>()) == word) {
              weight = json::get_number(row, "weight", 1.0);
              have = true;
            }
  }
  if (!have || !std::isfinite(weight) || weight < 0) return fail(Errc::InvalidArgument, x.say("weight_unknown"));
  MsgPatch patch;
  patch.weight = weight;
  if (auto st = x.rt.db().update_msg(m->id, patch); !st) return fail(st.error());
  x.rt.bus().emit(events::kMsgUpdated, Json{{"id", m->id}, {"fields", Json{{"weight", weight}}}});
  return answer(x.say("message_weighted", Json{{"id", m->id}, {"weight", number_text(weight)}}), Json{{"id", m->id}, {"weight", weight}, {"previous", m->weight}});
}

// ── search, graph, memory ───────────────────────────────────────────

StepOutcome search_find(Ctx& x) {
  const std::string query = trim_quotes(x.str("query"));
  if (query.empty()) return fail(Errc::InvalidArgument, "query is required");
  SearchOptions o;
  o.limit = static_cast<int>(x.integer("limit", x.limit("search", 10)) * 5);  // several hits per conversation are folded into one line
  if (x.has("conv_id")) o.conv_id = x.str("conv_id");
  auto found = x.rt.db().search_messages(query, o);
  if (!found) return fail(found.error());
  const std::size_t chars = static_cast<std::size_t>(x.limit("snippet_chars", 160));
  const std::size_t want = static_cast<std::size_t>(x.integer("limit", x.limit("search", 10)));
  std::vector<std::string> order;
  std::map<std::string, Json> best;
  std::map<std::string, int> counts;
  for (const auto& hit : found->hits) {
    const auto& m = hit.message;
    // What the user typed to ask for the search, the proposal and the answers are traffic of the commands, not content of the conversations.
    if (m.metadata.is_object() && (m.metadata.contains("routed") || m.metadata.contains("action_proposal") || m.metadata.contains("action_run"))) continue;
    if (!counts.count(m.conv_id)) {
      order.push_back(m.conv_id);
      auto cv = x.rt.db().get_conv(m.conv_id);
      const std::string title = cv && *cv ? (*cv)->title : m.conv_id;
      best[m.conv_id] = Json{{"conv_id", m.conv_id}, {"title", title}, {"message_id", m.id}, {"role", m.role},
                             {"snippet", clip(hit.snippet.empty() ? m.text : hit.snippet, chars)}, {"score", hit.score}};
    }
    ++counts[m.conv_id];
  }
  std::vector<std::string> lines;
  Json items = Json::array();
  for (const auto& id : order) {
    if (items.size() >= want) break;
    Json row = best[id];
    row["hits"] = counts[id];
    lines.push_back("• " + x.say("search_line", Json{{"title", row["title"]}, {"id", id}, {"snippet", row["snippet"]}, {"hits", counts[id]}}));
    items.push_back(std::move(row));
  }
  if (lines.empty()) return answer(x.say("search_none", Json{{"query", query}}), Json{{"query", query}, {"items", items}, {"mode", found->mode}});
  return answer(x.say("search_head", Json{{"query", query}, {"count", lines.size()}}) + "\n" + join_lines(lines), Json{{"query", query}, {"items", items}, {"mode", found->mode}});
}

StepOutcome graph_related(Ctx& x) {
  const std::string name = trim_quotes(x.str("name"));
  if (name.empty()) return fail(Errc::InvalidArgument, "name is required");
  const std::int64_t depth = std::clamp<std::int64_t>(x.integer("depth", 1), 1, x.limit("graph_depth_max", 3));
  const std::size_t max_edges = static_cast<std::size_t>(x.limit("graph_edges", 60));
  auto& db = x.rt.db();
  // The nodes the name stands for: the exact label first, else every label that contains it.
  std::vector<Node> seeds;
  if (auto exact = db.find_node(name); exact && *exact) seeds.push_back(**exact);
  if (seeds.empty()) {
    auto all = db.list_nodes(std::nullopt, static_cast<int>(x.limit("scan_nodes", 5000)));
    if (!all) return fail(all.error());
    const std::string needle = utf8::to_lower(name);
    for (const auto& n : *all)
      if (contains_ci(n.label, needle) && n.kind != "message") seeds.push_back(n);
    std::sort(seeds.begin(), seeds.end(), [](const Node& a, const Node& b) { return a.label.size() < b.label.size(); });
    if (seeds.size() > 5) seeds.resize(5);
  }
  if (seeds.empty()) return answer(x.say("graph_none", Json{{"name", name}}), Json{{"name", name}, {"nodes", Json::array()}, {"edges", Json::array()}});
  std::map<std::string, Node> known;
  for (const auto& s : seeds) known[s.id] = s;
  std::set<std::string> done;
  std::vector<std::string> frontier;
  for (const auto& s : seeds) frontier.push_back(s.id);
  Json edges = Json::array();
  std::vector<std::string> lines;
  auto label_of = [&](const std::string& id) {
    auto it = known.find(id);
    if (it == known.end()) {
      auto n = db.get_node(id);
      if (n && *n) it = known.emplace(id, **n).first;
    }
    return it == known.end() ? id : (it->second.label.empty() ? id : it->second.label);
  };
  for (std::int64_t level = 0; level < depth && !frontier.empty() && edges.size() < max_edges; ++level) {
    std::vector<std::string> next;
    for (const auto& id : frontier) {
      if (!done.insert(id).second) continue;
      auto links = db.get_links(id);
      if (!links) return fail(links.error());
      for (const auto& l : *links) {
        if (edges.size() >= max_edges) break;
        const std::string other = l.src == id ? l.dst : l.src;
        lines.push_back("• " + x.say(l.src == id ? "graph_edge_out" : "graph_edge_in",
                                     Json{{"from", label_of(id)}, {"type", l.link_type}, {"to", label_of(other)}}));
        edges.push_back(Json{{"from", id}, {"type", l.link_type}, {"to", other}, {"weight", l.weight}});
        if (!done.count(other)) next.push_back(other);
      }
    }
    frontier = std::move(next);
  }
  Json nodes = Json::array();
  for (const auto& [id, n] : known) nodes.push_back(Json{{"id", id}, {"kind", n.kind}, {"label", n.label}});
  const std::string head = x.say("graph_head", Json{{"name", name}, {"nodes", seeds.size()}, {"edges", edges.size()}});
  if (lines.empty()) return answer(head + "\n" + x.say("graph_alone"), Json{{"name", name}, {"nodes", nodes}, {"edges", edges}});
  return answer(head + "\n" + join_lines(lines), Json{{"name", name}, {"nodes", nodes}, {"edges", edges}});
}

StepOutcome graph_overview(Ctx& x) {
  graph_entry::Env env;
  env.db = &x.rt.db();
  env.data_dir = x.rt.paths().root;
  auto entry = graph_entry::compute(env, Json::object());
  if (!entry) return fail(entry.error());
  std::vector<std::string> lines;
  Json areas = Json::array();
  for (const auto& area : entry->value("areas", Json::array())) {
    std::vector<std::string> counts;
    Json cj = Json::object();
    const Json area_counts = area.value("counts", Json::object());
    for (auto it = area_counts.begin(); it != area_counts.end(); ++it) {
      const bool known = json::get_bool(it.value(), "known", false);
      counts.push_back(it.key() + ": " + (known ? json::dump(it.value().value("value", Json(0))) : x.say("unknown")));
      cj[it.key()] = known ? it.value().value("value", Json(nullptr)) : Json(nullptr);
    }
    std::string line = "• " + json::get_string(area, "label", json::get_string(area, "id"));
    if (!counts.empty()) {
      line += " — ";
      for (std::size_t i = 0; i < counts.size(); ++i) line += (i ? ", " : "") + counts[i];
    }
    lines.push_back(line);
    areas.push_back(Json{{"id", area.value("id", "")}, {"label", area.value("label", "")}, {"counts", cj}});
  }
  return answer(x.say("overview_head") + "\n" + join_lines(lines), Json{{"areas", areas}});
}

StepOutcome graph_reindex(Ctx& x) {
  auto& g = x.rt.graph();
  const bool all = x.arg("all").is_boolean() ? x.arg("all").get<bool>() : false;
  const std::string conv = x.conv_id();
  if (!all && conv.empty()) return fail(Errc::InvalidArgument, "name a conversation, or say all");
  const int n = all ? g.reindex_all() : g.reindex_conversation(conv);
  return answer(x.say(all ? "reindexed_all" : "reindexed", Json{{"count", n}, {"id", conv}}), Json{{"reindexed", n}, {"conv_id", all ? Json(nullptr) : Json(conv)}});
}

StepOutcome memory_recall(Ctx& x) {
  auto& mem = x.rt.memory();
  const std::string query = trim_quotes(x.str("query"));
  std::vector<MemoryNode> nodes;
  if (query.empty()) {
    nodes = mem.get_all();
  } else {
    nodes = mem.search(query, static_cast<int>(x.limit("memory", 30)));
  }
  const std::size_t cap = static_cast<std::size_t>(x.limit("memory", 30));
  std::vector<std::string> lines;
  Json items = Json::array();
  for (const auto& n : nodes) {
    if (!n.active) continue;
    if (items.size() >= cap) break;
    lines.push_back("• " + x.say("memory_line", Json{{"content", clip(n.content, 400)}, {"id", n.id}}));
    items.push_back(Json{{"id", n.id}, {"content", n.content}, {"node_type", n.node_type}, {"tags", n.tags}});
  }
  if (lines.empty()) return answer(x.say(query.empty() ? "memory_empty" : "memory_none", Json{{"query", query}}), Json{{"items", items}});
  return answer(x.say("memory_head", Json{{"count", lines.size()}}) + "\n" + join_lines(lines), Json{{"items", items}});
}

StepOutcome memory_remember(Ctx& x) {
  const std::string content = trim_quotes(x.str("content"));
  if (utf8::is_blank(content)) return fail(Errc::InvalidArgument, "content is required");
  Json meta = Json{{"origin", "chat"}};
  if (const std::string conv = x.conv_id(); !conv.empty()) meta["conv_id"] = conv;
  if (const std::string msg = json::get_string(x.c.context, "message_id"); !msg.empty()) meta["message_id"] = msg;
  auto id = x.rt.memory().add_node(content, std::nullopt, std::nullopt, meta, {});
  if (!id) return fail(id.error());
  return answer(x.say("memory_remembered", Json{{"content", clip(content, 400)}, {"id", *id}}), Json{{"id", *id}, {"content", content}});
}

StepOutcome memory_update(Ctx& x) {
  const std::string id = x.str("id");
  const std::string content = trim_quotes(x.str("content"));
  if (id.empty() || utf8::is_blank(content)) return fail(Errc::InvalidArgument, "id and content are required");
  auto before = x.rt.memory().get_node(id);
  if (!before) return fail(Errc::NotFound, "memory entry not found: " + id);
  if (auto st = x.rt.memory().update_node(id, Json{{"content", content}}); !st) return fail(st.error());
  return answer(x.say("memory_updated", Json{{"content", clip(content, 400)}, {"id", id}}), Json{{"id", id}, {"content", content}, {"previous", before->content}});
}

StepOutcome memory_forget(Ctx& x) {
  auto& mem = x.rt.memory();
  const std::string given = trim_quotes(x.str("query", x.str("id")));
  if (utf8::is_blank(given)) return fail(Errc::InvalidArgument, "say what to forget");
  std::vector<MemoryNode> found;
  if (auto exact = mem.get_node(given)) found.push_back(*exact);
  if (found.empty()) {
    for (auto& n : mem.search(given, static_cast<int>(x.limit("memory", 30))))
      if (n.active) found.push_back(std::move(n));
  }
  if (found.empty()) return fail(Errc::NotFound, x.say("memory_none", Json{{"query", given}}));
  if (found.size() > 1) {
    std::vector<std::string> lines;
    for (const auto& n : found) lines.push_back("• " + x.say("memory_line", Json{{"content", clip(n.content, 200)}, {"id", n.id}}));
    return fail(Errc::Conflict, x.say("memory_ambiguous", Json{{"count", found.size()}}) + "\n" + join_lines(lines));
  }
  const MemoryNode node = found[0];
  if (auto st = mem.delete_node(node.id, true); !st) return fail(st.error());
  return answer(x.say("memory_forgotten", Json{{"content", clip(node.content, 400)}, {"id", node.id}}), Json{{"id", node.id}, {"content", node.content}});
}

// ── import and export ───────────────────────────────────────────────

StepOutcome export_conversation(Ctx& x) {
  const std::string conv = x.conv_id();
  if (conv.empty()) return fail(Errc::InvalidArgument, "name a conversation");
  // "to": a format (markdown, json, text, html) or a storage backend; the words are the profile's.
  std::string format = x.fold(x.str("format"));
  std::string target = x.str("to", x.str("target"));
  const Json formats = x.cfg.value("formats", Json::object());
  auto format_of = [&](const std::string& word) {
    const std::string w = x.fold(trim_quotes(word));
    for (auto it = formats.begin(); it != formats.end(); ++it)
      for (const auto& alias : it.value())
        if (x.fold(alias.get<std::string>()) == w) return it.key();
    return std::string();
  };
  if (!format.empty()) format = format_of(format);
  std::string backend;
  if (!target.empty()) {
    // "markdown", "do Dysku Google jako json", "s3_main": the words of a format, or the id of a configured storage.
    const std::string words = x.fold(target);
    if (const std::string f = format_of(target); !f.empty() && format.empty()) {
      format = f;
    } else {
      backend = trim_quotes(target);
      const auto at = words.find(" jako ");
      if (at == std::string::npos) {
        const auto as = words.find(" as ");
        if (as != std::string::npos) {
          if (format.empty()) format = format_of(words.substr(as + 4));
          backend = trim_quotes(target.substr(0, as));
        }
      } else {
        if (format.empty()) format = format_of(words.substr(at + 6));
        backend = trim_quotes(target.substr(0, at));
      }
    }
  }
  if (format.empty()) format = json::get_string(x.cfg, "default_format", "markdown");
  const std::filesystem::path dir = x.rt.paths().root / "exports";
  auto written = x.rt.exporter().export_to_file(conv, format, dir);
  if (!written) return fail(written.error());
  Json data{{"conv_id", conv}, {"format", format}, {"path", written->string()}};
  if (!backend.empty()) {
    // The storage the words name: its id, or the kind of backend ("Dysk Google", "s3") among the configured ones.
    std::string storage_id;
    const Json storages = x.rt.storage().values().value("storages", Json::object());
    const std::string said = x.fold(backend);
    std::string kind;
    const Json kinds = x.cfg.value("storage_kinds", Json::object());
    for (auto it = kinds.begin(); it != kinds.end() && kind.empty(); ++it)
      for (const auto& w : it.value().value("words", Json::array()))
        if (said.find(x.fold(w.get<std::string>())) != std::string::npos) kind = it.key();
    for (auto it = storages.begin(); it != storages.end() && storage_id.empty(); ++it) {
      const Json& st = it.value();
      if (it.key() == backend) storage_id = it.key();
      else if (!kind.empty() && st.value("backend", Json::object()).value("kind", "") == kind && st.value("configured", false)) storage_id = it.key();
    }
    if (storage_id.empty()) return fail(Errc::NotFound, x.say("export_no_storage", Json{{"name", backend}}));
    auto sent = x.rt.storage().backup(*written, storage_id, written->filename().string());
    if (!sent) return fail(sent.error());
    data["storage"] = *sent;
    return answer(x.say("exported_to", Json{{"format", format}, {"path", written->string()}, {"storage", storage_id}}), data);
  }
  return answer(x.say("exported", Json{{"format", format}, {"path", written->string()}}), data);
}

std::string mime_extension(const Ctx& x, const std::string& mime) {
  const Json table = x.cfg.value("mime_ext", Json::object());
  const std::string base = mime.substr(0, mime.find(';'));
  const Json* v = json::find(table, base);
  return v && v->is_string() ? v->get<std::string>() : std::string();
}

StepOutcome import_run(Ctx& x) {
  std::filesystem::path path;
  std::string temp;  // a copy of a blob under a name that carries the extension, removed afterwards
  Json source = Json::object();
  if (x.has("path") && !x.str("path").empty()) {
    path = trim_quotes(x.str("path"));
    // A path typed in the conversation obeys the same policy as every path that comes from a request (confined runtimes: the user's own folders).
    if (auto ok = x.rt.path_policy().check(path, "path"); !ok) return fail(ok.error());
    source = Json{{"path", path.string()}};
  } else {
    // The file the device picked: a blob of the store.
    for (const auto& part : x.c.inputs) {
      if (part.blob_hash.empty()) continue;
      auto bytes = x.rt.blobs().read(part.blob_hash);
      if (!bytes) return fail(bytes.error());
      std::string name = part.node_id.empty() ? part.blob_hash.substr(0, 12) : part.blob_hash.substr(0, 12);
      std::string ext = mime_extension(x, part.mime);
      const std::filesystem::path dir = x.rt.paths().root / "tmp" / "uploads";
      std::error_code ec;
      std::filesystem::create_directories(dir, ec);
      path = dir / ("chat_" + name + ext);
      std::ofstream out(path, std::ios::binary);
      out.write(bytes->data(), static_cast<std::streamsize>(bytes->size()));
      if (!out.good()) return fail(Errc::Io, "cannot write the picked file for the import");
      temp = path.string();
      source = Json{{"blob", part.blob_hash}, {"mime", part.mime}};
      break;
    }
    if (path.empty()) return fail(Errc::InvalidArgument, x.say("import_no_file"));
  }
  ImportOptions opts;
  if (x.has("title")) opts.title = trim_quotes(x.str("title"));
  opts.force = x.arg("force").is_boolean() && x.arg("force").get<bool>();
  const std::string format = x.rt.importer().detect_format(path.string());
  auto done = x.rt.importer().import_file(path.string(), opts);
  if (!temp.empty()) {
    std::error_code ec;
    std::filesystem::remove(temp, ec);
  }
  if (!done) return fail(done.error());
  Json convs = Json::array();
  std::vector<std::string> lines;
  for (const auto& cv : done->conversations) {
    convs.push_back(Json{{"id", cv.id}, {"title", cv.title}});
    lines.push_back("• " + x.say("conv_line", Json{{"title", cv.title}, {"id", cv.id}, {"updated", cv.updated.substr(0, 10)}}));
  }
  const std::size_t shown = std::min<std::size_t>(lines.size(), static_cast<std::size_t>(x.limit("list", 20)));
  lines.resize(shown);
  return answer(x.say("imported", Json{{"format", done->format.empty() ? format : done->format}, {"conversations", convs.size()}, {"messages", done->messages}}) +
                    (lines.empty() ? "" : "\n" + join_lines(lines)),
                Json{{"format", done->format}, {"conversations", convs}, {"messages", done->messages}, {"source", source}});
}

// ── storage ─────────────────────────────────────────────────────────

StepOutcome storage_connect(Ctx& x) {
  // Only what is not secret is written; the credentials are entered in Settings (rule deny:settings.set:secret). Nothing the user typed here
  // can be a secret: the arguments that would hold one are refused with the pointer to Settings.
  for (const char* secret : {"secret", "password", "token", "access_key", "secret_key", "client_secret", "refresh_token", "api_key", "key"})
    if (x.has(secret)) return fail(Errc::Auth, x.say("connect_secret_refused"));
  const Json kinds = x.cfg.value("storage_kinds", Json::object());
  const std::string said = x.fold(x.str("kind"));
  std::string kind;
  for (auto it = kinds.begin(); it != kinds.end() && kind.empty(); ++it) {
    if (x.fold(it.key()) == said) kind = it.key();
    for (const auto& alias : it.value().value("words", Json::array()))
      if (x.fold(alias.get<std::string>()) == said) kind = it.key();
  }
  if (kind.empty()) return fail(Errc::InvalidArgument, x.say("connect_kind_unknown"));
  const Json spec = kinds.at(kind);
  std::string id = trim_quotes(x.str("id"));
  if (id.empty()) id = json::get_string(spec, "default_id", kind);
  // An id is a key of the overlay: letters, digits, underscore.
  std::string clean;
  for (char ch : x.fold(id)) clean += (std::isalnum(static_cast<unsigned char>(ch)) ? ch : '_');
  id = clean;
  if (id.empty() || !std::isalpha(static_cast<unsigned char>(id[0]))) id = "s_" + id;
  const std::string ref = "storage_" + id;
  Json backend{{"kind", kind}, {"secret_ref", ref}};
  for (const char* field : {"endpoint", "bucket", "region", "prefix", "url"})
    if (x.has(field)) backend[field] = trim_quotes(x.str(field));
  const Json defaults = spec.value("defaults", Json::object());
  for (auto it = defaults.begin(); it != defaults.end(); ++it)
    if (!backend.contains(it.key())) backend[it.key()] = it.value();
  Json missing = Json::array();
  for (const auto& need : spec.value("needs", Json::array()))
    if (!backend.contains(need.get<std::string>()) || (backend[need.get<std::string>()].is_string() && backend[need.get<std::string>()].get<std::string>().empty()))
      missing.push_back(need);
  if (!missing.empty()) {
    std::string names;
    for (std::size_t i = 0; i < missing.size(); ++i) names += (i ? ", " : "") + missing[i].get<std::string>();
    return fail(Errc::InvalidArgument, x.say("connect_missing", Json{{"fields", names}, {"kind", kind}}));
  }
  Json entry = spec.value("entry", Json::object());  // the class, sync and capability description of this kind, as data
  entry["label"] = Json{{"en", x.pick(spec.value("label", Json::object())) + " (" + id + ")"}, {"pl", json::get_string(spec.value("label", Json::object()), "pl", id) + " (" + id + ")"}};
  entry["backend"] = backend;
  entry["configured"] = true;
  OverlayStore overlay(x.rt.paths().root);
  if (auto st = overlay.put("storage_security", {"storages", id}, entry); !st) return fail(st.error());
  // The running service learns the new backend at once.
  if (auto p = RuntimeProfile::load("storage_security", x.rt.paths().root)) x.rt.storage().set_values(p->values());
  Json secrets = Json::array();
  std::vector<std::string> names;
  for (const auto& suffix : spec.value("secrets", Json::array())) {
    const std::string key = ref + "_" + suffix.get<std::string>();
    secrets.push_back(Json{{"key", key}, {"set", x.rt.secrets().has(key)}});
    names.push_back(key);
  }
  std::string list;
  for (std::size_t i = 0; i < names.size(); ++i) list += (i ? ", " : "") + names[i];
  Json data{{"id", id}, {"kind", kind}, {"overlay", "profiles/storage_security.pack"}, {"backend", backend}, {"secrets", secrets},
            {"open_settings", Json{{"section", "secrets"}, {"keys", names}}}};
  return answer(x.say("connected", Json{{"id", id}, {"kind", kind}, {"secrets", list}}), data);
}

// ── ports (the model or provider of a job) ──────────────────────────

const Json* role_row(Ctx& x, const std::string& word) {
  const std::string w = x.fold(trim_quotes(word));
  const Json* roles = json::find(x.cfg, "roles");
  if (!roles || !roles->is_array()) return nullptr;
  for (const auto& row : *roles) {
    if (x.fold(row.value("role", "")) == w) return &row;
    for (const auto& lang : {std::string("en"), std::string("pl")})
      for (const auto& alias : row.value("words", Json::object()).value(lang, Json::array()))
        if (x.fold(alias.get<std::string>()) == w) return &row;
  }
  return nullptr;
}

StepOutcome ports_show(Ctx& x) {
  auto prof = PortProfile::load(x.rt.paths().root);
  if (!prof) return fail(prof.error());
  const std::string word = x.str("role_word", x.str("role"));
  if (word.empty()) {
    const Json roles = prof->roles();
    std::vector<std::string> lines;
    for (auto it = roles.begin(); it != roles.end(); ++it)
      lines.push_back("• " + it.key() + " → " + x.say("role_entry", Json{{"adapter", json::get_string(it.value(), "adapter", json::get_string(it.value(), "compose"))},
                                                                           {"model", it.value().value("params", Json::object()).value("model", "")}}));
    return answer(x.say("roles_head") + "\n" + join_lines(lines), Json{{"roles", roles}});
  }
  const Json* row = role_row(x, word);
  const std::string role = row ? row->value("role", "") : trim_quotes(word);
  Json meta = Json::object();
  const std::string conv = x.conv_id();
  if (!conv.empty())
    if (auto cv = x.rt.db().get_conv(conv); cv && *cv) meta = (*cv)->metadata.value("ports", Json::object());
  auto resolved = prof->resolve(role, meta, conv, x.rt.config().all());
  if (!resolved) return fail(resolved.error());
  const Json params = resolved->value("params", Json::object());
  std::string model = params.value("model", "");
  if (model.empty() && resolved->contains("members") && (*resolved)["members"].is_array()) {
    for (const auto& m : (*resolved)["members"]) model += (model.empty() ? "" : ", ") + (m.is_string() ? m.get<std::string>() : json::get_string(m, "adapter"));
  }
  return answer(x.say("role_resolved", Json{{"role", role}, {"adapter", json::get_string(*resolved, "adapter", json::get_string(*resolved, "compose"))},
                                            {"model", model}, {"scope", json::get_string(*resolved, "scope")}, {"source", json::get_string(*resolved, "source")}}),
                *resolved);
}

StepOutcome ports_set(Ctx& x) {
  const std::string word = x.str("role_word", x.str("role"));
  const std::string value = trim_quotes(x.str("model", x.str("provider")));
  if (word.empty() || value.empty()) return fail(Errc::InvalidArgument, "say for which job and which model or provider");
  const Json* row = role_row(x, word);
  if (!row) return fail(Errc::NotFound, x.say("role_unknown", Json{{"word", word}}));
  const std::string role = row->value("role", "");
  const std::string kind = row->value("kind", "model");
  std::string scope = x.str("scope", "person");
  if (scope == "user") scope = "person";
  Json entry = Json::object();
  if (kind == "provider") {
    entry = Json{{"compose", "fallback"}, {"members", Json::array({value})}};
  } else {
    entry = Json{{"adapter", row->value("adapter", "openai_chat")}, {"params", Json{{"model", value}}}};
  }
  std::string where;
  std::string consumer;  // what reads the choice today (a key of role_consumers)
  const std::string setting = row->value("setting", "");
  if (scope == "conversation") {
    const std::string conv = x.conv_id();
    if (conv.empty()) return fail(Errc::InvalidArgument, "there is no conversation yet");
    auto cv = x.rt.db().get_conv(conv);
    if (!cv) return fail(cv.error());
    if (!*cv) return fail(Errc::NotFound, "conversation not found: " + conv);
    Json meta = (*cv)->metadata;
    if (!meta.is_object()) meta = Json::object();
    if (!meta.contains("ports") || !meta["ports"].is_object()) meta["ports"] = Json::object();
    meta["ports"][role] = entry;
    ConvPatch patch;
    patch.metadata = meta;
    if (auto st = x.rt.db().update_conv(conv, patch); !st) return fail(st.error());
    where = "conversations.metadata.ports";
    consumer = kind == "provider" || role.rfind("action.", 0) == 0 ? (kind == "provider" ? "provider" : "action") : "recorded";
  } else if (scope == "person" || scope == "installation") {
    if (!setting.empty() && kind == "model") {
      // The role is backed by a setting (the model of the chat, of the analysis): the person's choice is that setting, which the window already reads.
      if (auto ok = check_config_write(setting, x.rt.paths().root); !ok) return fail(ok.error());
      x.rt.config().set(setting, value);
      if (auto st = x.rt.config().save(); !st) return fail(st.error());
      where = "config.json:" + setting;
      consumer = "setting";
    } else {
      OverlayStore overlay(x.rt.paths().root);
      if (auto st = overlay.put("ports", {"roles", role}, entry); !st) return fail(st.error());
      where = "profiles/ports.pack";
      consumer = kind == "provider" ? "provider" : role.rfind("action.", 0) == 0 ? "action" : "recorded";
    }
  } else {
    return fail(Errc::InvalidArgument, "scope must be conversation or person");
  }
  // What reads the role today is said, not promised.
  const std::string reads = x.pick(x.cfg.value("role_consumers", Json::object()).value(consumer, Json(nullptr)));
  std::string reads_text = reads;
  if (consumer == "setting") {
    auto rendered = render_profile_template(reads, Json{{"setting", setting}});
    if (rendered) reads_text = *rendered;
  }
  Json data{{"role", role}, {"scope", scope}, {"entry", entry}, {"written", where}};
  return answer(x.say("role_set", Json{{"role", role}, {"value", value}, {"scope", x.say("scope_" + scope)}, {"where", where}}) + (reads_text.empty() ? "" : "\n" + reads_text), data);
}

// ── usage and money ─────────────────────────────────────────────────

std::string sqlite_stamp(std::time_t t) {
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

struct Period {
  std::string id, from, to;
};

Period period_of(Ctx& x, const std::string& words_in) {
  const std::string words = x.fold(words_in);
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&now, &tm);
  std::tm midnight = tm;
  midnight.tm_hour = midnight.tm_min = midnight.tm_sec = 0;
  const std::time_t today = timegm(&midnight);
  auto days_ago = [&](int n) { return today - static_cast<std::time_t>(n) * 86400; };
  Period p;
  p.to = sqlite_stamp(now + 1);
  // A number of days said outright: "w ostatnich 7 dniach", "in the last 30 days".
  const Json usage_cfg = x.cfg.value("usage", Json::object());
  if (const Json* rx = json::find(usage_cfg, "rolling_regex"); rx && rx->is_string())
    if (auto re = re::Regex::compile(rx->get<std::string>()))
      if (auto m = re->search(utf8::decode(words)))
        if (m->matched(1)) {
          const int n = std::max(1, std::atoi(m->group_utf8(1).c_str()));
          p.id = "last_" + std::to_string(n) + "_days";
          p.from = sqlite_stamp(now - static_cast<std::time_t>(n) * 86400);
          return p;
        }
  for (const auto& row : x.cfg.value("usage", Json::object()).value("periods", Json::array())) {
    for (const auto& lang : {std::string("en"), std::string("pl")})
      for (const auto& w : row.value("words", Json::object()).value(lang, Json::array()))
        if (!words.empty() && words.find(x.fold(w.get<std::string>())) != std::string::npos) {
          p.id = row.value("id", "");
          const std::string kind = row.value("kind", "");
          if (kind == "today") p.from = sqlite_stamp(today);
          else if (kind == "yesterday") {
            p.from = sqlite_stamp(days_ago(1));
            p.to = sqlite_stamp(today);
          } else if (kind == "week") {
            const int since_monday = (tm.tm_wday + 6) % 7;
            p.from = sqlite_stamp(days_ago(since_monday));
          } else if (kind == "month") {
            std::tm first = midnight;
            first.tm_mday = 1;
            p.from = sqlite_stamp(timegm(&first));
          } else if (kind == "year") {
            std::tm first = midnight;
            first.tm_mday = 1;
            first.tm_mon = 0;
            p.from = sqlite_stamp(timegm(&first));
          } else if (kind == "rolling") {
            p.from = sqlite_stamp(days_ago(static_cast<int>(json::get_int(row, "days", 7))));
          } else {
            continue;
          }
          return p;
        }
  }
  p.id = "all";
  p.from = "0000-00-00 00:00:00";
  return p;
}

StepOutcome usage_spend(Ctx& x) {
  const Period p = period_of(x, x.str("period"));
  auto policy = UsagePolicy::open(x.rt.paths().root / "usage-policy.sqlite", Json::object());
  if (!policy) return fail(policy.error());
  auto spent = (*policy)->spend_between(p.from, p.to);
  if (!spent) return fail(spent.error());
  const Json totals = spent->value("totals", Json::object());
  auto number = [&](const char* key) -> Json { return totals.contains(key) ? totals[key] : Json(nullptr); };
  std::vector<std::string> lines;
  auto line = [&](const char* id, const char* key) {
    const Json v = number(key);
    lines.push_back("• " + x.say(id, Json{{"value", v.is_null() ? x.say("unknown") : json::dump(v)}}));
  };
  lines.push_back("• " + x.say("spend_ops", Json{{"count", spent->value("operations", 0)}}));
  line("spend_requests", "requests");
  line("spend_tokens", "output_tokens");
  if (totals.contains("cost_usd")) lines.push_back("• " + x.say("spend_cost", Json{{"value", json::dump(totals["cost_usd"])}}));
  else lines.push_back("• " + x.say("spend_cost_unknown"));
  const Json by_model = spent->value("models", Json::object());
  for (auto it = by_model.begin(); it != by_model.end(); ++it)
    lines.push_back("  – " + x.say("spend_model", Json{{"model", it.key()}, {"count", it.value().value("operations", 0)}}));
  if (spent->value("folded_operations", 0) > 0) lines.push_back("• " + x.say("spend_folded", Json{{"count", spent->value("folded_operations", 0)}}));
  Json data = *spent;
  data["period"] = Json{{"id", p.id}, {"from", p.from}, {"to", p.to}};
  return answer(x.say("spend_head", Json{{"period", x.say("period_" + p.id)}}) + "\n" + join_lines(lines), data);
}

StepOutcome usage_set_limit(Ctx& x) {
  // The growth factor of the usage policy: how many times above the usual consumption an operation may grow before the user is asked.
  // The write is the setting loom_usage_policy and goes through the same rules as any edit of it: the x10 confirmation is not bypassed.
  double factor = 0;
  const Json given = x.arg("growth_factor");
  if (given.is_number()) factor = given.get<double>();
  else if (given.is_string()) {
    std::string s = given.get<std::string>();
    std::replace(s.begin(), s.end(), ',', '.');
    try {
      factor = std::stod(s);
    } catch (...) {
    }
  }
  if (!(factor > 0) || !std::isfinite(factor)) return fail(Errc::InvalidArgument, x.say("limit_number"));
  if (auto ok = check_config_write("loom_usage_policy", x.rt.paths().root); !ok) return fail(ok.error());
  Json options = x.rt.config().get("loom_usage_policy", Json::object());
  if (!options.is_object()) options = Json::object();
  const Json previous = options.value("growth_factor", Json(nullptr));
  options["growth_factor"] = factor;
  if (auto st = validate_usage_policy_options(options); !st) return fail(st.error());
  x.rt.config().set("loom_usage_policy", options);
  if (auto st = x.rt.config().save(); !st) return fail(st.error());
  return answer(x.say("limit_set", Json{{"value", number_text(factor)}}), Json{{"growth_factor", factor}, {"previous", previous}});
}

StepOutcome usage_compact(Ctx& x) {
  const bool dry = x.arg("dry_run").is_boolean() && x.arg("dry_run").get<bool>();
  Json command = Json{{"dry_run", dry}};
  auto done = compact_usage_ledger(x.rt.config(), x.rt.paths().root, command);
  if (!done) return fail(done.error());
  return answer(x.say(dry ? "compact_dry" : "compact_done", Json{{"status", json::get_string(*done, "status")}}), *done);
}

StepOutcome economy_report(Ctx& x) {
  Json command = Json::object();
  if (x.has("mode")) command["overrides"] = Json{{"mode", x.str("mode")}};
  auto report = economy::report_for_data_dir(x.rt.paths().root, x.rt.paths().db, command);
  if (!report) return fail(report.error());
  std::vector<std::string> lines;
  for (auto it = report->begin(); it != report->end(); ++it) {
    if (!it.value().is_object() || !it.value().contains("findings")) continue;
    lines.push_back("• " + it.key() + ": " + std::to_string(it.value()["findings"].size()));
  }
  return answer(x.say("economy_head", Json{{"status", json::get_string(*report, "status", "report")}}) + (lines.empty() ? "" : "\n" + join_lines(lines)), *report);
}

// ── models and providers ────────────────────────────────────────────

StepOutcome models_list(Ctx& x) {
  const Json all = x.rt.models().all();
  const std::string query = x.fold(trim_quotes(x.str("query")));
  const std::size_t cap = static_cast<std::size_t>(x.integer("limit", x.limit("models", 20)));
  std::vector<std::string> lines;
  Json items = Json::array();
  std::size_t total = 0;
  for (const auto& m : all) {
    const std::string id = json::get_string(m, "id");
    if (!query.empty() && x.fold(id + " " + json::get_string(m, "name")).find(query) == std::string::npos) continue;
    ++total;
    if (items.size() >= cap) continue;
    const std::int64_t ctx = json::get_int(m, "context_length", 0);
    lines.push_back("• " + id + (ctx > 0 ? " (" + std::to_string(ctx) + ")" : ""));
    items.push_back(Json{{"id", id}, {"name", json::get_string(m, "name")}, {"context_length", ctx}});
  }
  if (all.empty()) return answer(x.say("models_empty"), Json{{"items", items}});
  if (lines.empty()) return answer(x.say("models_none", Json{{"query", query}}), Json{{"items", items}});
  return answer(x.say("models_head", Json{{"shown", lines.size()}, {"total", total}}) + "\n" + join_lines(lines), Json{{"items", items}, {"total", total}});
}

StepOutcome models_refresh(Ctx& x) {
  auto& m = x.rt.models();
  if (auto st = m.update_from_api(); !st) return fail(st.error());
  return answer(x.say("models_refreshed", Json{{"count", m.size()}}), Json{{"count", m.size()}});
}

StepOutcome providers_list(Ctx& x) {
  Json providers = x.rt.providers().status();
  Json media = x.rt.media().status();
  std::vector<std::string> lines;
  const Json list = providers.is_object() ? providers.value("providers", Json::array()) : providers;
  for (const auto& p : list) {
    std::string caps;
    for (const auto& c : p.value("capabilities", Json::array())) caps += (caps.empty() ? "" : ", ") + (c.is_string() ? c.get<std::string>() : json::dump(c));
    lines.push_back("• " + x.say("provider_line", Json{{"id", p.value("id", "")}, {"state", x.say(json::get_bool(p, "available", false) ? "available" : "unavailable")}, {"caps", caps}}));
  }
  if (media.is_object())
    for (auto it = media.begin(); it != media.end(); ++it)
      if (it.value().is_object() && it.value().contains("available"))
        lines.push_back("• " + x.say("provider_line", Json{{"id", it.key()}, {"state", x.say(json::get_bool(it.value(), "available", false) ? "available" : "unavailable")}, {"caps", ""}}));
  if (lines.empty()) return answer(x.say("providers_none"), Json{{"providers", providers}, {"media", media}});
  return answer(x.say("providers_head") + "\n" + join_lines(lines), Json{{"providers", providers}, {"media", media}});
}

StepOutcome settings_list(Ctx& x) {
  auto doc = settings_fields(x.rt.paths().root);
  if (!doc) return fail(doc.error());
  std::string group = x.fold(trim_quotes(x.str("group")));
  if (!group.empty()) {
    const Json words = x.cfg.value("settings_groups", Json::object());
    for (auto it = words.begin(); it != words.end(); ++it) {
      if (x.fold(it.key()) == group) group = it.key();
      for (const auto& w : it.value()) if (x.fold(w.get<std::string>()) == group) group = it.key();
    }
  }
  std::vector<std::string> lines;
  Json items = Json::array();
  for (const auto& f : doc->value("fields", Json::array())) {
    if (!group.empty() && f.value("group", "") != group) continue;
    if (f.value("group", "") == "internal") continue;
    const std::string key = f.value("key", "");
    const bool secret = f.value("scope", "") == "secret";
    std::string value;
    if (secret) value = x.say(x.rt.secrets().has(key) ? "secret_set" : "secret_unset");
    else {
      const Json v = x.rt.config().get(key);
      value = v.is_null() ? x.say("setting_default") : clip(json::dump(v), 60);
    }
    lines.push_back("• " + key + " = " + value + " — " + clip(f.value("description", ""), 90));
    items.push_back(Json{{"key", key}, {"group", f.value("group", "")}, {"scope", f.value("scope", "")}, {"type", f.value("type", "")}, {"set", !secret || x.rt.secrets().has(key)}});
  }
  if (lines.empty()) return answer(x.say("settings_none", Json{{"group", group}}), Json{{"items", items}});
  return answer(x.say("settings_head", Json{{"count", lines.size()}, {"group", group.empty() ? x.say("settings_all") : group}}) + "\n" + join_lines(lines), Json{{"items", items}});
}

// ── background work ─────────────────────────────────────────────────

StepOutcome semantic_status(Ctx& x) {
  const Json st = x.rt.worker().status().to_json();
  std::string text = x.say("semantic_state", Json{{"pending", st.value("pending", 0)}, {"processed", st.value("processed", 0)}, {"errors", st.value("errors", 0)},
                                                  {"mode", st.value("mode", "")}, {"paused", x.say(st.value("paused", false) ? "yes" : "no")}});
  if (st.contains("usage_pending") && st["usage_pending"].is_object()) text += "\n" + x.say("semantic_waits");
  return answer(text, st);
}
StepOutcome semantic_pause(Ctx& x) {
  x.rt.worker().pause();
  return answer(x.say("semantic_paused"), x.rt.worker().status().to_json());
}
StepOutcome semantic_resume(Ctx& x) {
  x.rt.worker().resume();
  return answer(x.say("semantic_resumed"), x.rt.worker().status().to_json());
}
StepOutcome semantic_wake(Ctx& x) {
  x.rt.worker().wake();
  return answer(x.say("semantic_woken"), x.rt.worker().status().to_json());
}
StepOutcome semantic_requeue(Ctx& x) {
  Json filter = Json::object();
  if (const Json f = x.arg("filter"); f.is_object()) filter = f;
  auto done = x.rt.worker().requeue(filter);
  if (!done) return fail(done.error());
  return answer(x.say("semantic_requeued", Json{{"count", done->value("requeued", 0)}}), *done);
}

StepOutcome tasks_list(Ctx& x) {
  TaskFilter f;
  f.limit = static_cast<int>(x.integer("limit", x.limit("tasks", 20)));
  if (x.has("status")) f.status = x.str("status");
  auto tasks = x.rt.tasks().list(f);
  if (!tasks) return fail(tasks.error());
  std::vector<std::string> lines;
  Json items = Json::array();
  for (const auto& t : *tasks) {
    const Json j = t.to_json();
    lines.push_back("• " + x.say("task_line", Json{{"id", j.value("id", "")}, {"kind", j.value("kind", "")}, {"status", j.value("status", "")}}));
    items.push_back(j);
  }
  if (lines.empty()) return answer(x.say("tasks_none"), Json{{"items", items}});
  return answer(x.say("tasks_head", Json{{"count", lines.size()}}) + "\n" + join_lines(lines), Json{{"items", items}});
}
StepOutcome tasks_resume(Ctx& x) {
  auto& tasks = x.rt.tasks();
  auto n = tasks.recover_interrupted();
  if (!n) return fail(n.error());
  tasks.wake();
  return answer(x.say("tasks_resumed", Json{{"count", *n}}), Json{{"recovered", *n}});
}
StepOutcome tasks_cancel(Ctx& x) {
  const std::string id = trim_quotes(x.str("id"));
  if (id.empty()) return fail(Errc::InvalidArgument, "task id is required");
  if (auto st = x.rt.tasks().cancel(id); !st) return fail(st.error());
  return answer(x.say("task_cancelled", Json{{"id", id}}), Json{{"id", id}});
}

// ── what happened, where from ───────────────────────────────────────

StepOutcome logs_show(Ctx& x) {
  const std::size_t n = static_cast<std::size_t>(x.integer("limit", x.limit("logs", 30)));
  auto lines = log::recent(n);
  if (lines.empty()) return answer(x.say("logs_none"), Json{{"lines", Json::array()}});
  return answer(x.say("logs_head", Json{{"count", lines.size()}}) + "\n```\n" + join_lines(lines) + "\n```", Json{{"lines", lines}});
}

StepOutcome provenance_show(Ctx& x) {
  std::string subject = trim_quotes(x.str("subject"));
  if (subject.empty()) subject = x.element_id();
  if (subject.empty()) return fail(Errc::InvalidArgument, x.say("provenance_which"));
  auto& prov = x.rt.provenance();
  auto records = prov.for_subject(subject);
  if (!records) return fail(records.error());
  Json rows = Json::array();
  std::vector<std::string> lines;
  for (const auto& p : *records) {
    const Json j = p.to_json();
    rows.push_back(j);
    lines.push_back("• " + x.say("provenance_line", Json{{"kind", j.value("kind", j.value("type", ""))}, {"source", j.value("source_id", "")}, {"at", j.value("created", "")}}));
  }
  if (lines.empty()) return answer(x.say("provenance_none", Json{{"subject", subject}}), Json{{"subject", subject}, {"records", rows}});
  return answer(x.say("provenance_head", Json{{"subject", subject}, {"count", lines.size()}}) + "\n" + join_lines(lines), Json{{"subject", subject}, {"records", rows}});
}

StepOutcome sources_list(Ctx& x) {
  auto sources = x.rt.provenance().list_sources(static_cast<int>(x.integer("limit", x.limit("sources", 20))));
  if (!sources) return fail(sources.error());
  std::vector<std::string> lines;
  Json items = Json::array();
  for (const auto& s : *sources) {
    const Json j = s.to_json();
    lines.push_back("• " + x.say("source_line", Json{{"id", j.value("id", "")}, {"kind", j.value("kind", "")}, {"name", j.value("name", j.value("path", ""))}}));
    items.push_back(j);
  }
  if (lines.empty()) return answer(x.say("sources_none"), Json{{"items", items}});
  return answer(x.say("sources_head", Json{{"count", lines.size()}}) + "\n" + join_lines(lines), Json{{"items", items}});
}

StepOutcome app_info(Ctx& x) {
  const Json info = x.rt.info();
  const Json vault = x.rt.crypto().status();
  Json data{{"info", info}, {"encryption", vault}};
  std::vector<std::string> lines;
  lines.push_back("• " + x.say("about_version", Json{{"version", info.value("version", "")}}));
  lines.push_back("• " + x.say("about_data", Json{{"dir", x.rt.paths().root.string()}}));
  lines.push_back("• " + x.say("about_encryption", Json{{"state", x.say(json::get_bool(vault, "enabled", json::get_bool(vault, "unlocked", false)) ? "on" : "off")}}));
  return answer(x.say("about_head") + "\n" + join_lines(lines), data);
}

StepOutcome github_sync(Ctx& x) {
  SyncConfig cfg;
  cfg.repo = x.str("repo");
  if (cfg.repo.empty()) {
    Json saved = x.rt.config().get("github_repo");
    if (saved.is_string()) cfg.repo = saved.get<std::string>();
  }
  if (cfg.repo.empty()) return fail(Errc::InvalidArgument, x.say("github_repo_missing"));
  cfg.branch = x.str("branch", "main");
  cfg.local_path = x.str("local_path", x.rt.paths().root.string());
  cfg.sync_direction = x.str("direction", "bidirectional");
  cfg.token = x.rt.secrets().get_string("github_token");
  GitHubSync sync(cfg, x.rt.http());
  const std::string op = x.str("operation", "sync");
  if (op == "test") {
    const bool ok = sync.test_connection();
    return answer(x.say(ok ? "github_ok" : "github_fail"), Json{{"connected", ok}});
  }
  if (op == "status") {
    auto st = sync.get_sync_status();
    if (!st) return fail(st.error());
    return answer(x.say("github_status", Json{{"count", st->size()}}), Json{{"files", st->size()}});
  }
  Result<Json> r = Error(Errc::InvalidArgument, "operation must be test, status, pull, push or sync");
  if (op == "pull") r = sync.pull_all(std::nullopt);
  else if (op == "push") r = sync.push_all(std::nullopt);
  else if (op == "sync") r = sync.sync(std::nullopt);
  if (!r) return fail(r.error());
  return answer(x.say("github_done", Json{{"operation", op}}), *r);
}

// ── help ────────────────────────────────────────────────────────────

const Json* catalogue_entry(const Json& actions, const std::string& id) { return json::find(actions, id); }

std::string say_of(const Ctx& x, const Json& entry) {
  // How to say it: the first example in the language, else the first phrase without its wildcard.
  const Json ex = entry.value("examples", Json::object());
  for (const std::string& l : {x.lang, std::string("en")})
    if (const Json* a = json::find(ex, l); a && a->is_array() && !a->empty() && (*a)[0].is_string()) return (*a)[0].get<std::string>();
  const Json ph = entry.value("phrases", Json::object());
  for (const std::string& l : {x.lang, std::string("en")})
    if (const Json* a = json::find(ph, l); a && a->is_array() && !a->empty() && (*a)[0].is_string()) {
      std::string p = (*a)[0].get<std::string>();
      p.erase(std::remove(p.begin(), p.end(), '*'), p.end());
      return p;
    }
  return std::string();
}

std::string title_of(const Ctx& x, const Json& entry) {
  const std::string l = x.pick(entry.value("labels", Json::object()));
  return l.empty() ? json::get_string(entry, "title", json::get_string(entry, "id")) : l;
}

// The explicit syntax of an action: /id key=<…> for its parameters.
std::string syntax_of(const Json& entry) {
  std::string s = "/" + json::get_string(entry, "id");
  for (const auto& p : entry.value("params", Json::array())) {
    if (json::get_bool(p, "required", false)) s += " " + p.value("name", "") + "=…";
  }
  return s;
}

StepOutcome help_can_do(Ctx& x) {
  auto profile = RuntimeProfile::load("actions", x.rt.paths().root);
  if (!profile) return fail(profile.error());
  const Json* actions = json::find(profile->values(), "actions");
  if (!actions) return fail(Errc::Internal, "no catalogue");
  // The actions that fit the element in front of the user (this conversation) and those of the application as a whole.
  std::vector<Json> rows;
  std::set<std::string> seen;
  Json shown_for = Json::array();
  auto collect = [&](const Json& element, const std::string& scope_name) -> Status {
    Json req = Json{{"element", element}, {"implemented_only", true}};
    if (const std::string client = json::get_string(x.c.context, "client_id"); !client.empty()) req["client_id"] = client;
    auto rv = x.rt.actions().invoke("for", req);
    if (!rv) return rv.error();
    for (const auto& r : rv->value("actions", Json::array())) {
      const std::string id = r.value("action", "");
      if (!seen.insert(id).second) continue;
      if (r.value("status", "") == "denied") continue;
      Json row = r;
      row["_scope"] = scope_name;
      rows.push_back(std::move(row));
    }
    shown_for.push_back(Json{{"scope", scope_name}, {"element", rv->value("element", Json(nullptr))}});
    return {};
  };
  const std::string conv = x.conv_id();
  if (!conv.empty()) {
    if (auto st = collect(Json(conv), "conversation"); !st) return fail(st.error());
  }
  if (auto st = collect(Json("application"), "application"); !st) return fail(st.error());
  // Grouped by what they are for (the groups are data: runtime/chat_ops.pack help.groups).
  const Json groups = x.cfg.value("help", Json::object()).value("groups", Json::array());
  std::vector<std::vector<std::string>> bucket(groups.size() + 1);
  Json data_groups = Json::array();
  for (const auto& row : rows) {
    const std::string id = row.value("action", "");
    const Json* entry = catalogue_entry(*actions, id);
    if (!entry) continue;
    if (json::get_bool(*entry, "hidden", false)) continue;
    // Only what can be said and what can be run: the plumbing of the engine (declare a client, explain a rule) has no sentence, and an action that
    // is carried out by a consumer of its own (the chat send, the analysis) is not something to ask for.
    const std::string say = say_of(x, *entry);
    if (say.empty()) continue;
    if (row.value("runs_on", "") != "client" && !x.rt.actions().runnable(*entry)) continue;
    std::size_t at = groups.size();
    for (std::size_t g = 0; g < groups.size(); ++g) {
      const Json& grp = groups[g];
      bool in = false;
      for (const auto& a : grp.value("actions", Json::array())) in = in || a.get<std::string>() == id;
      for (const auto& pfx : grp.value("prefixes", Json::array())) in = in || id.rfind(pfx.get<std::string>(), 0) == 0;
      if (in) {
        at = g;
        break;
      }
    }
    std::string line = "• " + title_of(x, *entry);
    if (!say.empty()) line += " — „" + say + "”";
    if (row.value("status", "") == "needs_confirmation") line += " " + x.say("help_asks");
    bucket[at].push_back(line);
    data_groups.push_back(Json{{"action", id}, {"title", title_of(x, *entry)}, {"say", say}, {"group", at < groups.size() ? groups[at].value("id", "") : "other"}, {"scope", row.value("_scope", "")}});
  }
  std::vector<std::string> out;
  out.push_back(x.say("help_head"));
  for (std::size_t g = 0; g <= groups.size(); ++g) {
    if (bucket[g].empty()) continue;
    const std::string title = g < groups.size() ? x.pick(groups[g].value("title", Json::object())) : x.say("help_other");
    out.push_back("\n**" + title + "**");
    for (const auto& l : bucket[g]) out.push_back(l);
  }
  out.push_back("\n**" + x.say("help_expert_head") + "**");
  for (const auto& line : x.cfg.value("help", Json::object()).value("expert", Json::array())) out.push_back("• " + x.pick(line));
  out.push_back("\n" + x.say("help_how"));
  return answer(join_lines(out), Json{{"actions", data_groups}, {"for", shown_for}});
}

StepOutcome help_how_to(Ctx& x) {
  const std::string topic = trim_quotes(x.str("topic"));
  if (topic.empty()) return fail(Errc::InvalidArgument, "say what to do");
  // The topic goes through the same local reading as a message would: the explanation is of the command that reading finds, and nothing is run.
  Json request{{"text", topic}, {"method", "heuristic"}, {"lang", x.lang}, {"no_howto", true}};
  if (const std::string conv = json::get_string(x.c.context, "conv_id"); !conv.empty()) request["conv_id"] = conv;
  if (const std::string client = json::get_string(x.c.context, "client_id"); !client.empty()) request["client_id"] = client;
  auto routed = chat_route::route(x.rt, request);
  if (!routed) return fail(routed.error());
  if (routed->value("kind", "") != "command") return answer(x.say("howto_none", Json{{"topic", topic}}), Json{{"topic", topic}, {"found", false}});
  const Json cmd = routed->at("command");
  auto profile = RuntimeProfile::load("actions", x.rt.paths().root);
  const Json* actions = profile ? json::find(profile->values(), "actions") : nullptr;
  const std::string id = cmd.value("action", "");
  const Json* entry = actions ? json::find(*actions, id) : nullptr;
  std::vector<std::string> out;
  const std::string title = entry ? title_of(x, *entry) : id;
  out.push_back(x.say("howto_head", Json{{"title", title}, {"topic", topic}}));
  // The descriptions of the catalogue are written in English: they are quoted only where the answer is in English.
  if (entry && x.lang == "en" && !json::get_string(*entry, "description").empty()) out.push_back(clip(json::get_string(*entry, "description"), 300));
  const std::string say = entry ? say_of(x, *entry) : std::string();
  out.push_back("• " + x.say("howto_say", Json{{"say", topic}}));
  if (!say.empty() && say != topic) out.push_back("• " + x.say("howto_example", Json{{"say", say}}));
  // The steps of the plan, in plain words.
  std::string steps;
  for (const auto& s : cmd.value("plan", Json::object()).value("steps", Json::array())) {
    const Json* se = actions ? json::find(*actions, s.value("action", "")) : nullptr;
    steps += (steps.empty() ? "" : " → ") + (se ? title_of(x, *se) : s.value("action", ""));
  }
  if (!steps.empty()) out.push_back("• " + x.say("howto_steps", Json{{"steps", steps}}));
  if (!cmd.value("feasible", false)) {
    const Json reason = cmd.value("reason", Json::object());
    out.push_back("• " + x.say("howto_blocked", Json{{"why", reason.value("message", reason.value("code", ""))}}));
  } else if (!cmd.value("needs_confirmation", Json::array()).empty()) {
    out.push_back("• " + x.say("howto_confirm"));
  }
  if (cmd.value("estimate", Json::object()).value("model_calls", 0) > 0 || cmd.value("plan", Json::object()).value("cost", Json::object()).value("model_calls", 0) > 0)
    out.push_back("• " + x.say("howto_costs"));
  if (entry) out.push_back("• " + x.say("howto_syntax", Json{{"syntax", syntax_of(*entry)}}));
  out.push_back(x.say("howto_nothing_run"));
  return answer(join_lines(out), Json{{"topic", topic}, {"found", true}, {"action", id}, {"feasible", cmd.value("feasible", false)}, {"args", cmd.value("args", Json::object())}});
}

using Op = StepOutcome (*)(Ctx&);

const std::map<std::string, Op>& table() {
  static const std::map<std::string, Op> t = {
      {"conversations.list", conversations_list}, {"conversation.create", conversation_create}, {"conversation.rename", conversation_rename},
      {"conversation.delete", conversation_delete}, {"message.edit", message_edit}, {"message.versions", message_versions},
      {"message.restore", message_restore}, {"message.set_status", message_set_status}, {"message.weight", message_weight},
      {"search.find", search_find}, {"graph.related", graph_related}, {"graph.overview", graph_overview}, {"graph.reindex", graph_reindex},
      {"memory.recall", memory_recall}, {"memory.remember", memory_remember}, {"memory.update", memory_update}, {"memory.forget", memory_forget},
      {"export.conversation", export_conversation}, {"import.run", import_run}, {"storage.connect", storage_connect},
      {"ports.show", ports_show}, {"ports.set", ports_set}, {"usage.spend", usage_spend}, {"usage.set_limit", usage_set_limit},
      {"usage.compact", usage_compact}, {"economy.report", economy_report}, {"models.list", models_list}, {"models.refresh", models_refresh},
      {"providers.list", providers_list}, {"settings.list", settings_list}, {"semantic.status", semantic_status}, {"semantic.pause", semantic_pause},
      {"semantic.resume", semantic_resume}, {"semantic.wake", semantic_wake}, {"semantic.requeue", semantic_requeue}, {"tasks.list", tasks_list},
      {"tasks.resume", tasks_resume}, {"tasks.cancel", tasks_cancel}, {"logs.show", logs_show}, {"provenance.show", provenance_show},
      {"sources.list", sources_list}, {"app.info", app_info}, {"github.sync", github_sync}, {"help.can_do", help_can_do}, {"help.how_to", help_how_to}};
  return t;
}

}  // namespace

const std::vector<std::string>& chat_op_ids() {
  static const std::vector<std::string> ids = [] {
    std::vector<std::string> v;
    for (const auto& [id, op] : table()) v.push_back(id);
    return v;
  }();
  return ids;
}

void install_chat_ops(ExecutorTable& executors, Runtime& rt) {
  for (const auto& [id, op] : table()) {
    const Op fn = op;
    Runtime* runtime = &rt;
    executors.add(id, [fn, runtime](const StepCall& c) {
      Ctx x(*runtime, c);
      try {
        return fn(x);
      } catch (const std::exception& e) {
        return fail(Errc::Internal, std::string("operation failed: ") + e.what());
      }
    });
  }
}

}  // namespace loom::actions
