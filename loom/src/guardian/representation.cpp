// The compiled representation of a conversation (level L3) and what the context selector reads (select()).
#include <algorithm>
#include <map>
#include <set>

#include "../chat/active_task_acceptance.h"
#include "../chat/active_task_spec.h"
#include "internal.h"
#include "loom/action_rules.h"
#include "loom/log.h"
#include "loom/runtime_profile.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"
#include "text.h"

namespace loom::guardian::detail {
namespace {

struct Statement {
  std::string id, kind, status = "active", text, message_id;
  std::vector<std::string> supersedes;
};

std::string kind_of_refinement(const Data& d, const std::string& txt) {
  const Json& tr = d.p.at("transforms").at("compile_refinements");
  const auto toks = text::tokenize(utf8::decode(txt), d.fold);
  for (const auto& rule : tr.at("statement_kinds")) {
    if (json::get_bool(rule, "default", false)) return json::get_string(rule, "kind", "style");
    std::vector<std::string> phrases;
    if (const Json* m = json::find(rule, "markers"); m && m->is_object())
      for (auto it = m->begin(); it != m->end(); ++it)
        for (const auto& p : text::strings_of(&it.value())) phrases.push_back(p);
    const bool at_start = json::get_bool(rule, "at_start", false);
    for (const auto& hit : text::find(toks, text::compile(phrases, d.fold)))
      if (!at_start || hit.first < 3) return json::get_string(rule, "kind", "style");
    // phrases ending in a space are word-prefix markers written for readability ("nie "): fold them as words
  }
  return "style";
}

std::string clip(const std::string& s, std::size_t max_chars) {
  std::string t(utf8::strip(s));
  return std::string(utf8::prefix(t, max_chars));
}

Json spec_of(const std::vector<Statement>& statements, const std::vector<Message>& messages, const std::string& conv_id, const std::string& product_id,
             std::uint64_t version, const Json& previous, const Data& d, Json* bindings_out) {
  std::map<std::string, std::string> event_of;
  Json history = Json::array(), refs = Json::array();
  std::string known = "1970-01-01T00:00:00Z";
  for (std::size_t i = 0; i < messages.size(); ++i) {
    const std::string ev = "event." + std::to_string(i);
    event_of[messages[i].id] = ev;
    history.push_back(ev);
    refs.push_back(Json{{"event_id", ev}, {"locator", {{"source", "native.messages"}, {"json_pointer", "/messages/" + std::to_string(i)}}}, {"known_at", messages[i].created}});
    if (messages[i].created > known) known = messages[i].created;
    if (bindings_out) (*bindings_out)[ev] = Json{{"message_id", messages[i].id}, {"text_sha256", sha256_hex(messages[i].text)}};
  }
  Json list = Json::array();
  std::string first_active;
  for (const auto& s : statements) {
    if (first_active.empty() && s.status == "active") first_active = s.id;
    list.push_back(Json{{"id", s.id}, {"kind", s.kind}, {"status", s.status}, {"text", s.text}, {"source_event_ids", Json::array({event_of[s.message_id]})},
                        {"claim_ids", Json::array()}, {"conditions", Json::array()}, {"supersedes", s.supersedes}});
  }
  const Json& tr = d.p.at("transforms").at("compile_refinements");
  return Json{{"schema", "loom.active_task_spec/1"}, {"product_ref", {{"kind", "product"}, {"id", product_id}}},
              {"goal_id", json::get_string(tr, "goal_prefix", "guardian-goal-") + conv_id}, {"knowledge_run", nullptr},
              {"scope", {{"conversation_id", conv_id}, {"branch_id", "native:active"}, {"task_id", json::get_string(tr, "task_id", "guardian")}}},
              {"version", version}, {"previous_product_ref", previous}, {"known_at", known}, {"representation", "derived_product"},
              {"materializer", tr.at("materializer")}, {"history_event_ids", history}, {"source_refs", refs}, {"statements", list},
              // The compiler regenerates this from the statements; it is only here to satisfy the envelope.
              {"compiled_instruction", {{"text", "placeholder"}, {"source_map", Json::array({Json{{"span", {{"byte_start", 0}, {"byte_len", 1}}}, {"statement_ids", Json::array({first_active})}}})}}}};
}

Json statements_json(const std::vector<Statement>& v) {
  Json out = Json::array();
  for (const auto& s : v)
    out.push_back(Json{{"id", s.id}, {"kind", s.kind}, {"status", s.status}, {"text", s.text}, {"message_id", s.message_id}, {"supersedes", s.supersedes}});
  return out;
}

}  // namespace

Result<std::vector<Node>> hypotheses_of(Database& db, const Data& d, const std::string& conv_id) {
  LOOM_TRY_ASSIGN(auto all, db.list_nodes(json::get_string(d.p.at("nodes"), "hypothesis", "guardian_hypothesis"), 100000));
  std::vector<Node> out;
  for (auto& n : all)
    if (json::get_string(n.metadata, "conv_id") == conv_id) out.push_back(std::move(n));
  return out;
}

Result<std::string> instruction_of(const Json& statements, const std::vector<std::string>& message_ids, Database& db, const std::string& conv_id) {
  // Draft spec for the preview only (version 1, no ancestry): the compiler's own text.
  std::vector<Statement> st;
  for (const auto& s : statements) {
    Statement x;
    x.id = s.value("id", "");
    x.kind = s.value("kind", "");
    x.status = s.value("status", "active");
    x.text = s.value("text", "");
    x.message_id = s.value("message_id", "");
    for (const auto& y : s.value("supersedes", Json::array())) x.supersedes.push_back(y.get<std::string>());
    st.push_back(std::move(x));
  }
  std::vector<Message> msgs;
  for (const auto& id : message_ids) {
    LOOM_TRY_ASSIGN(auto m, db.get_msg(id));
    if (m) msgs.push_back(*m);
  }
  LOOM_TRY_ASSIGN(auto prof, RuntimeProfile::builtin("guardian"));
  Data d;
  d.p = prof.values();
  const Json spec = spec_of(st, msgs, conv_id, "preview", 1, nullptr, d, nullptr);
  LOOM_TRY_ASSIGN(Json compiled, chat::compile_active_task_spec(spec));
  return compiled["compiled_instruction"]["text"].get<std::string>();
}

Result<Json> rebuild_representation(Database& db, const Data& d, const std::string& conv_id, const std::string& include_hyp, bool write) {
  const std::string kind = json::get_string(d.p.at("nodes"), "representation", "guardian_representation");
  const std::string label = "rep:" + conv_id;
  LOOM_TRY_ASSIGN(auto hyps, hypotheses_of(db, d, conv_id));
  const Json& kinds = d.p.at("kinds");
  const Node* run = nullptr;
  std::vector<const Node*> rules;
  for (const auto& h : hyps) {
    if (json::get_string(h.metadata, "status") != "promoted" && h.id != include_hyp) continue;
    const std::string promotes = kinds.value(json::get_string(h.metadata, "signal"), Json::object()).value("hypothesis", Json::object()).value("promotes", "");
    if (promotes == "transform") {
      if (!run || json::get_string(h.metadata, "promoted_at") > json::get_string(run->metadata, "promoted_at")) run = &h;
    } else if (promotes == "rule") {
      rules.push_back(&h);
    }
  }
  LOOM_TRY_ASSIGN(auto existing, db.find_node(label, kind));
  auto empty_out = [&]() -> Result<Json> {
    if (existing && write) {
      Json meta = existing->metadata;
      meta["status"] = "empty";
      meta["updated"] = timeutil::utc_now_iso();
      NodePatch p;
      p.metadata = meta;
      LOOM_TRY(db.update_node(existing->id, p));
    }
    return Json(nullptr);
  };
  if (!run && rules.empty()) return empty_out();

  LOOM_TRY_ASSIGN(auto msgs, db.get_msgs(conv_id));
  std::map<std::string, const Message*> by_id;
  for (const auto& m : msgs)
    if (m.status == msg_status::kActive) by_id[m.id] = &m;
  const Json& tr = d.p.at("transforms").at("compile_refinements");
  const std::size_t goal_max = static_cast<std::size_t>(json::get_int(tr, "goal_max_chars", 4000));
  const std::size_t stmt_max = static_cast<std::size_t>(json::get_int(tr, "statement_max_chars", 1200));
  auto text_for = [&](const Message& m) {
    std::optional<std::string> l1 = level_text(db, d, m, "L1");
    return l1 ? *l1 : m.text;
  };

  std::vector<Statement> st;
  std::set<std::string> used;
  std::string goal_id;
  if (run) goal_id = json::get_string(run->metadata, "goal_message_id");
  if (goal_id.empty() || !by_id.count(goal_id)) {
    goal_id.clear();
    const std::size_t min_words = static_cast<std::size_t>(json::get_int(tr, "goal_min_words", 4));
    for (const auto& m : msgs)
      if (m.status == msg_status::kActive && m.role == "user" && !is_traffic(d, m) && text::tokenize(utf8::decode(m.text), d.fold).size() >= min_words) {
        goal_id = m.id;
        break;
      }
  }
  if (goal_id.empty()) return empty_out();
  st.push_back(Statement{"goal", "goal", "active", clip(text_for(*by_id[goal_id]), goal_max), goal_id, {}});
  used.insert(goal_id);
  std::vector<std::size_t> refinement_idx;
  if (run) {
    std::vector<std::pair<std::string, std::string>> ordered;  // created, message id
    for (const auto& o : run->metadata.value("occurrences", Json::array())) {
      if (o.value("undone", false)) continue;
      const std::string mid = o.value("message_id", "");
      auto it = by_id.find(mid);
      if (it != by_id.end() && mid != goal_id && !used.count(mid)) ordered.emplace_back(it->second->created, mid);
    }
    std::sort(ordered.begin(), ordered.end());
    int n = 0;
    for (const auto& [created, mid] : ordered) {
      (void)created;
      const Message& m = *by_id[mid];
      refinement_idx.push_back(st.size());
      st.push_back(Statement{"r" + std::to_string(++n), kind_of_refinement(d, m.text), "active", clip(text_for(m), stmt_max), mid, {}});
      used.insert(mid);
    }
    // A later clarification that says the opposite of an earlier one replaces it (same subject, opposite polarity).
    const double min_overlap = json::get_number(d.p.at("weighting"), "overlap_min", 0.5);
    for (std::size_t j = 0; j < refinement_idx.size(); ++j)
      for (std::size_t i = 0; i < j; ++i) {
        Statement& a = st[refinement_idx[i]];
        Statement& b = st[refinement_idx[j]];
        if (a.status == "active" && overlap(subject_tokens(d, a.text), subject_tokens(d, b.text)) >= min_overlap && negative(d, a.text) != negative(d, b.text)) {
          a.status = "superseded";
          b.supersedes.push_back(a.id);
        }
      }
  }
  Json hyp_ids = Json::array();
  if (run) hyp_ids.push_back(run->id);
  int rn = 0;
  for (const Node* h : rules) {
    const Json& occ = h->metadata.value("occurrences", Json::array());
    std::string mid;
    for (const auto& o : occ)
      if (by_id.count(o.value("message_id", ""))) {
        mid = o.value("message_id", "");
        break;
      }
    if (mid.empty()) continue;
    const std::string claim = h->metadata.value("claim", Json::object()).value("text", "");
    const bool neg = negative(d, claim) || json::get_string(h->metadata, "signal") == "proposal_rejection";
    st.push_back(Statement{"p" + std::to_string(++rn), neg ? "prohibition" : "style", "active", clip(claim, stmt_max), mid, {}});
    int en = 0;
    for (const auto& ex : h->metadata.value("exceptions", Json::array())) {
      const std::string emid = by_id.count(ex.value("message_id", "")) ? ex.value("message_id", "") : mid;
      st.push_back(Statement{"p" + std::to_string(rn) + "x" + std::to_string(++en), "exception", "active", clip("Exception: " + ex.value("text", ""), stmt_max), emid, {}});
    }
    hyp_ids.push_back(h->id);
  }

  // Messages in event order (creation order), the statements' sources only.
  std::set<std::string> ids;
  for (const auto& s : st) ids.insert(s.message_id);
  std::vector<Message> ordered_msgs;
  for (const auto& m : msgs)
    if (ids.count(m.id) && m.status == msg_status::kActive) ordered_msgs.push_back(m);
  Json hashes = Json::object();
  Json message_ids = Json::array();
  for (const auto& m : ordered_msgs) {
    hashes[m.id] = sha256_hex(m.text);
    message_ids.push_back(m.id);
  }
  Json stj = statements_json(st);
  std::vector<std::string> mids;
  for (const auto& m : ordered_msgs) mids.push_back(m.id);
  LOOM_TRY_ASSIGN(std::string instruction, instruction_of(stj, mids, db, conv_id));
  const std::string digest = sha256_hex(json::dump(Json{{"statements", stj}, {"hashes", hashes}}));
  if (!write) return Json{{"instruction", instruction}, {"statements", stj}, {"messages", message_ids}, {"digest", digest}, {"preview", true}};
  Json meta{{"schema", "loom.guardian_representation/1"}, {"conv_id", conv_id}, {"goal_message_id", goal_id}, {"statements", stj}, {"messages", message_ids},
            {"source_hashes", hashes}, {"digest", digest}, {"status", "accepted"}, {"hypotheses", hyp_ids}, {"instruction", instruction}, {"updated", timeutil::utc_now_iso()}};
  std::string node_id;
  if (existing) {
    node_id = existing->id;
    meta["created"] = existing->metadata.value("created", timeutil::utc_now_iso());
    NodePatch p;
    p.metadata = meta;
    p.content = instruction;
    LOOM_TRY(db.update_node(node_id, p));
  } else {
    meta["created"] = timeutil::utc_now_iso();
    NodeOptions o;
    o.content = instruction;
    o.metadata = meta;
    o.tags = Json::array({"guardian_representation"});
    LOOM_TRY_ASSIGN(node_id, db.create_node(label, kind, o));
  }
  for (const auto& mid : mids) {
    LOOM_TRY_ASSIGN(auto linked, db.create_link(node_id, mid, json::get_string(d.p.at("nodes").at("links"), "evidence", "derived_from"), 1.0, Json{{"role", "represents"}}));
    (void)linked;
  }
  return Json{{"id", node_id}, {"instruction", instruction}, {"statements", stj}, {"messages", message_ids}, {"digest", digest}};
}

Result<Json> materialize(Database& db, const Data& d, const std::string& conv_id, const Json& meta) {
  const Json& tr = d.p.at("transforms").at("compile_refinements");
  std::vector<Message> msgs;
  for (const auto& idj : meta.value("messages", Json::array())) {
    const std::string id = idj.get<std::string>();
    LOOM_TRY_ASSIGN(auto m, db.get_msg(id));
    if (!m || m->conv_id != conv_id || m->status != msg_status::kActive) return Error(Errc::Conflict, "a message of the representation is gone or no longer active: " + id);
    if (sha256_hex(m->text) != meta.value("source_hashes", Json::object()).value(id, "")) return Error(Errc::Conflict, "a message of the representation changed: " + id);
    msgs.push_back(*m);
  }
  std::vector<Statement> st;
  for (const auto& s : meta.value("statements", Json::array())) {
    Statement x;
    x.id = s.value("id", "");
    x.kind = s.value("kind", "");
    x.status = s.value("status", "active");
    x.text = s.value("text", "");
    x.message_id = s.value("message_id", "");
    for (const auto& y : s.value("supersedes", Json::array())) x.supersedes.push_back(y.get<std::string>());
    st.push_back(std::move(x));
  }
  const std::string digest = meta.value("digest", "");
  const std::string product = json::get_string(tr, "product_prefix", "guardian-") + digest.substr(0, 16);
  auto lock = db.lock();
  LOOM_TRY_ASSIGN(auto history, chat::read_active_task_history(db, conv_id));
  const Json* latest = nullptr;
  Json my_scope{{"conversation_id", conv_id}, {"branch_id", "native:active"}, {"task_id", json::get_string(tr, "task_id", "guardian")}};
  for (const auto& snap : history.snapshots) {
    if (!chat::valid_active_task_snapshot(snap)) continue;
    const Json& old = snap["supplied_spec"];
    if (chat::active_task_scope_key(old["scope"]) != chat::active_task_scope_key(my_scope)) continue;
    if (!latest || old["version"].get<std::uint64_t>() > (*latest)["supplied_spec"]["version"].get<std::uint64_t>()) latest = &snap;
  }
  if (latest && (*latest)["supplied_spec"]["product_ref"]["id"] == product)  // the same content as the last send: the same bytes
    return Json{{"spec", (*latest)["supplied_spec"]}, {"bindings", (*latest)["bindings"]}, {"product", product}, {"version", (*latest)["supplied_spec"]["version"]}};
  Json bindings = Json::object();
  std::uint64_t version = 1;
  Json previous = nullptr;
  if (latest) {
    version = (*latest)["supplied_spec"]["version"].get<std::uint64_t>() + 1;
    previous = (*latest)["supplied_spec"]["product_ref"];
  }
  Json spec = spec_of(st, msgs, conv_id, product, version, previous, d, &bindings);
  LOOM_TRY_ASSIGN(Json compiled, chat::compile_active_task_spec(spec));
  (void)compiled;
  return Json{{"spec", spec}, {"bindings", bindings}, {"product", product}, {"version", version}};
}

}  // namespace loom::guardian::detail

namespace loom::guardian {

Result<Selection> select(Database& db, const Config& cfg, const std::string& conv_id) {
  Selection sel;
  sel.preset = "raw";
  if (conv_id.empty()) return sel;
  const Json pref = cfg.get("chat_representation");
  const bool has_pref = pref.is_string() && !pref.get<std::string>().empty() && pref.get<std::string>() != "raw";
  LOOM_TRY_ASSIGN(auto rep, db.find_node("rep:" + conv_id, "guardian_representation"));
  const bool has_rep = rep && json::get_string(rep->metadata, "status") == "accepted";
  // Rules about the person live in the user's overlay; without one there is nothing to look up.
  std::error_code ec;
  const bool has_overlay = std::filesystem::exists(cfg.path().parent_path() / "profiles" / "guardian.pack", ec);
  const Json tf_set = cfg.get("chat_thread_filter");
  const bool has_tf = tf_set.is_string() && !tf_set.get<std::string>().empty() && tf_set.get<std::string>() != "off";
  if (!has_pref && !has_rep && !has_tf && !has_overlay) return sel;  // nothing is chosen: the raw messages, at no cost

  const std::filesystem::path root = cfg.path().parent_path();
  LOOM_TRY_ASSIGN(auto d, detail::load_data(cfg, root));
  const Json& section = d.p.at("representation");
  // The value in the scope of the conversation (R47): the installation's preset, the user's setting, a rule of the conversation.
  std::string name = json::get_string(section, "active", "raw");
  if (has_pref) name = pref.get<std::string>();
  if (auto engine = actions::Engine::load(root)) {
    engine->set_user_values(cfg.all());
    std::vector<actions::Baseline> baselines;
    baselines.emplace_back("installation", json::get_string(section, "active", "raw"), "profile:guardian.representation.active");
    if (has_pref) baselines.emplace_back("person", pref, "setting:chat_representation");
    if (auto scope = engine->parse_scope(Json{{"conversation", conv_id}, {"turn", ""}})) {
      if (auto sv = engine->resolve_value("chat_representation", *scope, baselines); sv && sv->defined && sv->value.is_string()) name = sv->value.get<std::string>();
    }
  } else {
    log::write(log::Level::Debug, detail::kLog, "scope rules not readable, the setting decides: " + engine.error().message);
  }
  if (!section.at("presets").contains(name)) name = json::get_string(section, "unknown", "raw");
  sel.preset = name;
  const Json& preset = section.at("presets").at(name);
  const Json levels = preset.value("levels", Json::object());
  const std::string runs = preset.value("runs", "raw");
  if (levels.is_object() && !levels.empty()) {
    LOOM_TRY_ASSIGN(auto msgs, db.get_msgs(conv_id));
    for (const auto& m : msgs) {
      if (m.status != msg_status::kActive || !levels.contains(m.role)) continue;
      if (auto t = detail::level_text(db, d, m, levels[m.role].get<std::string>())) sel.substitutions[m.id] = *t;
    }
  }
  // Standing rules about the person: only their normalized text, only where the scope chain (R47) lets rules about him go with a request
  // (setting guardian_person_rules; a rule of the conversation can switch them off there), only while not expired.
  if (has_overlay) {
    const Json& priv = d.p.at("privacy");
    const Json& vis = priv.at("visibility");
    std::string allowed = json::get_string(vis, "active", "send");
    const Json mine = cfg.get(json::get_string(vis, "setting", "guardian_person_rules"));
    if (mine.is_string() && !mine.get<std::string>().empty()) allowed = mine.get<std::string>();
    if (auto engine = actions::Engine::load(root)) {
      engine->set_user_values(cfg.all());
      std::vector<actions::Baseline> baselines;
      baselines.emplace_back("installation", json::get_string(vis, "active", "send"), "profile:guardian.privacy.visibility.active");
      if (mine.is_string() && !mine.get<std::string>().empty()) baselines.emplace_back("person", mine, "setting:guardian_person_rules");
      if (auto scope = engine->parse_scope(Json{{"conversation", conv_id}, {"turn", ""}}))
        if (auto sv = engine->resolve_value("guardian_person_rules", *scope, baselines); sv && sv->defined && sv->value.is_string()) allowed = sv->value.get<std::string>();
    }
    if (!vis.at("presets").contains(allowed)) allowed = json::get_string(vis, "unknown", "send");
    if (allowed == "send") {
      const auto wide = text::strings_of(json::find(priv, "wide_scopes"));
      const std::string now = timeutil::utc_now_iso();
      const Json rules = d.p.value("rules", Json::object());
      std::string lines;
      for (auto it = rules.begin(); it != rules.end(); ++it) {
        const Json& r = it.value();
        if (!r.is_object() || r.value("disabled", false) || r.value("text", "").empty()) continue;
        if (std::find(wide.begin(), wide.end(), r.value("scope", "")) == wide.end()) continue;
        if (!r.value("expires", "").empty() && r.value("expires", "") < now) continue;  // expired: not applied (and not deleted)
        const std::string vin = r.value("visible_in", "all");
        if (vin == "origin" && r.value("conv_id", "") != conv_id) continue;
        std::string line = "- " + r.value("text", "");
        std::string ex;
        for (const auto& e : r.value("exceptions", Json::array())) ex += (ex.empty() ? "" : "; ") + e.value("text", "");
        if (!ex.empty()) line += " (" + std::string(r.value("lang", "en") == "pl" ? "z wyjątkiem: " : "except: ") + ex + ")";
        lines += line + "\n";
      }
      if (!lines.empty()) sel.rules = json::get_string(priv.at("rules_header"), "en") + "\n" + lines;
    }
  }
  // The thread filter: messages the analyzer put in another topic thread than the one the user is in now are left out of this request.
  {
    const Json& tf = section.value("thread_filter", Json::object());
    std::string mode = json::get_string(tf, "active", "off");
    if (has_tf) mode = tf_set.get<std::string>();
    if (!tf.value("presets", Json::object()).contains(mode)) mode = json::get_string(tf, "unknown", "off");
    if (json::get_string(tf.value("presets", Json::object()).value(mode, Json::object()), "mode", "off") == "current") {
      const double min_conf = json::get_number(tf, "min_confidence", 0.5);
      const std::string category = json::get_string(tf, "category", "thread");
      const auto keep = text::strings_of(json::find(tf, "keep_roles"));
      const annotations::Policy pol = detail::policy_of(d);
      LOOM_TRY_ASSIGN(auto msgs, db.get_msgs(conv_id));
      std::map<std::string, std::string> thread_of;
      std::string current;
      for (const auto& m : msgs) {
        if (m.status != msg_status::kActive) continue;
        auto anns = annotations::list(db, pol, m.id);
        if (!anns) continue;
        for (const auto& a : *anns)
          if (a.value("category", "") == category && a.value("confidence", 0.0) >= min_conf) {
            const std::string label = a.value("data", Json::object()).value("thread", "");
            if (label.empty()) continue;
            thread_of[m.id] = label;
            if (m.role == "user") current = label;
          }
      }
      if (!current.empty())
        for (const auto& m : msgs)
          if (m.status == msg_status::kActive && std::find(keep.begin(), keep.end(), m.role) == keep.end())
            if (auto it = thread_of.find(m.id); it != thread_of.end() && it->second != current) sel.skip.push_back(m.id);
      if (!sel.skip.empty()) sel.notes.push_back(Json{{"thread_filter", current}, {"left_out", sel.skip.size()}});
    }
  }
  if (runs != "raw" && has_rep) {
    auto mat = detail::materialize(db, d, conv_id, rep->metadata);
    if (mat) {
      sel.spec = (*mat)["spec"];
      sel.bindings = (*mat)["bindings"];
      sel.history = runs == "append" ? "append" : "replace_refinement";
    } else {
      sel.notes.push_back(Json{{"level", "L3"}, {"left_raw", mat.error().message}});
    }
  }
  return sel;
}

bool do_not_propose(Database& db, const Config& cfg, const std::string& conv_id, const std::string& title) {
  if (title.empty() || conv_id.empty()) return false;
  try {
    auto d = detail::load_data(cfg, cfg.path().parent_path());
    if (!d) return false;
    const Json& kc = d->p.at("kinds").at("proposal_rejection");
    const double at = json::get_number(kc, "suppress_at", 0);
    if (at <= 0) return false;
    auto hyps = detail::hypotheses_of(db, *d, conv_id);
    if (!hyps) return false;
    for (const auto& h : *hyps) {
      if (json::get_string(h.metadata, "signal") != "proposal_rejection" || json::get_string(h.metadata, "status") == "undone") continue;
      if (detail::claim_text_of(h.metadata) == title && json::get_number(h.metadata, "weight", 0) >= at) return true;
    }
  } catch (const std::exception&) {
    return false;
  }
  return false;
}

}  // namespace loom::guardian
