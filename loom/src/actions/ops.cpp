// The operations of the application as actions (action_ops.h): the executors behind search.find, graph.related, memory.*, conversation.*,
// message.*, export/import, storage.connect, ports.*, usage.*, models.*, semantic.*, tasks.*, help.* and the rest. Each is a thin layer over
// the engine that does the work (the calls the C ABI makes); what it says back is written from runtime/chat_ops.pack in the language of the run.
#include "loom/action_ops.h"

#include "chat/context_format.h"
#include "graph/projection.h"
#include "import/import_truth.h"
#include "loom/action_spend.h"
#include "loom/client_blobs.h"
#include "loom/archive.h"
#include "loom/catalog.h"
#include "loom/knowledge.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <ctime>
#include <functional>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "loom/action_overlay.h"
#include "loom/advisory.h"
#include "loom/action_runner.h"
#include "loom/chat_engine.h"
#include "loom/chat_route.h"
#include "loom/usage_policy.h"
#include "loom/util/sha256.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/economy.h"
#include "loom/effective_view.h"
#include "loom/perspectives.h"
#include "loom/interaction.h"
#include "loom/view_engine.h"
#include "loom/scope_resolver.h"
#include "loom/event_bus.h"
#include "loom/graph_engine.h"
#include "loom/graph_entry.h"
#include "loom/graph_memory.h"
#include "loom/importer.h"
#include "loom/log.h"
#include "loom/money_format.h"
#include "loom/memory_engine.h"
#include "loom/path_policy.h"
#include "profile_ops.h"
#include "unit_ops.h"
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
#include "loom/text_genesis.h"
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

bool conv_trashed(const Conversation& c);
std::string trashed_mode(Ctx& x);

StepOutcome conversations_list(Ctx& x) {
  const int limit = static_cast<int>(x.integer("limit", x.limit("list", 20)));
  // R101 D4: the trash is filtered after reading, so the limit counts what is shown (setting conversations_hide_trashed, argument trashed).
  const std::string mode = trashed_mode(x);
  auto convs = x.rt.db().list_convs(mode == "show" ? limit : -1);
  if (!convs) return fail(convs.error());
  std::vector<std::string> lines;
  Json items = Json::array();
  for (const auto& cv : *convs) {
    if ((mode == "hide" && conv_trashed(cv)) || (mode == "only" && !conv_trashed(cv))) continue;
    if (static_cast<int>(items.size()) >= limit) break;
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

// ── R101 D4: the trash (PULPIT_R101 §5) ─────────────────────────────
// "To the trash" is a reversible step; "empty" is today's irreversible one (conversation.delete, memory.forget, removing a pin link) and nothing
// that is irreversible today becomes easier. A conversation in the trash is metadata.trash = {at, from}; ConvPatch.metadata REPLACES the whole
// JSON, so the flag is written by read-merge-write of the whole metadata (B2; test_trash proves no other key is ever lost). The schema is unchanged.

bool conv_trashed(const Conversation& c) { return c.metadata.is_object() && c.metadata.contains("trash") && c.metadata.at("trash").is_object(); }

// The setting conversations_hide_trashed (unset = the preset true); an explicit "trashed" argument (hide | show | only) wins.
std::string trashed_mode(Ctx& x) {
  const std::string given = x.fold(trim_quotes(x.str("trashed")));
  if (given == "hide" || given == "show" || given == "only") return given;
  const Json v = x.rt.config().get("conversations_hide_trashed");
  return v.is_boolean() && !v.get<bool>() ? "show" : "hide";
}

Status conv_merge_metadata(Ctx& x, const std::string& id, const std::function<void(Json&)>& change) {
  auto existing = x.rt.db().get_conv(id);
  if (!existing) return existing.error();
  if (!*existing) return Error(Errc::NotFound, "conversation not found: " + id);
  Json meta = (*existing)->metadata.is_object() ? (*existing)->metadata : Json::object();
  change(meta);
  ConvPatch patch;
  patch.metadata = meta;
  patch.keep_updated = true;   // moving to the trash is not talking in the conversation
  return x.rt.db().update_conv(id, patch);
}

// A conversation named by its id, by the element, or by words of its title among the conversations of the wanted state (trashed or not).
Result<Conversation> conv_named(Ctx& x, bool in_trash) {
  std::string given = trim_quotes(x.str("query", x.str("id")));
  if (given.empty()) given = x.conv_id();
  if (given.empty()) return Error(Errc::InvalidArgument, x.say("trash_what"));
  if (looks_like_id(given, "c_")) {
    auto c = x.rt.db().get_conv(given);
    if (!c) return c.error();
    if (*c) return **c;
  }
  auto all = x.rt.db().list_convs(-1);
  if (!all) return all.error();
  const std::string needle = utf8::to_lower(given);
  std::vector<Conversation> found;
  for (const auto& c : *all)
    if (conv_trashed(c) == in_trash && contains_ci(c.title, needle)) found.push_back(c);
  if (found.empty()) return Error(Errc::NotFound, x.say(in_trash ? "trash_none_conv" : "conversation_none", Json{{"query", given}}));
  if (found.size() > 1) {
    std::vector<std::string> lines;
    for (const auto& c : found) lines.push_back("• " + x.say("conv_line", Json{{"title", c.title}, {"id", c.id}, {"updated", c.updated.substr(0, 10)}}));
    return Error(Errc::Conflict, x.say("trash_ambiguous", Json{{"count", found.size()}}) + "\n" + join_lines(lines));
  }
  return found[0];
}

StepOutcome conversation_trash(Ctx& x) {
  auto c = conv_named(x, false);
  if (!c) return fail(c.error());
  std::string from = trim_quotes(x.str("from"));
  if (from.empty()) from = x.element_class().empty() ? "chat" : x.element_class();
  const Json record{{"at", timeutil::utc_now_iso()}, {"from", from}};
  if (auto st = conv_merge_metadata(x, c->id, [&](Json& m) { m["trash"] = record; }); !st) return fail(st.error());
  return answer(x.say("conversation_trashed", Json{{"title", c->title}, {"id", c->id}}), Json{{"id", c->id}, {"title", c->title}, {"trash", record}});
}

StepOutcome conversation_untrash(Ctx& x) {
  auto c = conv_named(x, true);
  if (!c) return fail(c.error());
  if (auto st = conv_merge_metadata(x, c->id, [](Json& m) { m.erase("trash"); }); !st) return fail(st.error());
  return answer(x.say("conversation_untrashed", Json{{"title", c->title}, {"id", c->id}}), Json{{"id", c->id}, {"title", c->title}, {"trash", nullptr}});
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
  env.cfg = &x.rt.config();
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

// The origin of a note in words (M1-K2c-1): {known, source?, time?, ref?, words}. A note written before M1 recorded no origin and is worded as unknown;
// it is never given one it did not record.
Json memory_provenance(const Ctx& x, const MemoryNode& n) {
  const Json* p = n.metadata.is_object() ? json::find(n.metadata, "provenance") : nullptr;
  const std::string source = p && p->is_object() ? json::get_string(*p, "source") : std::string();
  if (source.empty()) return Json{{"known", false}, {"words", x.say("memory_prov_unknown")}};
  const std::string time = json::get_string(*p, "time");
  const std::string ref = json::get_string(*p, "ref");
  const std::string key = "memory_prov_" + source;
  std::string label = x.say(key);
  if (label == key) label = x.say("memory_prov_other", Json{{"source", source}});  // a source the person added to the vocabulary: its own name
  const std::string date = time.size() >= 10 ? time.substr(0, 10) : time;
  Json out{{"known", true}, {"source", source}, {"time", time}};
  if (!ref.empty()) out["ref"] = ref;
  out["words"] = x.say(ref.empty() ? "memory_prov" : "memory_prov_ref", Json{{"source", label}, {"time", date}, {"ref", ref}});
  return out;
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
    const Json provenance = memory_provenance(x, n);
    lines.push_back("• " + x.say("memory_line_prov", Json{{"content", clip(n.content, 400)}, {"id", n.id}, {"provenance", provenance.value("words", "")}}));
    items.push_back(Json{{"id", n.id}, {"content", n.content}, {"node_type", n.node_type}, {"tags", n.tags}, {"provenance", provenance}});
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
  auto& mem = x.rt.memory();
  // The origin of the note (M1-K2c-1): you said it in the conversation; the message it came from is the ref when it is known.
  auto id = mem.add_node(content, std::nullopt, std::nullopt, mem.stamped(meta, "user", json::get_string(x.c.context, "message_id")), {});
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
  // Nothing vanishes silently: the entry leaves every conversation (it is made inactive; the context never reads an inactive one) and the record of
  // forgetting - when, by whom, from which conversation, and whether its words stay - is kept on the entry. Whether the words stay is the pack's
  // preset (chat_ops forget.keep_content), not a rule. The entries below it go with it, each with the same record.
  const bool keep = json::get_bool(x.cfg.value("forget", Json::object()), "keep_content", true);
  const std::string when = timeutil::utc_now_iso();
  std::vector<std::string> ids{node.id};
  for (std::size_t i = 0; i < ids.size(); ++i)
    for (const auto& child : mem.get_children(ids[i])) ids.push_back(child.id);
  for (const auto& id : ids) {
    auto entry = mem.get_node(id);
    if (!entry) continue;
    Json meta = entry->metadata.is_object() ? entry->metadata : Json::object();
    Json record{{"time", when}, {"by", "user"}, {"kept", keep}, {"was_active", entry->active}};
    if (const std::string conv = x.conv_id(); !conv.empty()) record["conv_id"] = conv;
    if (id != node.id) record["via"] = node.id;
    meta["forgotten"] = record;
    Json fields{{"active", false}, {"metadata", meta}};
    if (!keep) fields["content"] = "";
    if (auto st = mem.update_node(id, fields); !st) return fail(st.error());
  }
  return answer(x.say(keep ? "memory_forgotten" : "memory_forgotten_dropped", Json{{"content", clip(node.content, 400)}, {"id", node.id}}),
                Json{{"id", node.id}, {"content", keep ? Json(node.content) : Json(nullptr)}, {"forgotten", Json{{"time", when}, {"kept", keep}, {"entries", ids.size()}}}});
}

// The way back from the list "Forgotten": the entry (and the entries that went with it) is made what it was before forgetting, the record of
// forgetting is not erased but moved to metadata.forget_history together with the restoring (when, by whom). An entry whose words were not kept has
// nothing to give back: it stays forgotten and the answer says so (preset; reversal: restore the record only by dropping the check below).
StepOutcome memory_restore(Ctx& x) {
  auto& mem = x.rt.memory();
  const std::string id = trim_quotes(x.str("id", x.str("query")));
  if (utf8::is_blank(id)) return fail(Errc::InvalidArgument, "say which entry to bring back");
  auto found = mem.get_node(id);
  if (!found) return fail(Errc::NotFound, x.say("memory_none", Json{{"query", id}}));
  const Json mark = found->metadata.is_object() ? found->metadata.value("forgotten", Json(nullptr)) : Json(nullptr);
  if (!mark.is_object()) return fail(Errc::Conflict, x.say("memory_not_forgotten", Json{{"content", clip(found->content, 400)}, {"id", id}}));
  if (utf8::is_blank(found->content)) return fail(Errc::Conflict, x.say("memory_restore_no_content", Json{{"id", id}}));
  const std::string when = timeutil::utc_now_iso();
  std::vector<std::string> ids{id};
  for (std::size_t i = 0; i < ids.size(); ++i)
    for (const auto& child : mem.get_children(ids[i])) ids.push_back(child.id);
  std::size_t restored = 0;
  for (const auto& one : ids) {
    auto entry = mem.get_node(one);
    if (!entry || !entry->metadata.is_object() || !entry->metadata.contains("forgotten")) continue;
    Json meta = entry->metadata;
    Json record = meta.at("forgotten");
    record["restored"] = Json{{"time", when}, {"by", "user"}};
    Json history = meta.value("forget_history", Json::array());
    if (!history.is_array()) history = Json::array();
    history.push_back(record);
    meta["forget_history"] = history;
    meta.erase("forgotten");
    const bool was_active = json::get_bool(record, "was_active", true);
    if (auto st = mem.update_node(one, Json{{"active", was_active}, {"metadata", meta}}); !st) return fail(st.error());
    ++restored;
  }
  return answer(x.say("memory_restored", Json{{"content", clip(found->content, 400)}, {"id", id}}), Json{{"id", id}, {"restored", Json{{"time", when}, {"entries", restored}}}});
}

// R101 D4 (B1): memory.trash is a NEW intermediate step, not memory.forget. The entry keeps its words, leaves every conversation (inactive; the
// context never reads an inactive entry) and carries metadata.trash = {at, by, was_active}; memory.untrash makes it what it was and moves the
// record to metadata.trash_history. memory.forget stays irreversible as cataloged (emptying the trash is memory.forget, with its consent).
StepOutcome memory_trash_step(Ctx& x, bool to_trash, const std::string& named = std::string()) {
  auto& mem = x.rt.memory();
  const std::string given = named.empty() ? trim_quotes(x.str("query", x.str("id"))) : named;
  if (utf8::is_blank(given)) return fail(Errc::InvalidArgument, x.say("trash_what"));
  auto in_trash = [](const MemoryNode& n) { return n.metadata.is_object() && n.metadata.contains("trash"); };
  std::vector<MemoryNode> found;
  if (auto exact = mem.get_node(given)) found.push_back(*exact);
  if (found.empty()) {
    const std::string needle = utf8::to_lower(given);
    for (auto& n : mem.get_all())
      if (in_trash(n) != to_trash && !(n.metadata.is_object() && n.metadata.contains("forgotten")) && contains_ci(n.content, needle)) found.push_back(std::move(n));
  }
  if (found.empty()) return fail(Errc::NotFound, x.say("memory_none", Json{{"query", given}}));
  if (found.size() > 1) {
    std::vector<std::string> lines;
    for (const auto& n : found) lines.push_back("• " + x.say("memory_line", Json{{"content", clip(n.content, 200)}, {"id", n.id}}));
    return fail(Errc::Conflict, x.say("memory_ambiguous", Json{{"count", found.size()}}) + "\n" + join_lines(lines));
  }
  const MemoryNode node = found[0];
  if (in_trash(node) == to_trash)
    return answer(x.say(to_trash ? "memory_already_trashed" : "memory_not_trashed", Json{{"content", clip(node.content, 400)}, {"id", node.id}}),
                  Json{{"id", node.id}, {"changed", false}});
  const std::string when = timeutil::utc_now_iso();
  Json meta = node.metadata.is_object() ? node.metadata : Json::object();
  Json fields = Json::object();
  if (to_trash) {
    meta["trash"] = Json{{"at", when}, {"by", "user"}, {"was_active", node.active}};
    fields["active"] = false;
  } else {
    Json record = meta.at("trash");
    record["restored"] = Json{{"time", when}, {"by", "user"}};
    Json history = meta.value("trash_history", Json::array());
    if (!history.is_array()) history = Json::array();
    history.push_back(record);
    meta["trash_history"] = history;
    meta.erase("trash");
    fields["active"] = json::get_bool(record, "was_active", true);
  }
  fields["metadata"] = meta;
  if (auto st = mem.update_node(node.id, fields); !st) return fail(st.error());
  return answer(x.say(to_trash ? "memory_trashed" : "memory_untrashed", Json{{"content", clip(node.content, 400)}, {"id", node.id}}),
                Json{{"id", node.id}, {"changed", true}, {"trash", to_trash ? meta.at("trash") : Json(nullptr)}});
}
StepOutcome memory_trash(Ctx& x) { return memory_trash_step(x, true); }
StepOutcome memory_untrash(Ctx& x) { return memory_trash_step(x, false); }

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
  // G1a: what was found and what was saved per kind of element, the reason when not all of it, in one sentence (the language of the run)
  const Json truth = import_truth::for_runtime(*done, x.rt.paths().root);
  const Json summary = truth.value("summary", Json::object());
  const std::string truth_said = json::get_string(summary, x.lang, json::get_string(summary, "en"));
  return answer(x.say("imported", Json{{"format", done->format.empty() ? format : done->format}, {"conversations", convs.size()}, {"messages", done->messages}}) +
                    (lines.empty() ? "" : "\n" + join_lines(lines)) + (truth_said.empty() ? "" : "\n" + truth_said),
                Json{{"format", done->format}, {"conversations", convs}, {"messages", done->messages}, {"source", source}, {"truth", truth}});
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

const Json* role_row(Ctx& x, const std::string& word, Json& synthesized) {
  const std::string w = x.fold(trim_quotes(word));
  // Any action that calls a model can have its own model: "action.<id>" (the id of a catalogue action) is a job without being listed.
  if (w.rfind("action.", 0) == 0) {
    if (auto profile = RuntimeProfile::load("actions", x.rt.paths().root))
      if (const Json* actions = json::find(profile->values(), "actions"); actions && json::find(*actions, w.substr(7))) {
        synthesized = Json{{"role", w}, {"kind", "model"}};
        return &synthesized;
      }
  }
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
  Json synthesized;
  const Json* row = role_row(x, word, synthesized);
  const std::string role = row ? row->value("role", "") : trim_quotes(word);
  Json meta = Json::object();
  const std::string conv = x.conv_id();
  if (!conv.empty())
    if (auto cv = x.rt.db().get_conv(conv); cv && *cv) meta = (*cv)->metadata.value("ports", Json::object());
  auto resolved = prof->resolve(role, meta, conv, x.rt.config().all());
  if (!resolved) return fail(resolved.error());
  // What the reader of the role does today (ports.pack roles.<role>.status.today), said under the answer, not promised.
  std::string today;
  if (auto shipped = RuntimeProfile::load("ports", x.rt.paths().root)) {
    const Json roles = shipped->values().value("roles", Json::object());
    const Json* doc = json::find(roles, role);
    for (auto it = roles.begin(); !doc && it != roles.end(); ++it) {
      const std::string& k = it.key();
      if (k.size() > 1 && k.compare(k.size() - 2, 2, ".*") == 0 && role.rfind(k.substr(0, k.size() - 1), 0) == 0) doc = &it.value();
    }
    if (doc) today = x.pick(doc->value("status", Json::object()).value("today", Json(nullptr)));
  }
  auto with_today = [&](std::string text) { return today.empty() ? text : text + "\n" + today; };
  // A model for one action (action.<id>): the runner honours only a choice of the person or of the conversation, never the shipped preset
  // (runner.cpp chosen_role); without one the action takes its step's model, else the field of action_run.pack use_model.model_setting.
  if (role.rfind("action.", 0) == 0 && json::get_string(*resolved, "scope") == "installation" &&
      resolved->value("params", Json::object()).value("model", "").empty()) {
    std::string key = "default_model";
    if (auto run = RuntimeProfile::load("action_run", x.rt.paths().root))
      key = json::get_string(run->values().value("use_model", Json::object()), "model_setting", key);
    const Json v = x.rt.config().get(key);
    const std::string fallback = v.is_string() ? v.get<std::string>() : std::string();
    Json data = *resolved;
    data["chosen"] = false;
    data["fallback_setting"] = key;
    data["fallback_model"] = fallback;
    return answer(with_today(x.say("role_action_unchosen", Json{{"role", role}, {"model", fallback.empty() ? x.say("role_no_model") : fallback}})), data);
  }
  // A job that reads a setting of its own before its role (the Secretary: guardian_model, then the role guardian): that setting answers when set.
  if (row && row->value("kind", "model") == "model" && json::get_string(*resolved, "scope") != "conversation") {
    const std::string setting = row->value("setting", "");
    const std::string role_setting = json::get_string(resolved->value("entry", Json::object()), "setting");
    const Json v = setting.empty() || setting == role_setting ? Json(nullptr) : x.rt.config().get(setting);
    if (v.is_string() && !v.get<std::string>().empty()) {
      Json data = *resolved;
      data["from_setting"] = setting;
      data["model"] = v.get<std::string>();
      return answer(with_today(x.say("role_from_setting", Json{{"role", role}, {"model", v.get<std::string>()}, {"setting", setting}})), data);
    }
  }
  const Json params = resolved->value("params", Json::object());
  std::string model = params.value("model", "");
  if (model.empty() && resolved->contains("members") && (*resolved)["members"].is_array()) {
    for (const auto& m : (*resolved)["members"]) model += (model.empty() ? "" : ", ") + (m.is_string() ? m.get<std::string>() : json::get_string(m, "adapter"));
  }
  return answer(with_today(x.say("role_resolved", Json{{"role", role}, {"adapter", json::get_string(*resolved, "adapter", json::get_string(*resolved, "compose"))},
                                                       {"model", model}, {"scope", json::get_string(*resolved, "scope")},
                                                       {"source", json::get_string(*resolved, "source")}})),
                *resolved);
}

StepOutcome ports_set(Ctx& x) {
  const std::string word = x.str("role_word", x.str("role"));
  const std::string value = trim_quotes(x.str("model", x.str("provider")));
  if (word.empty() || value.empty()) return fail(Errc::InvalidArgument, "say for which job and which model or provider");
  Json synthesized;
  const Json* row = role_row(x, word, synthesized);
  if (!row) return fail(Errc::NotFound, x.say("role_unknown", Json{{"word", word}}));
  const std::string role = row->value("role", "");
  const std::string kind = row->value("kind", "model");
  std::string scope = x.str("scope", "person");
  if (scope == "user") scope = "person";
  Json entry = Json::object();
  if (kind == "provider") {
    entry = Json{{"compose", "fallback"}, {"members", Json::array({value})}};
  } else {
    // The adapter: the row's, else the one of the role's own entry (image.generate: openai_images; speech.synthesize names none), else openai_chat.
    std::string adapter = row->value("adapter", "");
    bool role_without_adapter = false;
    if (adapter.empty())
      if (auto prof = PortProfile::load(x.rt.paths().root))
        if (auto r = prof->resolve(role); r && r->value("defined", false)) {
          adapter = json::get_string(*r, "adapter");
          role_without_adapter = adapter.empty() && !r->contains("compose");
        }
    if (adapter.empty() && !role_without_adapter) adapter = "openai_chat";
    entry = Json{{"params", Json{{"model", value}}}};
    if (!adapter.empty()) entry["adapter"] = adapter;
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
      if (auto ok = check_config_patch(Json{{setting, value}}, x.rt.paths().root); !ok) return fail(ok.error());
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

std::vector<std::string> spend_limit_lines(Ctx& x, Json* data_out);

StepOutcome usage_spend(Ctx& x) {
  const Period p = period_of(x, x.str("period"));
  // A question never creates the ledger: without one nothing was recorded.
  if (!std::filesystem::exists(x.rt.paths().root / "usage-policy.sqlite")) {
    Json state = nullptr;
    const auto limit_lines = spend_limit_lines(x, &state);
    return answer(x.say("spend_head", Json{{"period", x.say("period_" + p.id)}}) + "\n• " + x.say("spend_ops", Json{{"count", 0}}) + (limit_lines.empty() ? "" : "\n" + join_lines(limit_lines)),
                  Json{{"operations", 0}, {"totals", Json::object()}, {"period", Json{{"id", p.id}, {"from", p.from}, {"to", p.to}}}, {"limits", state}});
  }
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
  // The limits the user set, with what their periods have spent (P17).
  Json state = nullptr;
  for (const auto& l : spend_limit_lines(x, &state)) lines.push_back(l);
  data["limits"] = state;
  return answer(x.say("spend_head", Json{{"period", x.say("period_" + p.id)}}) + "\n" + join_lines(lines), data);
}

// ── spending limits per period (P17) ───────────────────────────────

std::string spend_amount_text(double v, const Json& data) {
  return format_money(v, data.value("money_format", Json(nullptr)));
}

// The limits as lines for the answer of "how much did I spend": each limit with what the period has spent, or the way to set one.
std::vector<std::string> spend_limit_lines(Ctx& x, Json* data_out) {
  std::vector<std::string> lines;
  const Json data = spend::data_of(x.rt.paths().root);
  if (data.empty()) return lines;
  const Json state = spend::status(x.rt.paths().root, x.rt.config(), data);
  if (data_out) *data_out = state;
  const Json limits = state.value("limits", Json::array());
  if (limits.empty() && state.value("issues", Json::array()).empty()) {
    lines.push_back(x.say("limits_none"));
    return lines;
  }
  lines.push_back(x.say("limits_head"));
  for (const auto& l : limits) {
    const Json vars{{"period", spend::period_label(data, json::get_string(l, "period"), x.lang)},
                    {"amount", number_text(l.value("amount", 0.0))},
                    {"currency", json::get_string(l, "currency")},
                    {"what", spend::on_exceed_label(data, json::get_string(l, "on_exceed"), x.lang)},
                    {"spent", l["spent"].is_number() ? spend_amount_text(l["spent"].get<double>(), data) : std::string()},
                    {"share", l["share"].is_number() ? number_text(std::round(l["share"].get<double>() * 100)) : std::string()}};
    lines.push_back("• " + x.say(l["spent"].is_number() ? "limit_line" : "limit_line_unknown", vars));
  }
  for (const auto& issue : state.value("issues", Json::array())) lines.push_back("• " + x.say("limit_issue", Json{{"why", issue.value("reason", "")}}));
  return lines;
}

StepOutcome usage_set_spend_limit(Ctx& x) {
  const Json data = spend::data_of(x.rt.paths().root);
  if (data.empty()) return fail(Errc::Unavailable, "the spending limits are not available in this build");
  const std::string setting = json::get_string(data, "setting", "usage_spend_limits");
  if (auto ok = check_config_write(setting, x.rt.paths().root); !ok) return fail(ok.error());
  const bool clearing = x.arg("clear").is_boolean() && x.arg("clear").get<bool>();
  const std::string period_text = x.fold(x.str("period"));
  if (clearing) {
    const std::string period = spend::period_in(data, period_text);
    if (period.empty()) return fail(Errc::InvalidArgument, x.say("limit_period"));
    auto done = spend::clear(x.rt.config(), data, period);
    if (!done) return fail(done.error());
    const Json vars{{"period", spend::period_label(data, period, x.lang)}};
    return answer(x.say(done->value("removed", false) ? "limit_spend_cleared" : "limit_spend_none", vars), *done);
  }
  Json args = Json::object();
  args["amount"] = x.arg("amount");
  args["period"] = period_text;
  args["currency"] = x.fold(x.str("currency"));
  args["on_exceed"] = x.fold(x.str("on_exceed"));
  if (x.arg("rate").is_number()) args["rate"] = x.arg("rate");
  auto entry = spend::entry_from(data, args);
  if (!entry) return fail(entry.error());
  auto done = spend::put(x.rt.config(), data, *entry);
  if (!done) return fail(done.error());
  const Json vars{{"period", spend::period_label(data, entry->period, x.lang)}, {"amount", number_text(entry->amount)}, {"currency", entry->currency},
                  {"usd", spend_amount_text(entry->usd(), data)}, {"rate", number_text(entry->rate)}, {"what", spend::on_exceed_label(data, entry->on_exceed, x.lang)}};
  std::string text = x.say("limit_spend_set", vars);
  if (entry->rate_source == "preset") text += " " + x.say("limit_rate_preset", Json{{"note", x.pick(data.value("rates_note", Json::object()))}});
  if (!done->value("previous", Json(nullptr)).is_null()) {
    const Json prev = (*done)["previous"];
    text += " " + x.say("limit_spend_replaced", Json{{"amount", number_text(prev.value("amount", 0.0))}, {"currency", json::get_string(prev, "currency")}});
  }
  return answer(text, *done);
}

StepOutcome usage_set_limit(Ctx& x) {
  // A spending limit of a period (amount / period / clear) or the growth factor of the usage policy.
  if (x.has("amount") || x.has("clear") || (x.has("period") && !x.has("growth_factor"))) return usage_set_spend_limit(x);
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

// ── the long pipelines from a sentence (P17 4) ─────────────────────

// The paths the sentence (or the picked file) names, as the pipelines take them: one path, or a list.
bool pipeline_sources(Ctx& x, Json& out, StepOutcome& failure) {
  out = Json::array();
  std::vector<std::string> given;
  const Json path = x.arg("path");
  if (path.is_array()) {
    for (const auto& p : path)
      if (p.is_string() && !p.get<std::string>().empty()) given.push_back(p.get<std::string>());
  } else if (path.is_string() && !path.get<std::string>().empty()) {
    given.push_back(path.get<std::string>());
  }
  if (!given.empty()) {
    for (const auto& raw : given) {
      const std::filesystem::path one = trim_quotes(raw);
      // A path typed in the conversation obeys the same policy as every path that comes from a request.
      if (auto ok = x.rt.path_policy().check(one, "path"); !ok) {
        failure = fail(ok.error());
        return false;
      }
      out.push_back(one.string());
    }
    return true;
  }
  // The folder the device picked (device.pick_folder, P18): a manifest of the files it uploaded, written as a folder the pipeline can read.
  for (const auto& part : x.c.inputs) {
    if (part.kind != "json" || !part.value.is_object() || json::get_string(part.value, "schema") != client_blobs::kFolderManifestSchema) continue;
    auto folder = client_blobs::materialize_folder(x.rt, part.value);
    if (!folder) {
      failure = fail(folder.error());
      return false;
    }
    out.push_back(folder->at("path").get<std::string>());
    return true;
  }
  // The file the device picked: a blob of the store, written where the pipeline can read it (it keeps its own copy; the file stays in tmp/uploads).
  for (const auto& part : x.c.inputs) {
    if (part.blob_hash.empty()) continue;
    auto bytes = x.rt.blobs().read(part.blob_hash);
    if (!bytes) {
      failure = fail(bytes.error());
      return false;
    }
    const std::filesystem::path dir = x.rt.paths().root / "tmp" / "uploads";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path file = dir / ("chat_" + part.blob_hash.substr(0, 12) + mime_extension(x, part.mime));
    std::ofstream o(file, std::ios::binary);
    o.write(bytes->data(), static_cast<std::streamsize>(bytes->size()));
    if (!o.good()) {
      failure = fail(Errc::Io, "cannot write the picked file for the pipeline");
      return false;
    }
    out.push_back(file.string());
    return true;
  }
  failure = fail(Errc::InvalidArgument, x.say("pipeline_no_path"));
  return false;
}

// "a, b; c and d" -> [a, b, c, d]: the separators are data (chat_ops.pack pipelines.seed_separators).
Json split_terms(Ctx& x, const std::string& text) {
  std::string work = text;
  for (const auto& sep : x.cfg.value("pipelines", Json::object()).value("seed_separators", Json::array())) {
    if (!sep.is_string() || sep.get<std::string>().empty()) continue;
    const std::string needle = sep.get<std::string>();
    std::string lowered = utf8::to_lower(work);
    std::string rebuilt;
    std::size_t at = 0;
    for (std::size_t hit = lowered.find(needle); hit != std::string::npos; hit = lowered.find(needle, at)) {
      rebuilt += work.substr(at, hit - at) + "\n";
      at = hit + needle.size();
    }
    rebuilt += work.substr(at);
    work = rebuilt;
  }
  Json out = Json::array();
  std::istringstream split(work);
  for (std::string line; std::getline(split, line);) {
    const std::string term = trim_quotes(line);
    if (!term.empty()) out.push_back(term);
  }
  return out;
}

std::string pipeline_name(Ctx& x, const std::string& kind) { return x.say("pipeline_name_" + kind); }

// The configuration of a pipeline from the sentence: the sources, the project name, the terms to start from, and whether the model may refine
// (off unless said: a pipeline started from a sentence is local and free until the user says otherwise).
StepOutcome pipeline_start(Ctx& x, const std::string& kind) {
  Json sources;
  StepOutcome failure;
  if (!pipeline_sources(x, sources, failure)) return failure;
  Json config = Json::object();
  config["sources"] = sources;
  if (kind != "catalog") {
    if (const std::string project = trim_quotes(x.str("project")); !project.empty()) config["project"] = project;
    if (x.has("llm") && !x.str("llm").empty()) config["llm"] = x.fold(trim_quotes(x.str("llm")));
    // A folder that is a repository is also read as one (code and history): the marker is data.
    const std::string marker = x.cfg.value("pipelines", Json::object()).value("repo_marker", "");
    if (!marker.empty() && sources.size() == 1) {
      std::error_code ec;
      if (std::filesystem::exists(std::filesystem::path(sources[0].get<std::string>()) / marker, ec)) config["repo"] = sources[0];
    }
  }
  if (kind == "archive")
    if (const std::string seeds = x.str("seeds"); !seeds.empty()) config["seed_terms"] = split_terms(x, seeds);
  auto started = x.rt.actions().pipelines().start(kind, config);
  if (!started) return fail(started.error());
  const std::string project = config.value("project", "");
  std::string where;
  for (const auto& p : sources) where += (where.empty() ? "" : ", ") + p.get<std::string>();
  std::string text = x.say("pipeline_started_" + kind, Json{{"path", where}, {"project", project.empty() ? std::string() : x.say("pipeline_project", Json{{"project", project}})}});
  if (kind != "catalog") text += " " + x.say(config.value("llm", "off") == "off" ? "pipeline_llm_off" : "pipeline_llm_on");
  return answer(text, *started);
}

// The number of things a summary counts, as "key=value" for the plain numbers and "key: n" for lists, in the order the pipeline gave them.
std::string summary_text(Ctx& x, const Json& summary) {
  const std::size_t cap = static_cast<std::size_t>(x.integer("summary_max", x.cfg.value("pipelines", Json::object()).value("summary_max", 8)));
  std::vector<std::string> parts;
  if (summary.is_object())
    for (auto it = summary.begin(); it != summary.end() && parts.size() < cap; ++it) {
      if (it->is_number()) parts.push_back(it.key() + "=" + number_text(it->get<double>()));
      else if (it->is_array()) parts.push_back(it.key() + ": " + std::to_string(it->size()));
      else if (it->is_string() && it->get<std::string>().size() <= 40 && !it->get<std::string>().empty()) parts.push_back(it.key() + "=" + it->get<std::string>());
    }
  std::string out;
  for (const auto& part : parts) out += (out.empty() ? "" : ", ") + part;
  return out;
}

StepOutcome pipeline_status(Ctx& x, const std::string& kind) {
  const Json st = x.rt.actions().pipelines().status(kind);
  const std::string state = json::get_string(st, "state", "idle");
  std::vector<std::string> lines;
  lines.push_back(x.say("pipeline_state", Json{{"name", pipeline_name(x, kind)}, {"state", x.say("pipeline_state_" + state)}}));
  if (const Json* p = json::find(st, "progress"); p && p->is_object() && state == "running") {
    const std::string counts = p->value("total", Json(nullptr)).is_number()
                                   ? " (" + std::to_string(p->value("current", 0)) + "/" + std::to_string(p->value("total", 0)) + ")"
                                   : (p->value("current", 0) > 0 ? " (" + std::to_string(p->value("current", 0)) + ")" : std::string());
    lines.push_back("• " + x.say("pipeline_progress", Json{{"stage", json::get_string(*p, "stage")}, {"counts", counts}, {"message", json::get_string(*p, "message")}}));
  }
  if (st.contains("started")) lines.push_back("• " + x.say("pipeline_started_at", Json{{"at", json::get_string(st, "started")}}));
  if (st.contains("finished")) lines.push_back("• " + x.say("pipeline_finished_at", Json{{"at", json::get_string(st, "finished")}}));
  if (const Json* r = json::find(st, "result"); r && r->is_object()) {
    const std::string what = summary_text(x, r->value("summary", Json::object()));
    lines.push_back("• " + x.say("pipeline_result", Json{{"status", json::get_string(*r, "status")}, {"what", what.empty() ? std::string() : ", " + what}}));
  }
  if (const Json* e = json::find(st, "error"); e && e->is_object()) lines.push_back("• " + x.say("pipeline_error", Json{{"why", json::get_string(*e, "message")}}));
  // What the kernel itself has of the latest run (also after a restart, when this process ran none).
  Json latest = nullptr;
  if (kind == "archive") {
    if (auto k = x.rt.archive().status("")) {
      latest = *k;
      const Json run = k->value("run", Json(nullptr));
      if (run.is_object()) lines.push_back("• " + x.say("pipeline_latest_archive", Json{{"status", json::get_string(run, "status")}, {"artifacts", k->value("artifacts", Json::array()).size()}}));
    }
  } else if (kind == "knowledge") {
    if (auto k = x.rt.knowledge().status("")) {
      latest = *k;
      const Json run = k->value("run", Json(nullptr));
      if (run.is_object()) lines.push_back("• " + x.say("pipeline_latest_knowledge", Json{{"id", json::get_string(run, "task_id")}, {"stages", k->value("stages", Json::array()).size()}}));
    }
  } else if (auto pack = x.rt.knowledge().pack()) {
    catalog::Catalog cat(x.rt, *pack);
    if (auto k = cat.status()) {
      latest = *k;
      lines.push_back("• " + x.say("pipeline_latest_catalog", Json{{"units", k->value("units", 0)}, {"sources", k->value("sources", 0)}, {"selected", k->value("selected", 0)}, {"imported", k->value("imported", 0)}}));
    }
  }
  Json data = st;
  data["latest"] = latest;
  return answer(join_lines(lines), data);
}

StepOutcome pipeline_cancel(Ctx& x, const std::string& kind) {
  auto done = x.rt.actions().pipelines().cancel(kind);
  if (!done) {
    if (done.error().code == Errc::NotFound) return answer(x.say("pipeline_cancel_none", Json{{"name", pipeline_name(x, kind)}}), Json{{"kind", kind}, {"state", "idle"}});
    return fail(done.error());
  }
  return answer(x.say("pipeline_cancel_ok", Json{{"name", pipeline_name(x, kind)}}), *done);
}

StepOutcome archive_start(Ctx& x) { return pipeline_start(x, "archive"); }
StepOutcome archive_status(Ctx& x) { return pipeline_status(x, "archive"); }
StepOutcome archive_cancel(Ctx& x) { return pipeline_cancel(x, "archive"); }
StepOutcome knowledge_start(Ctx& x) { return pipeline_start(x, "knowledge"); }
StepOutcome knowledge_status(Ctx& x) { return pipeline_status(x, "knowledge"); }
StepOutcome knowledge_cancel(Ctx& x) { return pipeline_cancel(x, "knowledge"); }
StepOutcome catalog_start(Ctx& x) { return pipeline_start(x, "catalog"); }
StepOutcome catalog_status(Ctx& x) { return pipeline_status(x, "catalog"); }
StepOutcome catalog_cancel(Ctx& x) { return pipeline_cancel(x, "catalog"); }

// What the catalogue holds about a word: its units by title, head and terms (read only; the scan filled it).
StepOutcome catalog_query(Ctx& x) {
  auto pack = x.rt.knowledge().pack();
  if (!pack) return fail(pack.error());
  catalog::UnitQuery q;
  const std::string text = trim_quotes(x.str("text"));
  if (!text.empty()) q.text = text;
  q.limit = static_cast<int>(x.integer("limit", x.limit("catalog", 10)));
  catalog::Catalog cat(x.rt, *pack);
  auto units = cat.query(q);
  if (!units) return fail(units.error());
  if (units->empty()) return answer(x.say("catalog_none", Json{{"text", text}}), Json{{"units", Json::array()}});
  std::vector<std::string> lines;
  Json items = Json::array();
  lines.push_back(x.say(text.empty() ? "catalog_head" : "catalog_head_about", Json{{"count", units->size()}, {"text", text}}));
  for (const auto& u : *units) {
    const Json row = u.to_json();
    const Json unit = row.value("unit", Json::object());
    const std::string title = json::get_string(unit, "title", json::get_string(row, "head", json::get_string(unit, "id")));
    lines.push_back("• " + x.say("catalog_line", Json{{"title", clip(title, 80)}, {"platform", json::get_string(row, "platform")}, {"id", json::get_string(unit, "id")}}));
    items.push_back(Json{{"id", json::get_string(unit, "id")}, {"title", title}, {"platform", json::get_string(row, "platform")}});
  }
  return answer(join_lines(lines), Json{{"units", items}, {"text", text}});
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


// ── graph views as data (P24): effective views, perspectives, scope inheritance, edge kinds ──

// A JSON argument said as text ("{...}", "[...]") or given as JSON; anything else stays what it was.
Json json_arg(const Ctx& x, const char* name) {
  const Json v = x.arg(name);
  if (!v.is_string()) return v;
  return json::parse_or(v.get<std::string>(), v);
}

// A word of the sentence mapped through a table of the profile ({canonical: [words]}); the canonical name or a word of it gives the name, else the word.
std::string canonical_word_in(const Ctx& x, const char* group, const char* table, const std::string& given) {
  const std::string w = x.fold(trim_quotes(given));
  const Json words = x.cfg.value(group, Json::object()).value(table, Json::object());
  if (words.is_object())
    for (auto it = words.begin(); it != words.end(); ++it) {
      if (x.fold(it.key()) == w) return it.key();
      if (it.value().is_array())
        for (const auto& alias : it.value())
          if (alias.is_string() && x.fold(alias.get<std::string>()) == w) return it.key();
    }
  return trim_quotes(given);
}

std::string canonical_word(const Ctx& x, const char* table, const std::string& given) { return canonical_word_in(x, "views", table, given); }

std::vector<std::string> id_list(const Json& v, const std::vector<std::string>& separators) {
  std::vector<std::string> out;
  if (v.is_array()) {
    for (const auto& i : v)
      if (i.is_string() && !i.get<std::string>().empty()) out.push_back(i.get<std::string>());
  } else if (v.is_string()) {
    std::string cur;
    std::string s = v.get<std::string>();
    for (const auto& sep : separators) {
      if (sep.empty() || sep == ",") continue;
      for (std::size_t at = s.find(sep); at != std::string::npos; at = s.find(sep, at + 1)) s.replace(at, sep.size(), ",");
    }
    auto flush = [&] {
      std::size_t b = cur.find_first_not_of(" \t");
      std::size_t e = cur.find_last_not_of(" \t");
      if (b != std::string::npos) out.push_back(cur.substr(b, e - b + 1));
      cur.clear();
    };
    for (char ch : s) {
      if (ch == ',') flush();
      else cur += ch;
    }
    flush();
  }
  return out;
}

StepOutcome views_effective(Ctx& x) {
  const std::string kind = canonical_word(x, "kinds", x.str("kind", x.str("view")));
  if (kind.empty()) return fail(Errc::InvalidArgument, x.say("views_which_kind"));
  auto eff = views::effective(x.rt.paths().root, kind);
  if (!eff) return fail(eff.error());
  const Json doc = eff->to_json(json::get_bool(x.c.args, "include_hidden", true));
  std::vector<std::string> lines;
  for (const auto& e : doc.value("entries", Json::array()))
    lines.push_back("• " + x.say("views_entry", Json{{"id", e.value("id", "")}, {"origin", e.value("origin", "")}, {"disabled", e.value("disabled", false) ? x.say("views_disabled") : std::string()}}));
  if (lines.empty()) return answer(x.say("views_none", Json{{"kind", kind}}), doc);
  return answer(x.say("views_head", Json{{"kind", kind}, {"count", lines.size()}}) + "\n" + join_lines(lines), doc);
}

StepOutcome views_write(Ctx& x) {
  const std::string view = canonical_word(x, "kinds", x.str("view", x.str("kind")));
  const std::string id = trim_quotes(x.str("id"));
  if (view.empty() || id.empty()) return fail(Errc::InvalidArgument, x.say("views_write_need"));
  const std::string overlay = x.str("overlay", "user");
  auto& root = x.rt.paths().root;
  if (json::get_bool(x.c.args, "remove", false) || x.fold(x.str("op")) == "remove") {
    auto n = views::remove_ops(root, overlay, view, id);
    if (!n) return fail(n.error());
    return answer(x.say("views_removed", Json{{"view", view}, {"id", id}, {"count", *n}}), Json{{"overlay", overlay}, {"removed", *n}});
  }
  const std::string op_name = canonical_word(x, "ops", x.str("op", "add"));
  Json op{{"view", view}, {"op", op_name}, {"id", id}};
  const Json entry = json_arg(x, "entry");
  const Json patch = json_arg(x, "patch");
  const Json as = x.arg("as");
  if (entry.is_object()) op["entry"] = entry;
  if (patch.is_object()) op["patch"] = patch;
  if (as.is_string()) op["as"] = trim_quotes(as.get<std::string>());
  if (auto st = views::put_op(root, overlay, op); !st) return fail(st.error());
  return answer(x.say("views_written", Json{{"view", view}, {"op", op_name}, {"id", id}}), Json{{"overlay", overlay}, {"written", op}});
}

StepOutcome perspectives_show(Ctx& x) {
  auto p = views::Perspectives::open(x.rt.paths().root);
  if (!p) return fail(p.error());
  std::vector<std::string> separators;
  for (const auto& sep : x.cfg.value("views", Json::object()).value("id_separators", Json::array()))
    if (sep.is_string()) separators.push_back(sep.get<std::string>());
  std::vector<std::string> ids = id_list(x.arg("ids"), separators);
  for (const auto& one : id_list(x.arg("id"), separators)) ids.push_back(one);
  if (ids.empty()) {
    const Json list = p->list();
    std::vector<std::string> lines;
    for (const auto& e : list) lines.push_back("• " + x.say("persp_entry", Json{{"id", e.value("id", "")}, {"label", x.pick(e.value("entry", Json::object()).value("label", Json::object()))}}));
    return answer(x.say("persp_head", Json{{"count", lines.size()}}) + "\n" + join_lines(lines), Json{{"perspectives", list}});
  }
  Json request = Json::object();
  for (const char* k : {"seeds", "hops", "direction", "limit", "key", "units", "msg_id", "label_chars"})  // msg_id: the perspective sent_to_model (the record of that request)
    if (x.has(k)) request[k] = json_arg(x, k);
  request["lang"] = x.lang;  // the words of the nodes (derived labels, reasons) are in the language of the run
  std::string conv = x.str("conversation_id");
  if (conv.empty()) conv = x.conv_id();
  if (!conv.empty()) request["conversation_id"] = conv;
  views::RenderContext rc;
  rc.db = &x.rt.db();
  rc.cfg = &x.rt.config();
  rc.root = x.rt.paths().root;
  rc.memory = &x.rt.memory();
  auto r = p->render_many(rc, ids, request);
  if (!r) return fail(r.error());
  std::vector<std::string> lines;
  const Json all = r->value("perspectives", Json::object());
  for (auto it = all.begin(); it != all.end(); ++it)
    lines.push_back("• " + x.say("persp_rendered", Json{{"id", it.key()}, {"nodes", it.value().value("nodes", Json::array()).size()}, {"edges", it.value().value("edges", Json::array()).size()}}));
  return answer(x.say("persp_side_by_side", Json{{"count", lines.size()}}) + "\n" + join_lines(lines), *r);
}

// P28 step 2: the effective input bindings of a device (the list), and their rebinding in the user's overlay (writes are confirmed like views.write).
StepOutcome interaction_show(Ctx& x) {
  auto it = views::Interaction::open(x.rt.paths().root);
  if (!it) return fail(it.error());
  const std::string said = trim_quotes(x.str("device"));
  const std::string device = said.empty() ? std::string() : canonical_word_in(x, "interaction", "devices", said);
  const Json r = it->resolve(Json{{"device", device}});
  std::vector<std::string> lines;
  for (const auto& b : r.value("bindings", Json::array())) {
    std::string note = b.value("status", "") == "recorded" ? x.say("ia_recorded") : std::string();
    if (b.value("shadowed", false)) note += (note.empty() ? "" : " ") + x.say("ia_shadowed", Json{{"by", b.value("shadowed_by", "")}});
    lines.push_back("• " + x.say("ia_line", Json{{"id", b.value("id", "")}, {"label", x.pick(b.value("label", Json::object()))}, {"note", note}}));
  }
  for (const auto& c : r.value("conflicts", Json::array())) lines.push_back("! " + x.pick(c.value("message", Json::object())));
  for (const auto& u : r.value("unsupported", Json::array()))
    lines.push_back("? " + x.say("ia_unsupported", Json{{"id", u.value("binding", "")}, {"why", x.pick(u.value("reason", Json::object()).value("text", Json::object()))}}));
  if (lines.empty()) return answer(x.say("ia_none", Json{{"device", device}}), r);
  return answer(x.say("ia_head", Json{{"device", device.empty() ? x.say("ia_all_devices") : device}, {"count", r.value("bindings", Json::array()).size()}}) + "\n" + join_lines(lines), r);
}

StepOutcome interaction_rebind(Ctx& x) {
  const std::string said = trim_quotes(x.str("binding", x.str("id")));
  if (said.empty()) return fail(Errc::InvalidArgument, x.say("ia_rebind_need"));
  auto it = views::Interaction::open(x.rt.paths().root);
  if (!it) return fail(it.error());
  const Json all = it->resolve(Json::object());
  auto exists = [&](const std::string& id) {
    for (const auto& b : all.value("bindings", Json::array()))
      if (b.value("id", "") == id) return true;
    return false;
  };
  std::string id = said;
  if (!exists(id)) {
    const std::string lock = canonical_word_in(x, "interaction", "locks", said);
    id = "kbd.lock." + lock;
  }
  if (!exists(id)) return fail(Errc::NotFound, x.say("ia_unknown_binding", Json{{"binding", said}}));
  auto& root = x.rt.paths().root;
  if (json::get_bool(x.c.args, "remove", false) || x.fold(x.str("op")) == "remove") {
    auto n = views::remove_ops(root, "user", "interaction_bindings", id);
    if (!n) return fail(n.error());
    return answer(x.say("ia_removed", Json{{"id", id}, {"count", *n}}), Json{{"overlay", "user"}, {"removed", *n}});
  }
  const std::string to = trim_quotes(x.str("to", x.str("action")));
  if (to.empty()) return fail(Errc::InvalidArgument, x.say("ia_rebind_need"));
  Json action = json_arg(x, "action");
  if (!action.is_object()) {
    const std::string kind = canonical_word_in(x, "interaction", "actions", to);
    action = Json{{"kind", kind}};
    if (kind == "zoom") action["factor"] = x.cfg.value("interaction", Json::object()).value("zoom_factor", Json(1.25));
    const std::string decl = trim_quotes(x.str("declaration"));
    if (kind == "switch_view" && !decl.empty()) action["declaration"] = decl;
  }
  // The old action's own fields leave (null removes a field in a deep patch), so the new action replaces it instead of mixing with it.
  Json patch_action = action;
  for (const auto& b : all.value("bindings", Json::array()))
    if (b.value("id", "") == id && b.value("action", Json::object()).is_object())
      for (auto f = b["action"].begin(); f != b["action"].end(); ++f)
        if (!patch_action.contains(f.key())) patch_action[f.key()] = nullptr;
  Json op{{"view", "interaction_bindings"}, {"op", "modify"}, {"id", id}, {"patch", Json{{"action", patch_action}}}};
  if (auto st = views::put_op(root, "user", op); !st) return fail(st.error());
  return answer(x.say("ia_rebound", Json{{"id", id}, {"action", action.value("kind", "")}}), Json{{"overlay", "user"}, {"written", op}});
}

// P28: a view declaration evaluated around a focus node; without a declaration the list of declarations.
StepOutcome view_model_show(Ctx& x) {
  auto engine = views::ViewEngine::open(x.rt.paths().root);
  if (!engine) return fail(engine.error());
  const std::string id = trim_quotes(x.str("declaration", x.str("id")));
  if (id.empty()) {
    const Json list = engine->list();
    std::vector<std::string> lines;
    for (const auto& e : list.value("declarations", Json::object()).value("entries", Json::array()))
      lines.push_back("• " + x.say("vm_entry", Json{{"id", e.value("id", "")}, {"label", x.pick(e.value("entry", Json::object()).value("label", Json::object()))}}));
    return answer(x.say("vm_list_head", Json{{"count", lines.size()}}) + "\n" + join_lines(lines), list);
  }
  const std::string focus_said = trim_quotes(x.str("focus"));
  if (focus_said.empty()) return fail(Errc::InvalidArgument, x.say("vm_which_focus"));
  std::string focus = focus_said;
  auto by_id = x.rt.db().get_node(focus_said);
  if (!by_id) return fail(by_id.error());
  if (!*by_id) {
    auto by_label = x.rt.db().find_node(focus_said);
    if (!by_label) return fail(by_label.error());
    if (!*by_label) return fail(Errc::NotFound, x.say("vm_no_focus", Json{{"focus", focus_said}}));
    focus = (*by_label)->id;
  }
  Json request{{"focus", focus}};
  if (x.has("budget")) request["budget"] = json_arg(x, "budget");
  views::RenderContext rc;
  rc.db = &x.rt.db();
  rc.cfg = &x.rt.config();
  rc.root = x.rt.paths().root;
  rc.memory = &x.rt.memory();
  auto r = engine->evaluate(rc, id, request);
  if (!r) return fail(r.error());
  std::size_t promoted = 0, allowed = 0;
  std::vector<std::string> lines;
  for (const auto& e : r->value("encodings", Json::array())) {
    const bool ok = e.value("status", "") == "promoted";
    (ok ? promoted : allowed) += 1;
    std::string why;
    for (const auto& reason : e.value("reasons", Json::array()))
      if (reason.value("level", "") == "warning") { why = " — " + x.pick(reason.value("text", Json::object())); break; }
    lines.push_back("• " + x.say("vm_encoding", Json{{"channel", e.value("channel", "")}, {"measure", e.value("measure", "")}, {"status", x.say(ok ? "vm_promoted" : "vm_allowed")}}) + why);
  }
  for (const auto& u : r->value("unsupported", Json::array()))
    lines.push_back("• " + x.say("vm_unsupported", Json{{"element", u.value("element", "")}, {"why", x.pick(u.value("reason", Json::object()).value("text", Json::object()))}}));
  return answer(x.say("vm_head", Json{{"id", id}, {"elements", r->value("elements", Json::array()).size()}, {"promoted", promoted}, {"allowed", allowed},
                                      {"unsupported", r->value("unsupported", Json::array()).size()}}) + "\n" + join_lines(lines), *r);
}

StepOutcome scopes_explain(Ctx& x) {
  scopes::Binding b;
  b.key = trim_quotes(x.str("key"));
  if (b.key.empty()) return fail(Errc::InvalidArgument, x.say("scopes_which_key"));
  b.conv_id = x.str("conversation_id");
  if (b.conv_id.empty()) b.conv_id = x.conv_id();
  auto resolver = scopes::Resolver::open(x.rt.paths().root, &x.rt.config(), &x.rt.db());
  if (!resolver) return fail(resolver.error());
  auto r = resolver->resolve(b);
  if (!r) return fail(r.error());
  const Json doc = r->to_json();
  std::vector<std::string> lines;
  for (const auto& l : doc.value("chain", Json::array()))
    lines.push_back("• " + x.say("scopes_level", Json{{"level", l.contains("label") ? x.pick(l["label"]) : l.value("level", "")}, {"role", x.say("scopes_role_" + l.value("role", "none"))}}));
  const std::string head = r->defined ? x.say("scopes_supplied", Json{{"key", b.key}, {"value", json::dump(r->value)}, {"scope", r->scope}}) : x.say("scopes_undefined", Json{{"key", b.key}});
  return answer(head + "\n" + join_lines(lines), doc);
}

StepOutcome edge_kinds_list(Ctx& x) {
  auto kinds = views::EdgeKinds::open(x.rt.paths().root);
  if (!kinds) return fail(kinds.error());
  auto persp = views::effective(x.rt.paths().root, "perspectives");
  if (!persp) return fail(persp.error());
  const std::string one = trim_quotes(x.str("kind"));
  if (!one.empty()) {
    const Json d = kinds->describe(one, *persp);
    return answer(x.say("edge_kind_one", Json{{"id", one}, {"inverse", d.value("inverse", "")}, {"ancestors", json::dump(d.value("ancestors", Json::array()))}}), d);
  }
  Json list = Json::array();
  std::vector<std::string> lines;
  for (const auto& e : kinds->view().entries) {
    list.push_back(kinds->describe(e.id, *persp));
    lines.push_back("• " + e.id);
  }
  return answer(x.say("edge_kinds_head", Json{{"count", lines.size()}}) + "\n" + join_lines(lines), Json{{"kinds", list}});
}

StepOutcome edge_kinds_classify(Ctx& x) {
  auto kinds = views::EdgeKinds::open(x.rt.paths().root);
  if (!kinds) return fail(kinds.error());
  auto persp = views::effective(x.rt.paths().root, "perspectives");
  if (!persp) return fail(persp.error());
  auto used = views::link_types_in_use(x.rt.db());
  if (!used) return fail(used.error());
  const Json d = kinds->classify(*used, *persp);
  std::vector<std::string> lines;
  for (const auto& u : d.value("unclassified", Json::array())) lines.push_back("• " + x.say("edge_unclassified", Json{{"type", u.value("type", "")}, {"count", u.value("count", 0)}}));
  const std::size_t known = d.value("known", Json::array()).size();
  if (lines.empty()) return answer(x.say("edge_all_classified", Json{{"known", known}}), d);
  return answer(x.say("edge_classify_head", Json{{"known", known}, {"unclassified", lines.size()}}) + "\n" + join_lines(lines), d);
}

// ── the graph projection from the conversation (M1-K0) ─────────────

projection::Env projection_env(const Ctx& x) {
  Json request{{"lang", x.lang}};
  if (const std::string conv = x.conv_id(); !conv.empty()) request["conv_id"] = conv;
  return projection::env_of(x.rt, request);
}

// The plural form of a count in the language of the run (CLDR cardinal rules of the languages the texts have): pl one | few | many,
// en one | many; a text keyed <id>_<form> is used when there is one, else <id>.
std::string plural_form(const std::string& lang, std::int64_t n) {
  if (n == 1) return "one";
  if (lang == "pl") {
    const std::int64_t m10 = n % 10, m100 = n % 100;
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return "few";
  }
  return "many";
}

std::string say_counted(const Ctx& x, const std::string& id, std::int64_t n, const Json& vars) {
  const std::string keyed = id + "_" + plural_form(x.lang, n);
  const std::string text = x.say(keyed, vars);
  return text == keyed ? x.say(id, vars) : text;
}

// The count of a row in words: its count, or why it is unknown (in words); an area no source lists yet gives its entry counters by name.
std::string nav_suffix(const Ctx& x, const Json& row) {
  std::string out;
  if (row.contains("count")) {
    out += x.say("nav_count", Json{{"count", row["count"]}});
  } else if (const std::string why = json::get_string(row, "count_unknown_reason"); !why.empty()) {
    std::vector<std::string> named;
    const Json counts = row.value("counts", Json::object());
    if (why == "no_projection_source" && counts.is_object())
      for (const auto& c : counts.items()) {
        const Json& v = c.value();
        const std::string name = json::get_string(v, "name", c.key());
        if (v.value("known", false)) named.push_back(x.say("nav_counter", Json{{"name", name}, {"value", v.value("value", Json(0))}}));
        else named.push_back(x.say("nav_counter_unknown", Json{{"name", name}, {"reason", json::get_string(v, "reason_human", json::get_string(v, "reason"))}}));
      }
    if (!named.empty()) {
      std::string joined;
      for (const auto& n : named) joined += (joined.empty() ? "" : ", ") + n;
      out += x.say("nav_counters", Json{{"counters", joined}});
    } else {
      out += x.say("nav_count_unknown", Json{{"reason", json::get_string(row, "count_unknown_human", why)}});
    }
  }
  const Json chip = row.value("provenance", Json::object()).value("chip", Json::object());
  if (const std::string label = json::get_string(chip, "label"); !label.empty()) out += x.say("nav_chip", Json{{"chip", label}});
  return out;
}

// G1a: what the graph does have, when a sentence named nothing: the areas with the words the data gives them, the nearest to the words
// said first (projection::known_names); "" when the names cannot be read (the refusal then stands alone, as it did).
std::string names_known(const Ctx& x, const projection::Env& env, const std::string& said) {
  auto known = projection::known_names(env, said);
  if (!known) {
    log::write(log::Level::Debug, "loom.actions.ops", "known_names: " + known.error().message);
    return std::string();
  }
  const Json names = known->value("names", Json::array());
  if (names.empty()) return std::string();
  std::string joined;
  for (const auto& n : names) {
    const Json words = n.value("words", Json::array());
    std::string spoken;
    for (const auto& w : words) spoken += (spoken.empty() ? "" : ", ") + w.get<std::string>();
    const std::string label = n.value("label", "");
    joined += (joined.empty() ? "" : "; ") + (spoken.empty() ? label : x.say("nav_name_words", Json{{"label", label}, {"words", spoken}}));
  }
  const std::int64_t rest = json::get_int(*known, "total", 0) - json::get_int(*known, "shown", 0);
  const std::string more = rest > 0 ? x.say("nav_names_more", Json{{"count", rest}}) : std::string();
  return " " + x.say(known->value("nearest", false) ? "nav_names_near" : "nav_names_all", Json{{"names", joined}, {"more", more}});
}

// The focus the sentence names: an address, or the name of an area or of a node (projection::find_address).
bool focus_of(const Ctx& x, const projection::Env& env, const char* arg, std::string& out, StepOutcome& failure) {
  const std::string said = trim_quotes(x.str(arg));
  out.clear();
  if (said.empty()) return true;
  auto found = projection::find_address(env, said);
  if (!found) {
    failure = fail(found.error());
    return false;
  }
  if (found->empty()) {
    failure = fail(Errc::NotFound, x.say("nav_no_focus", Json{{"focus", said}}) + names_known(x, env, said));
    return false;
  }
  out = *found;
  return true;
}

StepOutcome graph_navigate(Ctx& x) {
  const projection::Env env = projection_env(x);
  Json request = Json::object();
  for (const char* k : {"perspectives", "hops", "limit", "query", "offset"})
    if (x.has(k)) request[k] = json_arg(x, k);
  std::string focus;
  StepOutcome failure;
  if (!focus_of(x, env, "focus", focus, failure)) return failure;
  if (!focus.empty()) request["focus"] = focus;
  auto r = projection::navigate(env, request);
  if (!r) return fail(r.error());
  const Json counts = r->value("counts", Json::object());
  std::vector<std::string> lines;
  for (const auto& c : r->value("children", Json::array()))
    if (json::get_int(c, "depth", 1) == 1) lines.push_back(x.say("nav_row", Json{{"label", c.value("label", "")}, {"count", nav_suffix(x, c)}, {"chip", ""}}));
  if (const std::int64_t collapsed = json::get_int(counts, "collapsed", 0); collapsed > 0) {
    lines.push_back(say_counted(x, "nav_collapsed", collapsed, Json{{"collapsed", collapsed}}));
    const Json by_source = counts.value("by_source", Json::object());
    for (const auto& b : by_source.items())
      lines.push_back(x.say("nav_collapsed_source", Json{{"label", b.value().value("label", b.key())}, {"shown", b.value().value("shown", 0)}, {"total", b.value().value("total", 0)}}));
  }
  if (const std::int64_t hidden = json::get_int(r->value("hidden_by_you", Json::object()), "count", 0); hidden > 0)
    lines.push_back(x.say("nav_hidden", Json{{"count", hidden}}));
  for (const auto& u : r->value("unknown", Json::array()))
    lines.push_back(x.say("nav_unknown", Json{{"source", json::get_string(u, "source_label", json::get_string(u, "source"))},
                                              {"reason", json::get_string(u, "reason_human", json::get_string(u, "reason"))}}));
  const std::string label = json::get_string(r->value("focus", Json::object()), "label");
  if (lines.empty()) {
    // G1a: "nothing here" would be false for an area that shows its counters only (nothing lists its contents as nodes yet): say the counters
    const Json focus_node = r->value("focus", Json::object());
    const std::string address = json::get_string(focus_node, "address");
    if (address.rfind("area:", 0) == 0) {
      // the counters of an area are what the root lists for it
      auto root = projection::navigate(env, Json{{"limit", 0}});
      if (root) {
        const Json root_rows = root->value("children", Json::array());
        for (const auto& row : root_rows) {
          if (json::get_string(row, "address") != address) continue;
          const std::string counters = nav_suffix(x, row);
          if (counters.empty()) break;
          const std::string why = json::get_string(focus_node, "why");
          return answer(x.say("nav_area_counters", Json{{"label", label}, {"counters", counters}, {"why", why.empty() ? std::string() : " " + why}}), *r);
        }
      } else {
        log::write(log::Level::Debug, "loom.actions.ops", "graph.navigate: the root could not be read for the counters: " + root.error().message);
      }
    }
    return answer(x.say("nav_empty", Json{{"label", label}}), *r);
  }
  return answer(x.say("nav_head", Json{{"label", label}, {"shown", counts.value("shown", 0)}, {"total", counts.value("total", 0)}}) + "\n" + join_lines(lines), *r);
}

StepOutcome graph_card(Ctx& x) {
  const projection::Env env = projection_env(x);
  std::string address;
  StepOutcome failure;
  if (!focus_of(x, env, "address", address, failure)) return failure;
  if (address.empty()) return fail(Errc::InvalidArgument, x.say("card_which"));
  auto r = projection::card(env, Json{{"address", address}});
  if (!r) return fail(r.error());
  std::vector<std::string> lines;
  if (r->contains("value_human"))
    lines.push_back(x.say("card_value", Json{{"value", r->value("value_human", "")},
                                             {"chip", json::get_string(r->value("provenance", Json::object()).value("chip", Json::object()), "label",
                                                                       json::get_string(r->value("provenance", Json::object()), "layer"))}}));
  if (const std::string why = json::get_string(*r, "why"); !why.empty()) lines.push_back(why);
  const Json relations = r->value("relations", Json::object());
  for (const auto& rel : relations.items())
    lines.push_back(x.say("card_relation", Json{{"kind", json::get_string(rel.value(), "label", rel.key())},
                                                {"count", rel.value().value("out", Json::array()).size() + rel.value().value("in", Json::array()).size()}}));
  const std::string head = x.say("card_head", Json{{"label", r->value("label", "")}, {"kind", r->value("kind", "")}, {"address", address}});
  return answer(lines.empty() ? head : head + "\n" + join_lines(lines), *r);
}

StepOutcome settings_resolved_op(Ctx& x) {
  const projection::Env env = projection_env(x);
  auto r = projection::settings_resolved(env, Json::object());
  if (!r) return fail(r.error());
  std::vector<std::string> lines;
  std::int64_t yours_all = 0;
  for (const auto& g : r->value("groups", Json::array())) {
    // Yours: a value a level above the presets supplied (you, a conversation, a unit, a rule) - never a preset kept in config.json.
    std::int64_t yours = 0;
    for (const auto& f : g.value("fields", Json::array())) {
      const Json prov = f.value("provenance", Json::object());
      const std::string scope = json::get_string(prov, "scope");
      if (!scope.empty() && scope != "installation" && scope != "none" && !json::get_bool(prov, "same_as_preset", false)) ++yours;
    }
    yours_all += yours;
    const auto n = static_cast<std::int64_t>(g.value("fields", Json::array()).size());
    lines.push_back(say_counted(x, "settings_resolved_group", n, Json{{"label", g.value("label", "")}, {"count", n}, {"yours", yours}}));
  }
  if (const std::size_t hidden = r->value("hidden_keys", Json::array()).size(); hidden > 0) lines.push_back(x.say("settings_resolved_hidden", Json{{"count", hidden}}));
  const Json counts = r->value("counts", Json::object());
  return answer(x.say("settings_resolved_head", Json{{"fields", counts.value("fields", 0)}, {"groups", r->value("groups", Json::array()).size()}, {"yours", yours_all}}) + "\n" +
                    join_lines(lines),
                *r);
}

StepOutcome roles_resolved_op(Ctx& x) {
  const projection::Env env = projection_env(x);
  auto r = projection::roles_resolved(env, Json::object());
  if (!r) return fail(r.error());
  std::vector<std::string> lines;
  for (const auto& role : r->value("roles", Json::array())) {
    const std::string model = json::get_string(role, "model");
    // A role without a model has no class: the line says what it has instead (no model, providers in turn).
    const Json tier = role.value("tier", Json::object());
    if (model.empty())
      lines.push_back(x.say("rr_line_no_model", Json{{"label", role.value("label", "")}, {"model", json::get_string(role, "model_human", x.say("rr_no_model"))}}));
    else
      lines.push_back(x.say("rr_line", Json{{"label", role.value("label", "")}, {"model", role.value("model_human", model)},
                                            {"tier", json::get_string(tier, "label", json::get_string(tier, "id"))}}));
  }
  return answer(x.say("rr_head") + "\n" + join_lines(lines), *r);
}

StepOutcome rom_override_op(Ctx& x) {
  const std::string id = trim_quotes(x.str("id", x.str("address")));
  const Json patch = json_arg(x, "patch");
  if (id.empty() || !patch.is_object()) return fail(Errc::InvalidArgument, x.say("rom_which"));
  auto r = projection::rom_override(x.rt.paths().root, Json{{"id", id}, {"patch", patch}});
  if (!r) return fail(r.error());
  return answer(x.say("rom_override_done", Json{{"id", id}, {"file", r->value("file", "")}}), *r);
}


// ── what was sent to the model, and what the next request will be (M1-K1b) ──────────────────────────────────────────────────

// The scoped keys (action_rules.pack scoped_values.keys) whose value is for one turn: default_level turn.
Json turn_keys(Ctx& x) {
  auto rules = RuntimeProfile::load("action_rules", x.rt.paths().root);
  Json keys = Json::object();
  if (!rules) return keys;
  const Json all = rules->values().value("scoped_values", Json::object()).value("keys", Json::object());
  if (!all.is_object()) return keys;
  for (auto it = all.begin(); it != all.end(); ++it)
    if (it.value().is_object() && json::get_string(it.value(), "default_level") == "turn") keys[it.key()] = it.value();
  return keys;
}

// What waits in the turn carrier, in words: "szerzej ×2, profil lean".
std::string turn_words(Ctx& x, const Json& keys, const Json& waiting) {
  std::vector<std::string> parts;
  if (!waiting.is_object()) return std::string();
  for (auto it = waiting.begin(); it != waiting.end(); ++it) {
    const Json def = keys.is_object() && keys.contains(it.key()) ? keys.at(it.key()) : Json::object();
    const Json steps = def.value("steps", Json::object());
    std::map<std::string, int> counted;
    std::vector<std::string> order;
    const auto add_step = [&](const std::string& id) {
      if (!counted.count(id)) order.push_back(id);
      ++counted[id];
    };
    if (it.value().is_array()) {
      for (const auto& v : it.value()) if (v.is_string()) add_step(v.get<std::string>());
    } else if (it.value().is_string() && steps.is_object() && !steps.empty()) {
      add_step(it.value().get<std::string>());
    } else if (it.value().is_string()) {
      parts.push_back(x.say("context_modify_profile_word", Json{{"profile", it.value().get<std::string>()}}));
      continue;
    }
    for (const auto& id : order) {
      const std::string label = steps.contains(id) ? x.pick(steps.at(id).value("label", Json::object())) : id;
      parts.push_back(counted[id] > 1 ? label + " ×" + std::to_string(counted[id]) : label);
    }
  }
  std::string out;
  for (const auto& p : parts) out += (out.empty() ? "" : ", ") + p;
  return out;
}

StepOutcome context_modify(Ctx& x) {
  const std::string conv = [&] {
    const std::string named = trim_quotes(x.str("conversation_id"));
    return named.empty() ? x.conv_id() : named;
  }();
  TurnCarrier& carrier = x.rt.chat().turn_carrier();
  const Json keys = turn_keys(x);

  const Json reset = x.arg("reset");
  if (json::truthy(reset)) {
    const bool had = carrier.clear(conv);
    return answer(x.say(had ? "context_modify_cleared" : "context_modify_nothing"), Json{{"conv_id", conv}, {"cleared", had}, {"waiting", Json::object()}});
  }

  // What is being said: a profile of the selector, or a step (szerzej, węziej, głębiej, płycej) of a key whose value is for one turn.
  std::string key, value;
  const std::string profile = trim_quotes(x.str("profile"));
  const std::string step = trim_quotes(x.str("step", x.str("value")));
  if (!profile.empty()) {
    key = "context.profile";
    value = profile;
    auto cc = RuntimeProfile::load("chat_context", x.rt.paths().root);
    std::vector<std::string> ids;
    if (cc)
      for (const auto& row : cc->values().value("selector_profiles", Json::array()))
        if (row.is_object() && !json::get_string(row, "id").empty()) ids.push_back(json::get_string(row, "id"));
    if (std::find(ids.begin(), ids.end(), value) == ids.end()) {
      std::string list;
      for (const auto& id : ids) list += (list.empty() ? "" : ", ") + id;
      return fail(Errc::InvalidArgument, x.say("context_modify_unknown_profile", Json{{"profile", value}, {"profiles", list}}));
    }
  } else if (!step.empty()) {
    std::string known;
    for (auto it = keys.begin(); it != keys.end(); ++it) {
      const Json steps = it.value().value("steps", Json::object());
      if (!steps.is_object()) continue;
      for (auto st = steps.begin(); st != steps.end(); ++st) {
        known += (known.empty() ? "" : ", ") + st.key();
        if (st.key() == step && key.empty()) key = it.key();
      }
    }
    if (key.empty()) return fail(Errc::InvalidArgument, x.say("context_modify_unknown_step", Json{{"step", step}, {"steps", known}}));
    value = step;
  } else {
    return fail(Errc::InvalidArgument, x.say("context_modify_which"));
  }
  const bool append = keys.at(key).contains("steps") && keys.at(key).at("steps").is_object() && !keys.at(key).at("steps").empty();

  // The estimate after the change (the request is compiled as the next send would compile it) against the baseline of its kind; a jump of ten
  // times or more is the shared usage guard's question (the owner's rule: only a x10 jump is asked). Numbers the guard cannot give stay unknown.
  // M1-K1c1: the estimate is for the request the send will compile - the text the person has typed (pending_text; the graph is chosen by it, so
  // widening has nothing to widen without it) and the options of the send (pending_options: the object a chat request carries). Without them it is
  // the estimate for an empty message and says so in the answer (guard.estimate_for), never as if it were the request itself.
  const std::string pending_text = x.str("pending_text");
  ChatOptions probe;
  bool pending_options = false;
  if (const Json options = x.arg("pending_options"); !options.is_null()) {
    if (!options.is_object()) return fail(Errc::InvalidArgument, x.say("context_modify_bad_options", Json{{"why", "pending_options must be an object"}}));
    if (!options.empty()) {
      auto parsed = ChatOptions::from_json(options);
      if (!parsed) return fail(Errc::InvalidArgument, x.say("context_modify_bad_options", Json{{"why", parsed.error().message}}));
      probe = *parsed;
      pending_options = true;
    }
  }
  if (!conv.empty()) probe.conv_id = conv;
  Json guard = nullptr;
  {
    auto g = x.rt.chat().context_guard(conv, pending_text, probe, Json{{key, value}});
    if (g) {
      guard = *g;
    } else {
      guard = Json{{"resource", "input_bytes"}, {"asked", false}, {"unknown_reason", "guard_not_computed"}, {"why", g.error().message}};
      log::write(log::Level::Debug, "loom.actions.ops", "context.modify: the request could not be compiled for the guard: " + g.error().message);
    }
  }
  guard["estimate_for"] = pending_text.empty() ? "empty_message" : "pending_text";
  guard["pending_text_bytes"] = pending_text.size();
  guard["pending_options"] = pending_options;
  bool confirmed = false;
  if (json::get_bool(guard, "asked", false)) {
    const Json& conf = x.c.usage_confirmation;
    if (conf.is_object() && !conf.empty()) {
      if (!json::get_bool(conf, "approved", false)) {
        // The refusal is the ledger's record too; nothing was changed and nothing waits in the carrier.
        auto settings = effective_usage_policy_options(x.rt.config());
        if (settings)
          if (auto policy = UsagePolicy::open(x.rt.paths().root / "usage-policy.sqlite", *settings))
            if (auto c = (*policy)->confirm(json::get_string(conf, "operation_id"), json::get_string(conf, "receipt_id"), false, json::get_string(conf, "ref")); !c)
              log::write(log::Level::Debug, "loom.actions.ops", "context.modify: the refusal was not recorded: " + c.error().message);
        return fail(Errc::Cancelled, x.say("context_modify_declined"));
      }
      confirmed = true;
    } else {
      // The question goes through the guard of the usage ledger like every other: the decision is recorded and its receipt is what the answer binds to.
      auto settings = effective_usage_policy_options(x.rt.config());
      if (!settings) return fail(settings.error());
      auto policy = UsagePolicy::open(x.rt.paths().root / "usage-policy.sqlite", *settings);
      if (!policy) return fail(policy.error());
      const std::string bytes = std::to_string(json::get_int(guard, "bytes", 0));
      Json estimate{{"operation_id", "ctxmod_" + Sha256::hex(x.c.run_id + "|" + conv + "|" + key + "|" + json::dump(value) + "|" + json::dump(x.rt.chat().turn_carrier().peek(conv)) + "|" + bytes).substr(0, 24)},
                    {"baseline_key", json::get_string(guard, "baseline_key")},
                    {"resources", Json{{"requests", 1}, {"input_bytes", json::get_int(guard, "bytes", 0)}, {"output_tokens", nullptr}, {"cost_usd", nullptr}}},
                    {"instrument", "loom.context.modify"}, {"model", json::get_string(guard, "model")},
                    {"estimate_basis", "compiled_request_body_bytes_after_the_turn_modifier"}};
      auto decision = (*policy)->request(estimate);
      if (!decision) return fail(decision.error());
      if (json::get_string(*decision, "status") == "denied") return fail(Errc::Conflict, x.say("context_modify_denied"));
      if (json::get_string(*decision, "status") == "requires_confirmation") {
        StepOutcome held;
        held.state = StepOutcome::State::Held;
        held.hold = Json{{"schema", "loom.action_usage_pending/1"}, {"stage", "context_modify"}, {"status", "requires_confirmation"},
                         {"operation_id", decision->value("operation_id", Json(nullptr))}, {"receipt_id", decision->value("receipt_id", Json(nullptr))},
                         {"baseline_key", decision->value("baseline_key", Json(nullptr))}, {"projected", decision->value("resources", Json::object())},
                         {"guard", guard}, {"sent", false}};
        return held;
      }
      // The ledger sees nothing to ask about after all (the baseline moved between the look and the request): the reservation it made is let go.
      (void)(*policy)->cancel(json::get_string(*decision, "operation_id"), "context.modify only prepares the request");
    }
  }
  if (confirmed) {
    // The person said yes to the question of the guard: the ledger records it and the reservation it made is let go - this action sends nothing,
    // the request itself is admitted again when it is sent.
    const Json& conf = x.c.usage_confirmation;
    auto settings = effective_usage_policy_options(x.rt.config());
    if (settings)
      if (auto policy = UsagePolicy::open(x.rt.paths().root / "usage-policy.sqlite", *settings)) {
        const std::string op = json::get_string(conf, "operation_id");
        if (auto c = (*policy)->confirm(op, json::get_string(conf, "receipt_id"), true, json::get_string(conf, "ref")); !c)
          log::write(log::Level::Debug, "loom.actions.ops", "context.modify: the confirmation was not recorded: " + c.error().message);
        else
          (void)(*policy)->cancel(op, "context.modify only prepares the request");
      }
  }

  carrier.add(conv, key, value, append);
  const Json waiting = carrier.peek(conv);
  Json data{{"conv_id", conv}, {"key", key}, {"value", value}, {"applies_to", "next_request"}, {"persistent", false}, {"waiting", waiting}, {"guard", guard}};
  if (confirmed) data["confirmed"] = true;
  std::string text = x.say("context_modify_applied", Json{{"what", turn_words(x, keys, waiting)}});
  // The number always names what it is measured against (the base: the average of the last requests of its kind, a starting floor or a declared
  // value) and what the estimate was compiled for (an empty message or the typed text).
  if (guard.value("ratio", Json(nullptr)).is_number()) {
    const std::string src = json::get_string(guard, "baseline_source");
    const std::string n = std::to_string(json::get_int(guard, "baseline_samples", 0));
    const Json vars{{"ratio", number_text(std::round(json::get_number(guard, "ratio", 0.0) * 10) / 10)},
                    {"for", x.say(guard.value("estimate_for", "empty_message") == "pending_text" ? "context_modify_for_text" : "context_modify_for_empty")},
                    {"base", x.say(src == "measured" ? "context_modify_base_measured" : src == "floor" ? "context_modify_base_floor" : "context_modify_base_declared", Json{{"n", n}})}};
    text += "\n" + x.say(json::get_bool(guard, "asked", false) ? "context_modify_guard_asked" : "context_modify_guard_ratio", vars);
  }
  return answer(text, data);
}

StepOutcome context_receipt(Ctx& x) {
  std::string id = x.element_class() == "message" ? x.element_id() : std::string();
  if (id.empty()) id = trim_quotes(x.str("msg_id", x.str("id")));
  if (id.empty()) {
    // Without a message named: the last request of this conversation - the last message the person sent that has a record (the sentence that asks
    // for the record is not a request to the model and has none); when none has, the last message the person sent.
    const std::string conv = x.conv_id();
    if (!conv.empty()) {
      auto msgs = x.rt.db().get_msgs(conv);
      if (msgs) {
        std::string last_user;
        for (auto it = msgs->rbegin(); it != msgs->rend(); ++it) {
          if (it->role != "user" || it->status != "active") continue;
          if (last_user.empty()) last_user = it->id;
          const Json trace = it->metadata.is_object() ? it->metadata.value("context_trace", Json(nullptr)) : Json(nullptr);
          if (trace.is_object() && trace.value("receipt", Json(nullptr)).is_object()) { id = it->id; break; }
        }
        if (id.empty()) id = last_user;
      }
    }
  }
  if (id.empty()) return fail(Errc::InvalidArgument, x.say("context_receipt_which"));
  // One reading for this action and for the perspective sent_to_model (chat::formats::stored_receipt): the record lives with the message the person
  // sent; asked about the answer, the request that made it is the message before it.
  auto stored = chat::formats::stored_receipt(x.rt.db(), id);
  if (!stored) return fail(stored.error());
  const std::string request_id = stored->request_msg_id;
  const Json receipt = stored->receipt;
  if (!receipt.is_object())
    return answer(x.say("context_receipt_missing"), Json{{"msg_id", id}, {"request_msg_id", request_id}, {"missing_reason", "before_m1"},
                                                          {"also_when", Json::array({"context_receipt_store=never", "trace_context=false"})}});
  std::vector<std::string> lines;
  lines.push_back(x.say("context_receipt_head", Json{{"tokens", receipt.value("tokens_est", 0)}, {"sections", receipt.value("sections", Json::array()).size()}}));
  for (const auto& s : receipt.value("sections", Json::array()))
    lines.push_back("• " + x.say("context_receipt_section", Json{{"class", s.value("class", "")}, {"items", s.contains("items") && !s["items"].empty() ? s["items"].size() : s.value("items_omitted", 0)}, {"tokens", s.value("tokens_est", 0)}}));
  for (const auto& d : receipt.value("dropped", Json::array()))
    lines.push_back("• " + x.say("context_receipt_dropped", Json{{"reason", d.value("reason", "")}, {"count", d.value("count", 0)}, {"what", d.value("ref", "")}}));
  lines.push_back(x.say("context_receipt_note"));
  return answer(join_lines(lines), Json{{"msg_id", id}, {"request_msg_id", request_id}, {"receipt", receipt}});
}

// ── the interview of the person (M1-K2b-1): the words, the setting and the profile are the pack's, the store's and the config's ─────────

// profile_detail_level is written like any setting: the read-only rule and the declared values of its field apply.
Status write_profile_setting(Runtime& rt, const std::string& key, const Json& value) {
  if (auto ok = check_config_write(key, rt.paths().root); !ok) return ok;
  auto doc = settings_fields(rt.paths().root);
  if (!doc) return doc.error();
  const Json fields = doc->value("fields", Json::array());
  for (const auto& f : fields)
    if (json::get_string(f, "key") == key) {
      auto declared = check_declared_value(f, value);
      if (!declared) return declared.error();
      break;
    }
  rt.config().set(key, value);
  return rt.config().save();
}

using ProfileFn = Result<profile::Reply> (*)(const profile::Env&, const Json&);

StepOutcome profile_run(Ctx& x, ProfileFn fn, std::initializer_list<const char*> names) {
  Ctx lx = x;  // the language of the words may be named by the call (locale), the run's own otherwise
  if (const std::string loc = x.str("locale"); !loc.empty()) lx.lang = loc.substr(0, loc.find_first_of("-_"));
  Json args = Json::object();
  for (const char* n : names)
    if (x.has(n)) args[n] = x.arg(n);
  Ctx* cx = &lx;
  Runtime* rt = &x.rt;
  profile::Env env{x.rt.db(),
                   lx.lang,
                   json::get_string(lx.cfg.value("profile", Json::object()), "default_user"),
                   [cx](const std::string& id, const Json& v) { return cx->say(id, v); },
                   [cx](const std::string& t) { return cx->fold(t); },
                   [rt](const std::string& k) { return rt->config().get(k); },
                   [rt](const std::string& k, const Json& v) { return write_profile_setting(*rt, k, v); },
                   lx.cfg.value("profile", Json::object()).value("words", Json::object()),
                   [rt](const std::string& what, const Json& payload) {
                     if (what == "conv_created") rt->bus().emit(events::kConvCreated, payload);
                   },
                   lx.cfg.value("profile", Json::object()).value("match_languages", Json(nullptr)),
                   lx.cfg.value("profile", Json::object()).value("restart", Json(nullptr))};
  auto r = fn(env, args);
  if (!r) return fail(r.error());
  return answer(r->text, r->data);
}

StepOutcome profile_interview_op(Ctx& x) {
  return profile_run(x, profile::interview, {"op", "slot", "text", "category", "section", "user_id", "expected_revision", "reopen", "erase", "mode", "locale", "conv_id", "in_conversation"});
}
StepOutcome profile_restart_op(Ctx& x) { return profile_run(x, profile::restart, {"user_id", "locale", "in_conversation"}); }
StepOutcome profile_show_op(Ctx& x) { return profile_run(x, profile::show, {"user_id", "locale"}); }
StepOutcome profile_set_op(Ctx& x) { return profile_run(x, profile::set, {"slot", "value", "user_id", "expected_revision", "locale"}); }
StepOutcome profile_confirm_op(Ctx& x) {
  return profile_run(x, profile::confirm, {"kind", "text", "section", "decision", "user_id", "expected_revision", "locale"});
}

StepOutcome inference_confirm_op(Ctx& x) { return profile_run(x, profile::confirm_inference, {"entry", "locale"}); }
StepOutcome inference_reject_op(Ctx& x) { return profile_run(x, profile::reject_inference, {"entry", "locale"}); }

// ── the units a conversation belongs to (M1-K2c-1): the kinds, the words and the conversation are the data's, the store's and the call's ─────────

using UnitFn = Result<units::Reply> (*)(const units::Env&, const Json&);

StepOutcome unit_run(Ctx& x, UnitFn fn, std::initializer_list<const char*> names) {
  Ctx lx = x;  // the language of the words may be named by the call (locale), the run's own otherwise
  if (const std::string loc = x.str("locale"); !loc.empty()) lx.lang = loc.substr(0, loc.find_first_of("-_"));
  Json args = Json::object();
  for (const char* n : names)
    if (x.has(n)) args[n] = x.arg(n);
  const Ctx* cx = &lx;
  units::Env env{x.rt.db(), x.rt.paths().root, lx.lang, x.conv_id(),
                 [cx](const std::string& id, const Json& v) { return cx->say(id, v); },
                 [cx](const std::string& t) { return cx->fold(t); }};
  auto r = fn(env, args);
  if (!r) return fail(r.error());
  return answer(r->text, r->data);
}

StepOutcome units_define_op(Ctx& x) { return unit_run(x, units::define, {"kind", "id", "label"}); }
StepOutcome units_list_op(Ctx& x) { return unit_run(x, units::list, {"kind"}); }
StepOutcome units_forget_op(Ctx& x) { return unit_run(x, units::forget, {"kind", "id"}); }
StepOutcome conversation_set_unit_op(Ctx& x) { return unit_run(x, units::set_unit, {"conv_id", "kind", "id"}); }
StepOutcome conversation_clear_unit_op(Ctx& x) { return unit_run(x, units::clear_unit, {"conv_id", "kind"}); }

// ── answers to advisories (G3b): the remembered "keep" / "don't ask" of loom/advisory.h through the action runner ─────────────────────────────
// The answers live in <data_dir>/advisory_ack.json; the preset policy is the view engine's (view_rules advisory_policy). Every write is reversible:
// answer "forget" (an open list in the preset: forget_answers) removes a remembered answer; advisory.policy reset puts the preset back.

Json advisory_preset_of(Ctx& x) {
  auto engine = views::ViewEngine::open(x.rt.paths().root);
  return engine ? engine->advisory_preset() : Json::object();
}

bool policy_lists(const Json& pol, const char* key, const std::string& value) {
  if (!pol.contains(key) || !pol.at(key).is_array()) return false;
  for (const auto& v : pol.at(key))
    if (v.is_string() && v.get<std::string>() == value) return true;
  return false;
}

// words of the run's language (a '|' list in chat_ops texts) — e.g. what "no" or "forever" is said with
bool said_one_of(const Ctx& x, const std::string& words_id, const std::string& value) {
  const std::string v = x.fold(trim_quotes(value));
  std::stringstream ss(x.say(words_id));
  for (std::string w; std::getline(ss, w, '|');)
    if (!w.empty() && x.fold(w) == v) return true;
  return false;
}

StepOutcome advisory_answer(Ctx& x) {
  const std::string code = trim_quotes(x.str("code"));
  if (code.empty()) return fail(Errc::InvalidArgument, x.say("adv_which_code"));
  const std::string fp = trim_quotes(x.str("fingerprint"));
  std::string choice = trim_quotes(x.str("answer", "dont_ask"));
  if (choice.empty()) choice = "dont_ask";
  const Json preset = advisory_preset_of(x);
  auto store = advisory::AckStore::open(x.rt.paths().root);
  const Json store_policy = store.document().at("policy");
  const Json pol = advisory::effective_policy(preset, store_policy, code);
  if (pol.contains("answer_aliases") && pol.at("answer_aliases").is_object() && pol.at("answer_aliases").contains(choice) &&
      pol.at("answer_aliases").at(choice).is_string())
    choice = pol.at("answer_aliases").at(choice).get<std::string>();
  Json data{{"code", code}, {"fingerprint", fp}, {"answer", choice}};
  const Json vars{{"code", code}};
  if (policy_lists(pol, "forget_answers", choice)) {
    if (fp.empty()) {  // no configuration named: the per-code "never ask" goes back to the layer below
      auto st = store.set_policy(Json{{"codes", Json{{code, nullptr}}}});
      if (!st) return fail(st.error());
      data["policy"] = store.document().at("policy");
      return answer(x.say("adv_forgot_code", vars), data);
    }
    auto n = store.forget(code, fp);
    if (!n) return fail(n.error());
    data["removed"] = *n;
    return answer(x.say(*n ? "adv_forgot" : "adv_nothing_to_forget", vars), data);
  }
  const bool transient = policy_lists(pol, "transient_choices", choice);
  data["transient"] = transient;
  if (fp.empty()) {  // about the code itself: a silencing answer turns asking off for the code (reversible: answer forget, advisory.policy reset)
    if (transient) return answer(x.say("adv_kept_once", vars), data);
    auto st = store.set_policy(Json{{"codes", Json{{code, Json{{"ask", false}}}}}});
    if (!st) return fail(st.error());
    data["policy"] = store.document().at("policy");
    return answer(x.say("adv_never_code", vars), data);
  }
  Json context = json_arg(x, "context");
  if (!context.is_object()) context = Json::object();
  auto st = store.acknowledge(code, fp, choice, context);
  if (!st) return fail(st.error());
  data["ack"] = store.find(code, fp);
  return answer(x.say(transient ? "adv_kept_once" : "adv_remembered", vars), data);
}

StepOutcome advisory_list(Ctx& x) {
  const Json preset = advisory_preset_of(x);
  auto store = advisory::AckStore::open(x.rt.paths().root);
  const Json doc = store.document();
  const Json& stored = doc.at("acks");
  std::vector<Json> rows;
  for (auto it = stored.begin(); it != stored.end(); ++it) rows.push_back(it.value());
  std::sort(rows.begin(), rows.end(), [](const Json& a, const Json& b) { return json::get_string(a, "ack_at") > json::get_string(b, "ack_at"); });
  Json acks = Json::array();
  std::vector<std::string> lines;
  for (const auto& r : rows) {
    acks.push_back(r);
    lines.push_back("• " + x.say("adv_entry", Json{{"code", json::get_string(r, "code")}, {"choice", json::get_string(r, "choice")}, {"at", json::get_string(r, "ack_at")}}));
  }
  const Json& policy = doc.at("policy");
  Json data{{"schema", "loom.advisory_answers/1"}, {"acks", acks}, {"policy", policy}, {"preset", preset},
            {"effective", advisory::effective_policy(preset, policy, "")}, {"issues", store.issues()}};
  std::string text = x.say(rows.empty() ? "adv_list_empty" : "adv_list_head", Json{{"count", rows.size()}});
  if (!lines.empty()) text += "\n" + join_lines(lines);
  return answer(text, data);
}

StepOutcome advisory_policy(Ctx& x) {
  const std::string code = trim_quotes(x.str("code"));
  Json patch = Json::object();
  const Json reset = json_arg(x, "reset");
  if ((reset.is_boolean() && reset.get<bool>()) || (reset.is_string() && !said_one_of(x, "adv_no_words", reset.get<std::string>()) && !reset.get<std::string>().empty())) {
    patch["ask"] = nullptr;
    patch["remember_days"] = nullptr;
  }
  if (x.has("ask")) {
    const Json a = json_arg(x, "ask");
    if (a.is_boolean()) patch["ask"] = a;
    else if (a.is_number()) patch["ask"] = a.get<double>() != 0.0;
    else patch["ask"] = !said_one_of(x, "adv_no_words", a.is_string() ? a.get<std::string>() : a.dump());
  }
  if (x.has("remember_days")) {
    const Json d = json_arg(x, "remember_days");
    if (d.is_number()) {
      patch["remember_days"] = d;
    } else {
      const std::string said = trim_quotes(d.is_string() ? d.get<std::string>() : d.dump());
      char* end = nullptr;
      const double n = std::strtod(said.c_str(), &end);
      if (end != said.c_str() && std::isfinite(n)) patch["remember_days"] = n;
      else if (said_one_of(x, "adv_forever_words", said)) patch["remember_days"] = nullptr;
      else return fail(Errc::InvalidArgument, x.say("adv_days_what", Json{{"value", said}}));
    }
  }
  if (patch.empty()) return fail(Errc::InvalidArgument, x.say("adv_policy_what"));
  auto store = advisory::AckStore::open(x.rt.paths().root);
  auto st = store.set_policy(code.empty() ? patch : Json{{"codes", Json{{code, patch}}}});
  if (!st) return fail(st.error());
  const Json preset = advisory_preset_of(x);
  const Json policy = store.document().at("policy");
  const Json eff = advisory::effective_policy(preset, policy, code);
  const Json days = eff.value("remember_days", Json(nullptr));
  const Json vars{{"scope", code.empty() ? x.say("adv_scope_all") : code},
                  {"ask", json::get_bool(eff, "ask", true) ? x.say("adv_ask_yes") : x.say("adv_ask_no")},
                  {"days", days.is_number() ? json::dump(days) : x.say("adv_days_forever")}};
  return answer(x.say("adv_policy_set", vars), Json{{"code", code}, {"patch", patch}, {"policy", policy}, {"preset", preset}, {"effective", eff}});
}

// ── R101 D1: pinning on a desktop (PULPIT_R101 §4.3) ────────────────
// A pin is the graph link `pinned_on` (target node -> desktop node) in the links table; a thing that is an address and not a row (kind:chat,
// desktop:main, trash:, conv:<id>, sentence:<text>...) gets a representative node (label = the address, metadata.address) of the node kind
// graph_views.pack -> node_addresses names for its prefix; an unlisted prefix is pinned as `unknown_kind` (preset `address`), never refused.
// window.unpin removes the link and nothing else (pinning again restores it); the soft step through the trash is D4.

Json node_addresses_of(Ctx& x) {
  if (auto p = RuntimeProfile::load("graph_views", x.rt.paths().root)) {
    const Json v = p->values();
    if (const Json* na = json::find(v, "node_addresses"); na && na->is_object()) return *na;
  }
  return Json::object();
}

std::string address_node_kind(const Json& na, const std::string& address) {
  std::string best, kind;
  if (const Json* pre = json::find(na, "prefixes"); pre && pre->is_object()) {
    for (const auto& [prefix, entry] : pre->items()) {
      if (prefix.size() <= best.size() || address.rfind(prefix, 0) != 0) continue;
      const Json* k = json::find(entry, "node_kind");
      if (k && k->is_string()) { best = prefix; kind = k->get<std::string>(); }
    }
  }
  if (!kind.empty()) return kind;
  const Json* u = json::find(na, "unknown_kind");
  return u && u->is_string() ? u->get<std::string>() : std::string("address");
}

Result<std::string> address_node(Ctx& x, const Json& na, const std::string& address) {
  if (address.rfind("n_", 0) == 0) {   // an existing node of the graph is pinned as itself
    auto n = x.rt.db().get_node(address);
    if (!n) return n.error();
    if (*n) return address;
  }
  NodeOptions opts;
  opts.metadata = Json{{"address", address}, {"representative", true}};
  return x.rt.db().get_or_create_node(address, address_node_kind(na, address), opts);
}

std::string pin_desktop(Ctx& x, const Json& na) {
  std::string d = trim_quotes(x.str("desktop"));
  if (!d.empty()) return d;
  const Json* dd = json::find(na, "default_desktop");
  return dd && dd->is_string() ? dd->get<std::string>() : std::string("desktop:main");
}

StepOutcome window_pin(Ctx& x) {
  const std::string target = trim_quotes(x.str("target", x.element_id()));
  if (target.empty()) return fail(Errc::InvalidArgument, x.say("pin_what"));
  const Json na = node_addresses_of(x);
  const std::string desktop = pin_desktop(x, na);
  auto dn = address_node(x, na, desktop);
  if (!dn) return fail(dn.error());
  auto tn = address_node(x, na, target);
  if (!tn) return fail(tn.error());
  auto into = x.rt.db().get_links(std::string_view(*dn), std::string_view("pinned_on"));
  if (!into) return fail(into.error());
  std::int64_t order = 0;
  for (const auto& l : *into) {
    if (l.dst != *dn) continue;
    if (l.src == *tn)
      return answer(x.say("pin_already", Json{{"target", target}, {"desktop", desktop}}),
                    Json{{"pinned", true}, {"changed", false}, {"link", l.id}, {"node", *tn}, {"desktop_node", *dn}, {"desktop", desktop}});
    ++order;
  }
  const std::string label = trim_quotes(x.str("label"));
  const Json meta{{"order", order}, {"pinned_at", timeutil::utc_now_iso()}, {"label_override", label.empty() ? Json(nullptr) : Json(label)},
                  {"glyph", nullptr}, {"address", target}, {"desktop", desktop}};
  auto lid = x.rt.db().create_link(*tn, *dn, "pinned_on", 1.0, meta);
  if (!lid) return fail(lid.error());
  return answer(x.say("pinned", Json{{"target", target}, {"desktop", desktop}}),
                Json{{"pinned", true}, {"changed", true}, {"link", *lid}, {"node", *tn}, {"desktop_node", *dn}, {"desktop", desktop}, {"metadata", meta}});
}

StepOutcome window_unpin(Ctx& x) {
  const std::string target = trim_quotes(x.str("target", x.element_id()));
  if (target.empty()) return fail(Errc::InvalidArgument, x.say("pin_what"));
  const Json na = node_addresses_of(x);
  const std::string desktop = pin_desktop(x, na);
  auto dnode = x.rt.db().find_node(desktop, address_node_kind(na, desktop));
  if (!dnode) return fail(dnode.error());
  std::string tid = target;
  if (target.rfind("n_", 0) != 0) {
    auto tnode = x.rt.db().find_node(target, address_node_kind(na, target));
    if (!tnode) return fail(tnode.error());
    tid = *tnode ? (*tnode)->id : std::string();
  }
  std::vector<std::string> removed;
  if (*dnode && !tid.empty()) {
    const std::string did = (*dnode)->id;
    auto links = x.rt.db().get_links(std::string_view(tid), std::string_view("pinned_on"));
    if (!links) return fail(links.error());
    for (const auto& l : *links)
      if (l.src == tid && l.dst == did) {
        if (auto st = x.rt.db().delete_link(l.id); !st) return fail(st.error());
        removed.push_back(l.id);
      }
  }
  const Json vars{{"target", target}, {"desktop", desktop}};
  return answer(x.say(removed.empty() ? "unpin_none" : "unpinned", vars),
                Json{{"pinned", false}, {"changed", !removed.empty()}, {"removed", removed}, {"desktop", desktop}, {"target", target}});
}

// R101 D4: a pin to the trash = the link pinned_on gets metadata.trash = {at, from} (create_link upserts the whole metadata, so it is read,
// merged and written back); the desktop draws no trashed pin; pin.untrash erases the field; emptying the trash removes the link.
Result<std::vector<Link>> pin_links(Ctx& x, const std::string& target, const std::string& desktop) {
  const Json na = node_addresses_of(x);
  std::vector<Link> out;
  auto dnode = x.rt.db().find_node(desktop, address_node_kind(na, desktop));
  if (!dnode) return dnode.error();
  std::string tid = target;
  if (target.rfind("n_", 0) != 0) {
    auto tnode = x.rt.db().find_node(target, address_node_kind(na, target));
    if (!tnode) return tnode.error();
    tid = *tnode ? (*tnode)->id : std::string();
  }
  if (!*dnode || tid.empty()) return out;
  const std::string did = (*dnode)->id;
  auto links = x.rt.db().get_links(std::string_view(tid), std::string_view("pinned_on"));
  if (!links) return links.error();
  for (const auto& l : *links)
    if (l.src == tid && l.dst == did) out.push_back(l);
  return out;
}

StepOutcome pin_trash_step(Ctx& x, bool to_trash) {
  const std::string target = trim_quotes(x.str("target", x.element_id()));
  if (target.empty()) return fail(Errc::InvalidArgument, x.say("pin_what"));
  const std::string desktop = pin_desktop(x, node_addresses_of(x));
  auto links = pin_links(x, target, desktop);
  if (!links) return fail(links.error());
  std::vector<std::string> changed;
  for (const auto& l : *links) {
    Json meta = l.metadata.is_object() ? l.metadata : Json::object();
    if (meta.contains("trash") == to_trash) continue;
    if (to_trash) meta["trash"] = Json{{"at", timeutil::utc_now_iso()}, {"from", "desktop"}};
    else meta.erase("trash");
    auto id = x.rt.db().create_link(l.src, l.dst, "pinned_on", l.weight, meta);
    if (!id) return fail(id.error());
    changed.push_back(*id);
  }
  const Json vars{{"target", target}, {"desktop", desktop}};
  return answer(x.say(changed.empty() ? "pin_trash_none" : (to_trash ? "pin_trashed" : "pin_untrashed"), vars),
                Json{{"changed", !changed.empty()}, {"links", changed}, {"desktop", desktop}, {"target", target}, {"trashed", to_trash}});
}
StepOutcome pin_trash(Ctx& x) { return pin_trash_step(x, true); }
StepOutcome pin_untrash(Ctx& x) { return pin_trash_step(x, false); }

// ── the trash window's kernel side: what is in the trash, bring one back, empty ──
// trash_retention_days: unset = the preset null (never emptied by itself). A number only marks what is older as `due` and the page asks
// before the first emptying (advisory trash_retention_due); trash.empty with older_than_days empties just that.

double age_days(const std::string& at) {
  auto tp = timeutil::parse_iso_utc(at);
  if (!tp) return 0.0;
  return std::chrono::duration<double>(timeutil::Clock::now() - *tp).count() / 86400.0;
}

Result<Json> trash_items(Ctx& x) {
  Json items = Json::array();
  auto convs = x.rt.db().list_convs(-1);
  if (!convs) return convs.error();
  for (const auto& c : *convs) {
    if (!conv_trashed(c)) continue;
    const Json t = c.metadata.at("trash");
    items.push_back(Json{{"kind", "conversation"}, {"id", c.id}, {"title", c.title}, {"at", json::get_string(t, "at")}, {"from", json::get_string(t, "from")}});
  }
  for (const auto& n : x.rt.memory().get_all()) {
    if (!n.metadata.is_object() || !n.metadata.contains("trash")) continue;
    const Json t = n.metadata.at("trash");
    items.push_back(Json{{"kind", "memory"}, {"id", n.id}, {"title", clip(n.content, 120)}, {"at", json::get_string(t, "at")}, {"from", "memory"}});
  }
  auto links = x.rt.db().get_links(std::nullopt, std::string_view("pinned_on"));
  if (!links) return links.error();
  for (const auto& l : *links) {
    if (!l.metadata.is_object() || !l.metadata.contains("trash")) continue;
    const Json t = l.metadata.at("trash");
    items.push_back(Json{{"kind", "pin"}, {"id", l.id}, {"title", json::get_string(l.metadata, "address")}, {"desktop", json::get_string(l.metadata, "desktop")},
                         {"at", json::get_string(t, "at")}, {"from", json::get_string(t, "from")}});
  }
  return items;
}

StepOutcome trash_list(Ctx& x) {
  auto items = trash_items(x);
  if (!items) return fail(items.error());
  const Json days = x.rt.config().get("trash_retention_days");
  Json retention{{"days", days.is_number() ? days : Json(nullptr)}, {"due", 0}};
  std::vector<std::string> lines;
  std::size_t due = 0;
  for (auto& it : *items) {
    if (days.is_number() && age_days(json::get_string(it, "at")) > days.get<double>()) { it["due"] = true; ++due; }
    lines.push_back("• " + x.say("trash_line", Json{{"kind", x.say("trash_kind_" + json::get_string(it, "kind"))}, {"title", json::get_string(it, "title")},
                                                     {"at", json::get_string(it, "at").substr(0, 10)}, {"id", json::get_string(it, "id")}}));
  }
  retention["due"] = due;
  if (lines.empty()) return answer(x.say("trash_empty_now"), Json{{"items", *items}, {"count", 0}, {"retention", retention}});
  return answer(x.say("trash_head", Json{{"count", lines.size()}}) + "\n" + join_lines(lines), Json{{"items", *items}, {"count", items->size()}, {"retention", retention}});
}

// „przywróć z kosza …”: one thing of the trash named by its id or by words of its title, of any kind the kernel keeps.
StepOutcome trash_restore(Ctx& x) {
  const std::string given = trim_quotes(x.str("query", x.str("id")));
  if (utf8::is_blank(given)) return fail(Errc::InvalidArgument, x.say("trash_what"));
  auto items = trash_items(x);
  if (!items) return fail(items.error());
  const std::string needle = utf8::to_lower(given);
  Json found = Json::array();
  for (const auto& it : *items)
    if (json::get_string(it, "id") == given || contains_ci(json::get_string(it, "title"), needle)) found.push_back(it);
  if (found.empty()) return fail(Errc::NotFound, x.say("trash_none", Json{{"query", given}}));
  if (found.size() > 1) {
    std::vector<std::string> lines;
    for (const auto& it : found) lines.push_back("• " + x.say("trash_line", Json{{"kind", x.say("trash_kind_" + json::get_string(it, "kind"))}, {"title", json::get_string(it, "title")},
                                                                                {"at", json::get_string(it, "at").substr(0, 10)}, {"id", json::get_string(it, "id")}}));
    return fail(Errc::Conflict, x.say("trash_ambiguous", Json{{"count", found.size()}}) + "\n" + join_lines(lines));
  }
  const Json one = found[0];
  const std::string kind = json::get_string(one, "kind"), id = json::get_string(one, "id");
  if (kind == "conversation") {
    if (auto st = conv_merge_metadata(x, id, [](Json& m) { m.erase("trash"); }); !st) return fail(st.error());
  } else if (kind == "memory") {
    return memory_trash_step(x, false, id);
  } else if (kind == "pin") {
    auto links = x.rt.db().get_links(std::nullopt, std::string_view("pinned_on"));
    if (!links) return fail(links.error());
    for (const auto& l : *links) {
      if (l.id != id) continue;
      Json meta = l.metadata;
      meta.erase("trash");
      if (auto r = x.rt.db().create_link(l.src, l.dst, "pinned_on", l.weight, meta); !r) return fail(r.error());
    }
  }
  return answer(x.say("trash_restored", Json{{"kind", x.say("trash_kind_" + kind)}, {"title", json::get_string(one, "title")}}), Json{{"restored", one}});
}

// „opróżnij kosz”: today's irreversible steps for each thing in the trash (catalogued irreversible, asked by confirm:trash.empty) - a
// conversation is deleted with its messages (conversation.delete), a memory entry is forgotten as memory.forget forgets it (its words kept
// or dropped by the same preset chat_ops forget.keep_content), a trashed pin link is removed. older_than_days / kind narrow it; id empties one.
StepOutcome trash_empty(Ctx& x) {
  auto items = trash_items(x);
  if (!items) return fail(items.error());
  const Json older = x.arg("older_than_days");
  const std::string only_kind = trim_quotes(x.str("kind"));
  const std::string only_id = trim_quotes(x.str("id"));
  const bool keep = json::get_bool(x.cfg.value("forget", Json::object()), "keep_content", true);
  const std::string when = timeutil::utc_now_iso();
  Json done = Json::array();
  for (const auto& it : *items) {
    const std::string kind = json::get_string(it, "kind"), id = json::get_string(it, "id");
    if (!only_kind.empty() && kind != only_kind) continue;
    if (!only_id.empty() && id != only_id) continue;
    if (older.is_number() && age_days(json::get_string(it, "at")) <= older.get<double>()) continue;
    if (kind == "conversation") {
      if (auto st = x.rt.db().delete_conv(id); !st) return fail(st.error());
    } else if (kind == "memory") {
      auto entry = x.rt.memory().get_node(id);
      if (!entry) continue;
      Json meta = entry->metadata.is_object() ? entry->metadata : Json::object();
      const Json t = meta.value("trash", Json::object());
      meta["forgotten"] = Json{{"time", when}, {"by", "user"}, {"kept", keep}, {"was_active", json::get_bool(t, "was_active", true)}, {"via", "trash.empty"}};
      meta.erase("trash");
      Json fields{{"active", false}, {"metadata", meta}};
      if (!keep) fields["content"] = "";
      if (auto st = x.rt.memory().update_node(id, fields); !st) return fail(st.error());
    } else if (kind == "pin") {
      if (auto st = x.rt.db().delete_link(id); !st) return fail(st.error());
    }
    done.push_back(it);
  }
  return answer(x.say(done.empty() ? "trash_emptied_none" : "trash_emptied", Json{{"count", done.size()}}), Json{{"emptied", done}, {"count", done.size()}});
}

// ── R102 G2: text genesis (text_genesis.h) ──────────────────────────────────────────────────────────────────────────
struct GenesisSetup {
  text::Segmenter seg;
  text::genesis::Options opt;
};

// Presets: the embedded text pack; `level` of the call overrides the pack's level; the text's language is `language`
// of the call, else the language of the run (pl/en) — the pack's "*" rules cover a language it does not know.
Result<GenesisSetup> genesis_setup(Ctx& x) {
  const Json& pack = text::builtin_pack();
  LOOM_TRY_ASSIGN(auto opt, text::genesis::Options::from_pack(pack));
  const std::string level = x.str("level");
  if (!level.empty()) opt.level = level;
  LOOM_TRY_ASSIGN(auto seg, text::Segmenter::from_pack(pack, x.str("language", x.lang)));
  return GenesisSetup{std::move(seg), std::move(opt)};
}

// The message a genesis question is about: named (message_id / id), the element when it is a message, the message the
// question was asked from (context.message_id), else the last message of the conversation.
Result<std::string> genesis_message_id(Ctx& x) {
  std::string id = x.str("message_id");
  if (id.empty() && x.element_class() == "message") id = x.element_id();
  if (id.empty()) id = x.str("id");
  if (id.empty()) id = json::get_string(x.c.context, "message_id");
  if (id.empty()) {
    const std::string conv = x.conv_id();
    if (!conv.empty()) {
      LOOM_TRY_ASSIGN(auto msgs, x.rt.db().get_msgs(conv, false));
      if (!msgs.empty()) id = msgs.back().id;
    }
  }
  if (id.empty()) return Error(Errc::InvalidArgument, x.say("genesis_no_message"));
  return id;
}

Json genesis_version_vars(const Json& v) {
  return Json{{"ordinal", v.value("ordinal", 0)}, {"when", v.value("when", std::string())}, {"actor", v.value("actor", std::string())}};
}

Result<Json> genesis_ask(Ctx& x, const std::string& question, const std::string& text) {
  LOOM_TRY_ASSIGN(auto su, genesis_setup(x));
  text::genesis::Query q;
  q.question = question;
  q.text = text;
  q.from_id = x.str("from_id");
  q.to_id = x.str("to_id");
  if (q.from_id.empty() || q.to_id.empty()) {
    LOOM_TRY_ASSIGN(q.message_id, genesis_message_id(x));
  }
  return text::genesis::query(x.rt.db(), su.seg, q, su.opt);
}

StepOutcome genesis_origin(Ctx& x) {
  const std::string text = trim_quotes(x.str("text"));
  if (text.empty()) return fail(Errc::InvalidArgument, "text is required");
  auto r = genesis_ask(x, "origin", text);
  if (!r) return fail(r.error());
  const Json& a = *r;
  if (a.at("matches").empty()) return answer(x.say("genesis_none", Json{{"text", text}}), a);
  std::vector<std::string> lines;
  for (const auto& m : a.at("matches")) {
    const Json origin = m.value("origin", Json::object());
    const Json fs = m.value("final_state", Json::object());
    const std::string st = fs.value("state", std::string());
    std::string state = st;
    if (st == "present") state = x.say("genesis_state_present");
    else if (st == "gone") {
      Json vars = genesis_version_vars(fs.value("version", Json::object()));
      vars["event"] = fs.value("event", std::string());
      state = x.say("genesis_state_gone", vars);
    }
    std::size_t places = 0;
    for (const auto& g : a.value("graph", Json::array()))
      if (g.value("fingerprint", std::string()) == m.value("fingerprint", std::string())) places = g.value("occurrences", Json::array()).size();
    Json vars = genesis_version_vars(origin.value("version", Json::object()));
    vars["text"] = m.value("text", std::string());
    vars["how"] = origin.value("how", std::string()) == "first_version" ? x.say("genesis_origin_first") : x.say("genesis_origin_new");
    vars["state"] = state;
    vars["elsewhere"] = places ? x.say("genesis_elsewhere", Json{{"count", places}}) : std::string();
    lines.push_back("• " + x.say("genesis_origin_line", vars));
  }
  std::string out = x.say("genesis_origin_head", Json{{"text", text}, {"count", lines.size()}}) + "\n" + join_lines(lines);
  for (const auto& ad : a.at("advisories"))
    if (ad.value("code", std::string()) == "not_materialized") out += "\n" + x.say("genesis_not_materialized");
  return answer(out, a);
}

StepOutcome genesis_replaced(Ctx& x) {
  const std::string text = trim_quotes(x.str("text"));
  if (text.empty()) return fail(Errc::InvalidArgument, "text is required");
  auto r = genesis_ask(x, "replaced", text);
  if (!r) return fail(r.error());
  const Json& a = *r;
  if (a.at("matches").empty()) return answer(x.say("genesis_none", Json{{"text", text}}), a);
  const std::size_t chars = static_cast<std::size_t>(x.limit("snippet_chars", 160));
  std::vector<std::string> lines;
  for (const auto& m : a.at("matches")) {
    lines.push_back(x.say("genesis_replaced_head", Json{{"text", m.value("text", std::string())}}));
    const Json succ = m.value("successors", Json::array());
    for (const auto& s : succ) {
      Json vars = genesis_version_vars(s.value("version", Json::object()));
      vars["event"] = s.value("event", std::string());
      vars["after"] = clip(s.value("text", std::string()), chars);
      lines.push_back("• " + x.say("genesis_replaced_line", vars));
    }
    const Json fs = m.value("final_state", Json::object());
    if (fs.value("state", std::string()) == "gone" && fs.value("successor", std::string()).empty())
      lines.push_back("• " + x.say("genesis_replaced_removed", genesis_version_vars(fs.value("version", Json::object()))));
    else if (succ.empty() && fs.value("state", std::string()) == "present") lines.push_back("• " + x.say("genesis_replaced_untouched"));
  }
  return answer(join_lines(lines), a);
}

StepOutcome genesis_missing(Ctx& x) {
  auto r = genesis_ask(x, "missing", std::string());
  if (!r) return fail(r.error());
  const Json& a = *r;
  if (a.value("from", Json(nullptr)).is_null()) return answer(x.say("genesis_missing_first"), a);
  const std::size_t chars = static_cast<std::size_t>(x.limit("snippet_chars", 160));
  const Json removed = a.value("removed", Json::array()), replaced = a.value("replaced", Json::array()), moved = a.value("moved", Json::array()), added = a.value("added", Json::array());
  std::vector<std::string> lines;
  lines.push_back(x.say("genesis_missing_head", Json{{"to", a.at("to").value("ordinal", 0)}, {"from", a.at("from").value("ordinal", 0)}, {"removed", removed.size()},
                                                     {"replaced", replaced.size()}, {"moved", moved.size()}, {"added", added.size()}, {"kept", a.value("kept", 0)}}));
  for (const auto& e : removed) lines.push_back("• " + x.say("genesis_missing_removed", Json{{"text", clip(e.value("text_before", std::string()), chars)}}));
  for (const auto& e : replaced)
    lines.push_back("• " + x.say("genesis_missing_replaced", Json{{"before", clip(e.value("text_before", std::string()), chars)}, {"after", clip(e.value("text_after", std::string()), chars)}}));
  for (const auto& e : moved) lines.push_back("• " + x.say("genesis_missing_moved", Json{{"text", clip(e.value("text_before", std::string()), chars)}}));
  for (const auto& e : added) lines.push_back("• " + x.say("genesis_missing_added", Json{{"text", clip(e.value("text_after", std::string()), chars)}}));
  return answer(join_lines(lines), a);
}

StepOutcome genesis_materialize(Ctx& x) {
  auto su = genesis_setup(x);
  if (!su) return fail(su.error());
  auto& db = x.rt.db();
  std::vector<std::vector<text::genesis::VersionText>> sources;
  const std::string scope = x.str("scope", "conversation");
  if (scope == "message") {
    auto mid = genesis_message_id(x);
    if (!mid) return fail(mid.error());
    auto v = text::genesis::versions_of_message(db, *mid);
    if (!v) return fail(v.error());
    sources.push_back(std::move(*v));
  } else {
    std::string conv = x.conv_id();
    if (conv.empty()) {
      auto mid = genesis_message_id(x);
      if (!mid) return fail(mid.error());
      auto m = db.get_msg(*mid);
      if (!m) return fail(m.error());
      if (*m) conv = (*m)->conv_id;
    }
    if (conv.empty()) return fail(Errc::InvalidArgument, x.say("genesis_no_message"));
    auto v = text::genesis::chains_of_conversation(db, conv);
    if (!v) return fail(v.error());
    sources = std::move(*v);
  }
  text::genesis::MaterializeSummary total;
  for (const auto& s : sources) {
    auto chain = text::genesis::compute_chain(su->seg, s, su->opt);
    if (!chain) return fail(chain.error());
    auto sum = text::genesis::materialize(db, *chain, su->opt);
    if (!sum) return fail(sum.error());
    total.versions += sum->versions;
    total.fragments += sum->fragments;
    total.events += sum->events;
    total.nodes_created += sum->nodes_created;
    total.nodes_reused += sum->nodes_reused;
    total.links_written += sum->links_written;
  }
  Json data = total.to_json();
  data["chains"] = sources.size();
  return answer(x.say("genesis_materialized", Json{{"chains", sources.size()}, {"versions", total.versions}, {"fragments", total.fragments}, {"events", total.events},
                                                   {"created", total.nodes_created}, {"reused", total.nodes_reused}}),
                data);
}

using Op = StepOutcome (*)(Ctx&);

const std::map<std::string, Op>& table() {
  static const std::map<std::string, Op> t = {
      {"conversations.list", conversations_list}, {"conversation.create", conversation_create}, {"conversation.rename", conversation_rename},
      {"conversation.delete", conversation_delete}, {"message.edit", message_edit}, {"message.versions", message_versions},
      {"message.restore", message_restore}, {"message.set_status", message_set_status}, {"message.weight", message_weight},
      {"search.find", search_find}, {"graph.related", graph_related}, {"graph.overview", graph_overview}, {"graph.reindex", graph_reindex},
      {"genesis.origin", genesis_origin}, {"genesis.replaced", genesis_replaced}, {"genesis.missing", genesis_missing}, {"genesis.materialize", genesis_materialize},
      {"memory.recall", memory_recall}, {"memory.remember", memory_remember}, {"memory.update", memory_update}, {"memory.forget", memory_forget}, {"memory.restore", memory_restore},
      {"export.conversation", export_conversation}, {"import.run", import_run}, {"storage.connect", storage_connect},
      {"ports.show", ports_show}, {"ports.set", ports_set}, {"usage.spend", usage_spend}, {"usage.set_limit", usage_set_limit},
      {"usage.compact", usage_compact}, {"economy.report", economy_report}, {"models.list", models_list}, {"models.refresh", models_refresh},
      {"providers.list", providers_list}, {"settings.list", settings_list}, {"semantic.status", semantic_status}, {"semantic.pause", semantic_pause},
      {"semantic.resume", semantic_resume}, {"semantic.wake", semantic_wake}, {"semantic.requeue", semantic_requeue}, {"tasks.list", tasks_list},
      {"tasks.resume", tasks_resume}, {"tasks.cancel", tasks_cancel}, {"logs.show", logs_show}, {"provenance.show", provenance_show},
      {"sources.list", sources_list}, {"app.info", app_info}, {"github.sync", github_sync}, {"help.can_do", help_can_do}, {"help.how_to", help_how_to},
      {"archive.status", archive_status}, {"archive.cancel", archive_cancel}, {"knowledge.status", knowledge_status}, {"knowledge.cancel", knowledge_cancel},
      {"catalog.status", catalog_status}, {"catalog.cancel", catalog_cancel}, {"catalog.query", catalog_query},
      {"views.effective", views_effective}, {"views.write", views_write}, {"perspectives.show", perspectives_show}, {"scopes.explain", scopes_explain},
      {"view_model.show", view_model_show}, {"interaction.show", interaction_show}, {"interaction.rebind", interaction_rebind}, {"edge_kinds.list", edge_kinds_list}, {"edge_kinds.classify", edge_kinds_classify},
      {"graph.navigate", graph_navigate}, {"graph.card", graph_card}, {"settings.resolved", settings_resolved_op}, {"roles.resolved", roles_resolved_op},
      {"rom.override", rom_override_op}, {"context.modify", context_modify}, {"context.receipt", context_receipt},
      {"profile.interview", profile_interview_op}, {"profile.restart", profile_restart_op}, {"profile.show", profile_show_op}, {"profile.set", profile_set_op}, {"profile.confirm", profile_confirm_op},
      {"inference.confirm", inference_confirm_op}, {"inference.reject", inference_reject_op},
      {"units.define", units_define_op}, {"units.list", units_list_op}, {"units.forget", units_forget_op},
      {"conversation.set_unit", conversation_set_unit_op}, {"conversation.clear_unit", conversation_clear_unit_op},
      {"advisory.answer", advisory_answer}, {"advisory.list", advisory_list}, {"advisory.policy", advisory_policy},
      {"window.pin", window_pin}, {"window.unpin", window_unpin},
      {"conversation.trash", conversation_trash}, {"conversation.untrash", conversation_untrash}, {"memory.trash", memory_trash},
      {"memory.untrash", memory_untrash}, {"pin.trash", pin_trash}, {"pin.untrash", pin_untrash}, {"trash.list", trash_list},
      {"trash.restore", trash_restore}, {"trash.empty", trash_empty}};
  return t;
}

// The executors of the actions that start a pipeline: each is the second step of a composite (archive.run, knowledge.run, catalog.scan) whose first step
// picks the file when no path was said, so no sentence of its own reaches them and they are not among the ids the chat tests walk.
const std::map<std::string, Op>& starters() {
  static const std::map<std::string, Op> t = {{"archive.start", archive_start}, {"knowledge.start", knowledge_start}, {"catalog.start", catalog_start}};
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
  std::vector<std::pair<std::string, Op>> all(table().begin(), table().end());
  all.insert(all.end(), starters().begin(), starters().end());
  for (const auto& [id, op] : all) {
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
