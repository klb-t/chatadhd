// Proposals, promotion, undo and the actions guardian.* (observe, status, promote, undo, compile, levels, annotate).
#include <algorithm>
#include <set>

#include "impl.h"
#include "loom/annotation_layer.h"
#include "loom/action_overlay.h"
#include "loom/chat_route.h"
#include "loom/log.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"
#include "text.h"

namespace loom::guardian {
using namespace detail;

bool is_guardian_action(std::string_view a) {
  return a == "guardian.observe" || a == "guardian.status" || a == "guardian.promote" || a == "guardian.undo" || a == "guardian.compile" || a == "guardian.levels" ||
         a == "guardian.annotate" || a == "guardian.remembered" || a == "guardian.merge" || a == "guardian.unmerge";
}

namespace {

std::string short_hash(const std::string& s) { return sha256_hex(s).substr(0, 8); }

Result<Node> get_node_or(Database& db, const std::string& id) {
  LOOM_TRY_ASSIGN(auto n, db.get_node(id));
  if (!n) return Error(Errc::NotFound, "no node '" + id + "'");
  return *n;
}

Status save_meta(Database& db, const std::string& id, const Json& meta) {
  NodePatch p;
  p.metadata = meta;
  return db.update_node(id, p);
}

std::size_t scope_rank_of(const Data& d, const std::string& scope) {
  const auto order = text::strings_of(json::find(d.p.at("scopes"), "order"));
  auto it = std::find(order.begin(), order.end(), scope);
  return it == order.end() ? 0 : static_cast<std::size_t>(it - order.begin());
}

std::string kind_promotes(const Data& d, const std::string& signal) {
  return d.p.at("kinds").value(signal, Json::object()).value("hypothesis", Json::object()).value("promotes", "");
}

std::string exceptions_suffix(const Data& d, const std::string& lang, const Json& exceptions) {
  std::string list;
  for (const auto& e : exceptions) list += (list.empty() ? "" : "; ") + e.value("text", "");
  return list.empty() ? std::string() : say(d, lang, "exceptions_suffix", Json{{"list", list}});
}

Result<std::string> post_notice(Runtime& rt, const std::string& conv_id, const std::string& text, const Json& guardian_meta) {
  NewMessage nm;
  nm.conv_id = conv_id;
  nm.role = "assistant";
  nm.text = text;
  nm.model = "loom:guardian";
  nm.metadata = Json{{"routed", "result"}, {"guardian", guardian_meta}};
  LOOM_TRY_ASSIGN(std::string id, rt.db().create_msg(nm));
  MsgPatch patch;
  patch.semantic_status = "done";
  LOOM_TRY(rt.db().update_msg(id, patch));
  return id;
}

}  // namespace

namespace {

std::string quotes_block(const Data& d, const std::string& lang, const Json& claim) {
  std::string out;
  std::size_t n = 0;
  const Json quotes = claim.value("quotes", Json::array());
  for (auto it = quotes.rbegin(); it != quotes.rend() && n < 5; ++it, ++n)
    out = say(d, lang, "quote_line", Json{{"quote", it->get<std::string>()}}) + (out.empty() ? "" : "\n") + out;
  return out;
}

std::string why_text(const Data& d, const std::string& lang, const Json& why) {
  std::string list;
  std::set<std::string> done;
  for (const auto& w : why) {
    const std::string kind = w.value("kind", "");
    if (!done.insert(kind).second) continue;
    const std::string id = kind == "conversations" ? "why_conversations" : (kind == "task_kinds" ? "why_task_kinds" : (kind == "requested" ? "why_requested" : ""));
    if (id.empty()) continue;
    list += (list.empty() ? "" : "; ") + say(d, lang, id, Json{{"n", w.value("n", 0)}});
  }
  return list.empty() ? std::string() : say(d, lang, "why_wrap", Json{{"list", list}});
}

}  // namespace

Result<Json> Guardian::Impl::post_pending(const Data& d, const std::string& conv_id) {
  Database& db = rt.db();
  LOOM_TRY_ASSIGN(auto hyps, hypotheses_of(db, d, conv_id));
  std::sort(hyps.begin(), hyps.end(), [](const Node& a, const Node& b) { return json::get_number(a.metadata, "weight", 0) > json::get_number(b.metadata, "weight", 0); });
  const std::size_t limit = static_cast<std::size_t>(json::get_int(d.p.at("promotion"), "max_per_reply", 1));
  const std::string mode = choose(rt.config(), d.p.at("promotion"));
  Json posted = Json::array();
  for (auto& h : hyps) {
    Json meta = h.metadata;
    const std::string status = json::get_string(meta, "status");
    const std::string lang = json::get_string(meta, "lang", "en");
    const std::string signal = json::get_string(meta, "signal");
    std::string claim = claim_text_of(meta);
    if (status == "promoted" && json::get_bool(meta, "notice_pending", false)) {
      const std::string rule = json::get_string(meta, "rule_id");
      Json texts = Json::object();
      for (const std::string l : {"en", "pl"}) {
        const std::string id = kind_promotes(d, signal) == "transform" ? "promoted_compile" : "applied_rule";
        texts[l] = say(d, l, id, Json{{"claim", claim}, {"count", json::get_int(meta, "count", 0)}, {"rule", rule}, {"scope", scope_name(d, l, json::get_string(meta, "scope"))}});
      }
      auto id = post_notice(rt, conv_id, texts.value(lang, texts.value("en", "")), Json{{"traffic", true}, {"notice", "applied"}, {"hypothesis", h.id}, {"texts", texts}});
      meta["notice_pending"] = false;
      LOOM_TRY(save_meta(db, h.id, meta));
      if (id) posted.push_back(Json{{"hypothesis", h.id}, {"message_id", *id}, {"notice", true}});
      continue;
    }
    // A rule that was corrected against: asking whether to switch it off is the same cycle, turned round.
    if (status == "promoted" && json::get_bool(meta, "disable_suggested", false) && posted.size() < limit && !json::get_string(meta, "rule_id").empty()) {
      Json texts = Json::object();
      for (const std::string l : {"en", "pl"}) texts[l] = say(d, l, "disable_rule", Json{{"claim", claim}});
      chat_route::OfferRequest req;
      req.conv_id = conv_id;
      req.action = "guardian.undo";
      req.lang = lang;
      req.args = Json{{"hypothesis", h.id}};
      req.texts = texts;
      req.meta = Json{{"guardian", Json{{"traffic", true}, {"hypothesis", h.id}, {"claim", claim}, {"lifecycle", "disable"}}}};
      auto offered = chat_route::offer(rt, req);
      meta["disable_suggested"] = false;
      meta["disable_proposed_at"] = timeutil::utc_now_iso();
      meta["disable_proposed_weight"] = json::get_number(meta, "disable_weight", 0);
      LOOM_TRY(save_meta(db, h.id, meta));
      if (offered) posted.push_back(Json{{"hypothesis", h.id}, {"message_id", json::get_string(*offered, "message_id")}, {"lifecycle", "disable"}});
      else log::write(log::Level::Warning, kLog, "disable proposal not posted: " + offered.error().message);
      continue;
    }
    if (status != "ready" || posted.size() >= limit) continue;
    const std::string promotes = kind_promotes(d, signal);
    // Worth asking now: an unnamed claim is named (one model call, only if the method uses a model); a scope wider than the conversation is only
    // offered for a claim that has a name of its own, since a rule about the person carries its normalized text and not the quotes.
    Json issues = Json::array();
    if (promotes == "rule" && !json::get_bool(meta.value("claim", Json::object()), "named", false) && naming_allowed(d)) {
      (void)name_with_model(d, h, issues);
      meta = h.metadata;
      claim = claim_text_of(meta);
    }
    const bool named = json::get_bool(meta.value("claim", Json::object()), "named", false);
    std::string asked = json::get_string(meta, "scope");
    const std::string cand = json::get_string(meta, "scope_candidate");
    if (promotes == "rule" && named && scope_rank_of(d, cand) > scope_rank_of(d, asked)) asked = cand;
    meta["asked_scope"] = asked;
    Json texts = Json::object();
    Json shown{{"traffic", true}, {"hypothesis", h.id}, {"signal", signal}, {"claim", claim}, {"named", named}, {"scope", asked}, {"weight", json::get_number(meta, "weight", 0)},
               {"count", json::get_int(meta, "count", 0)}, {"promotes", promotes}, {"exceptions", meta.value("exceptions", Json::array())}, {"mode", mode},
               {"quotes", meta.value("claim", Json::object()).value("quotes", Json::array())}, {"why", meta.value("scope_why", Json::array())}};
    Json evidence = Json::array();
    for (const auto& o : meta.value("occurrences", Json::array()))
      if (!o.value("undone", false)) evidence.push_back(o.value("message_id", ""));
    shown["evidence"] = evidence;
    std::string instruction;
    if (promotes == "transform") {
      auto pre = rebuild_representation(db, d, conv_id, h.id, false);
      if (!pre || pre->is_null()) continue;
      instruction = (*pre)["instruction"].get<std::string>();
      shown["instruction"] = instruction;
    }
    for (const std::string l : {"en", "pl"}) {
      const std::string sc = scope_name(d, l, asked);
      const std::string why = scope_rank_of(d, asked) > scope_rank_of(d, json::get_string(meta, "scope")) ? why_text(d, l, meta.value("scope_why", Json::array())) : std::string();
      const Json params{{"count", json::get_int(meta, "count", 0)}, {"claim", claim}, {"scope", sc}, {"why", why}, {"quotes", quotes_block(d, l, meta.value("claim", Json::object()))},
                        {"exceptions", exceptions_suffix(d, l, meta.value("exceptions", Json::array()))}};
      if (promotes == "transform") texts[l] = say(d, l, "propose_compile", Json{{"count", json::get_int(meta, "count", 0)}, {"instruction", instruction}});
      else if (mode == "ask" && named) texts[l] = say(d, l, "ask_rule", params);
      else texts[l] = say(d, l, named ? "propose_rule" : "propose_rule_unnamed", params);
    }
    chat_route::OfferRequest req;
    req.conv_id = conv_id;
    req.action = "guardian.promote";
    req.lang = lang;
    req.args = Json{{"hypothesis", h.id}};
    req.texts = texts;
    req.meta = Json{{"guardian", shown}};
    auto offered = chat_route::offer(rt, req);
    if (!offered) {
      log::write(log::Level::Debug, kLog, "proposal not posted: " + offered.error().message);
      continue;
    }
    meta["status"] = "proposed";
    meta["proposal_message_id"] = json::get_string(*offered, "message_id");
    meta["proposed_at"] = timeutil::utc_now_iso();
    LOOM_TRY(save_meta(db, h.id, meta));
    posted.push_back(Json{{"hypothesis", h.id}, {"message_id", json::get_string(*offered, "message_id")}});
  }
  // A rule nobody confirmed for a long time is asked about, once per review period (the answer "no" keeps it).
  if (posted.size() < limit) {
    LOOM_TRY_ASSIGN(auto due, review_due(d));
    for (const auto& r : due) {
      if (posted.size() >= limit) break;
      if (r.value("conv_id", "") != conv_id && r.value("scope", "conversation") == "conversation") continue;
      const std::string lang = r.value("lang", "en");
      Json texts = Json::object();
      for (const std::string l : {"en", "pl"}) texts[l] = say(d, l, "review_rule", Json{{"claim", r.value("text", "")}, {"days", r.value("days", 0)}});
      chat_route::OfferRequest req;
      req.conv_id = conv_id;
      req.action = "guardian.undo";
      req.lang = lang;
      req.args = Json{{"rule", r.value("id", "")}};
      req.texts = texts;
      req.meta = Json{{"guardian", Json{{"traffic", true}, {"rule", r.value("id", "")}, {"claim", r.value("text", "")}, {"lifecycle", "review"}}}};
      auto offered = chat_route::offer(rt, req);
      if (!offered) continue;
      // Asked now; the next question about it waits a review period.
      Json rec = r.value("record", Json::object());
      rec["review_asked_at"] = timeutil::utc_now_iso();
      (void)actions::OverlayStore(d.root).put("guardian", {"rules", r.value("id", "")}, rec);
      posted.push_back(Json{{"rule", r.value("id", "")}, {"message_id", json::get_string(*offered, "message_id")}, {"lifecycle", "review"}});
    }
  }
  return posted;
}

// Rules that were not confirmed for review_after_days and were not asked about during the last review period.
Result<std::vector<Json>> Guardian::Impl::review_due(const Data& d) {
  std::vector<Json> out;
  const Json& lc = d.p.at("lifecycle");
  const double after = json::get_number(lc, "review_after_days", 0);
  if (after <= 0) return out;
  const double every = json::get_number(lc, "review_every_days", 30);
  const auto now = timeutil::Clock::now();
  const Json rules = d.p.value("rules", Json::object());
  for (auto it = rules.begin(); it != rules.end(); ++it) {
    const Json& r = it.value();
    if (!r.is_object() || r.value("disabled", false)) continue;
    auto days_since = [&](const std::string& iso) {
      auto t = timeutil::parse_iso_utc(iso);
      return t ? std::chrono::duration<double>(now - *t).count() / 86400.0 : 0.0;
    };
    const double age = days_since(r.value("confirmed_at", r.value("created", "")));
    if (age < after) continue;
    if (!r.value("review_asked_at", "").empty() && days_since(r.value("review_asked_at", "")) < every) continue;
    Json row = r;
    out.push_back(Json{{"id", it.key()}, {"text", r.value("text", "")}, {"days", static_cast<int>(age)}, {"lang", r.value("lang", "en")}, {"scope", r.value("scope", "conversation")},
                       {"conv_id", r.value("conv_id", "")}, {"record", row}});
  }
  return out;
}

Result<Json> Guardian::Impl::promote_node(const Data& d, const std::string& hyp_id, const Json& request) {
  Database& db = rt.db();
  LOOM_TRY_ASSIGN(Node h, get_node_or(db, hyp_id));
  Json meta = h.metadata;
  const std::string status = json::get_string(meta, "status");
  const std::string signal = json::get_string(meta, "signal");
  const std::string promotes = kind_promotes(d, signal);
  const std::string conv = json::get_string(meta, "conv_id");
  if (status == "undone") return Error(Errc::Conflict, "this hypothesis was taken back; a new correction starts a new one");
  if (promotes != "rule" && promotes != "transform") return Error(Errc::InvalidArgument, "a hypothesis of kind '" + signal + "' is only recorded: there is nothing to promote");
  Json out{{"schema", "loom.guardian_promoted/1"}, {"hypothesis", hyp_id}, {"signal", signal}, {"kind", promotes}, {"conv_id", conv}};
  const std::string lang = json::get_string(meta, "lang", "en");
  // The scope asked in the proposal is the one the user's yes covers; applying on its own never widens.
  const bool applying = json::get_string(request, "origin", "user") == "apply";
  std::string scope = json::get_string(request, "scope");
  if (scope.empty()) scope = applying ? json::get_string(meta, "scope") : json::get_string(meta, "asked_scope", json::get_string(meta, "scope"));
  const bool wide = scope_rank_of(d, scope) > scope_rank_of(d, json::get_string(d.p.at("scopes"), "default", "conversation"));
  if (status == "promoted") {  // answered twice: the same result
    out["already"] = true;
    out["rule"] = json::get_string(meta, "rule_id");
    out["text"] = say(d, lang, "answer_yes", Json{{"what", ""}});
    return out;
  }
  meta["status"] = "promoted";
  meta["scope"] = scope;
  meta["scope_id"] = wide ? std::string() : json::get_string(meta, "conv_id");
  meta["promoted_at"] = timeutil::utc_now_iso();
  meta["promoted_by"] = json::get_string(request, "origin", "user");
  if (!json::get_string(request, "text").empty()) {  // he names it himself
    meta["claim"]["text"] = json::get_string(request, "text");
    meta["claim"]["source"] = "user";
    meta["claim"]["named"] = true;
  }
  const std::string claim = claim_text_of(meta);
  if (wide && promotes == "rule" && json::get_bool(d.p.at("privacy"), "require_named", true) && !json::get_bool(meta.value("claim", Json::object()), "named", false))
    return Error(Errc::InvalidArgument, "name the rule (text) before it applies beyond this conversation: only its normalized text is kept, not your quotes");
  std::string what;
  if (promotes == "rule") {
    const std::string rule_id = "g:" + h.label.substr(h.label.rfind(':') + 1);
    Json evidence = Json::array();
    for (const auto& o : meta.value("occurrences", Json::array())) evidence.push_back(o.value("message_id", ""));
    const std::int64_t evidence_total = evidence_of(d, h).count;
    Json exc = Json::array();
    for (const auto& e : meta.value("exceptions", Json::array())) exc.push_back(wide ? Json{{"text", e.value("text", "")}} : e);
    const Json& lc = d.p.at("lifecycle");
    const auto now = timeutil::Clock::now();
    auto in_days = [&](double days) { return days > 0 ? timeutil::format_iso_utc(now + std::chrono::seconds(static_cast<long long>(days * 86400))) : std::string(); };
    Json record{{"id", rule_id}, {"hypothesis", hyp_id}, {"signal", signal}, {"rule_kind", d.p.at("kinds").at(signal).at("hypothesis").value("rule_kind", "")}, {"scope", scope},
                {"scope_id", json::get_string(meta, "scope_id")}, {"text", claim}, {"named", json::get_bool(meta.value("claim", Json::object()), "named", false)}, {"exceptions", exc},
                {"conv_id", conv}, {"created", timeutil::utc_now_iso()}, {"confirmed_at", timeutil::utc_now_iso()}, {"lang", lang}, {"visible_in", wide ? "all" : "origin"},
                {"evidence_count", evidence_total}};
    // A rule about the person carries its normalized text and nothing of the conversations it was heard in: the evidence stays in the graph, as edges.
    if (!wide) record["evidence"] = evidence;
    if (const std::string r = in_days(json::get_number(lc, "review_after_days", 0)); !r.empty()) record["review_after"] = r;
    if (const std::string e = in_days(json::get_number(lc, "expire_after_days", 0)); !e.empty()) record["expires"] = e;
    LOOM_TRY(actions::OverlayStore(d.root).put("guardian", {"rules", rule_id}, record));
    meta["rule_id"] = rule_id;
    out["rule"] = rule_id;
    out["overlay"] = "profiles/guardian.pack";
    what = say(d, lang, "promoted_rule", Json{{"rule", rule_id}, {"scope", scope_name(d, lang, json::get_string(meta, "scope"))}});
  } else {
    what = say(d, lang, "promoted_compile", Json{{"count", json::get_int(meta, "count", 0)}});
  }
  LOOM_TRY(save_meta(db, hyp_id, meta));
  // The same correction heard elsewhere is covered by the rule: it does not ask again.
  for (const auto& sib : meta.value("siblings", Json::array())) {
    auto n = db.get_node(sib.value("id", ""));
    if (!n || !*n || json::get_string((*n)->metadata, "status") == "undone") continue;
    Json sm = (*n)->metadata;
    sm["status"] = "covered";
    sm["rule_id"] = meta.value("rule_id", "");
    sm["covered_by"] = hyp_id;
    LOOM_TRY(save_meta(db, (*n)->id, sm));
  }
  // The representation of the conversation, and the selector's choice for it (a rule of the conversation, switched off by undo).
  LOOM_TRY_ASSIGN(Json rep, rebuild_representation(db, d, conv));
  if (!rep.is_null()) {
    out["representation"] = rep;
    LOOM_TRY_ASSIGN(auto node, db.find_node("rep:" + conv, json::get_string(d.p.at("nodes"), "representation", "guardian_representation")));
    if (node && json::get_string(node->metadata, "selector_rule").empty()) {
      Json set{{"scope", "conversation"}, {"scope_id", conv}, {"effect", "set_value"}, {"key", d.p.at("representation").value("setting", "chat_representation")}, {"value", "guardian"},
               {"provenance", Json{{"origin", "guardian"}, {"conv_id", conv}, {"hypothesis", hyp_id}}}};
      auto written = actions::rules_set(d.root, set);
      Json nm = node->metadata;
      if (written) {
        nm["selector_rule"] = json::get_string(*written, "id");
        out["selector_rule"] = json::get_string(*written, "id");
      } else {
        out["selector_rule_error"] = written.error().message;
        log::write(log::Level::Warning, kLog, "the selector rule could not be written: " + written.error().message);
      }
      LOOM_TRY(save_meta(db, node->id, nm));
    } else if (node) {
      out["selector_rule"] = json::get_string(node->metadata, "selector_rule");
    }
  }
  out["text"] = say(d, lang, "answer_yes", Json{{"what", what}});
  return out;
}

Result<Json> Guardian::Impl::promote(const Data& d, const Json& request) {
  const std::string hyp = json::get_string(request, "hypothesis");
  if (hyp.empty()) return Error(Errc::InvalidArgument, "hypothesis is required (see guardian.status)");
  return promote_node(d, hyp, request);
}

Result<Json> Guardian::Impl::undo(const Data& d, const Json& request) {
  Database& db = rt.db();
  if (const std::string ann = json::get_string(request, "annotation"); !ann.empty()) {
    LOOM_TRY_ASSIGN(auto cur, db.get_message_annotation(ann));
    if (!cur) return Error(Errc::NotFound, "no annotation '" + ann + "'");
    MessageAnnotationPatch patch;
    patch.state = "retracted";
    LOOM_TRY_ASSIGN(auto revised, db.revise_message_annotation(ann, cur->revision, patch));
    LOOM_TRY_ASSIGN(auto msg, db.get_msg(revised.message_id));
    Json out{{"schema", "loom.guardian_undone/1"}, {"annotation", ann}, {"message_id", revised.message_id}};
    if (msg) {
      LOOM_TRY_ASSIGN(Json lv, build_levels(db, d, *msg));
      out["levels"] = lv["levels"];
    }
    out["text"] = say(d, "en", "undone", Json{{"what", "annotation " + ann}});
    return out;
  }
  std::string hyp_id = json::get_string(request, "hypothesis");
  const std::string rule = json::get_string(request, "rule");
  if (hyp_id.empty() && !rule.empty()) {
    LOOM_TRY_ASSIGN(auto all, db.list_nodes(json::get_string(d.p.at("nodes"), "hypothesis", "guardian_hypothesis"), 100000));
    for (const auto& n : all)
      if (json::get_string(n.metadata, "rule_id") == rule) hyp_id = n.id;
  }
  if (hyp_id.empty()) return Error(Errc::InvalidArgument, "name what to take back: hypothesis, rule or annotation");
  LOOM_TRY_ASSIGN(Node h, get_node_or(db, hyp_id));
  Json meta = h.metadata;
  const std::string conv = json::get_string(meta, "conv_id");
  const std::string lang = json::get_string(meta, "lang", "en");
  Json out{{"schema", "loom.guardian_undone/1"}, {"hypothesis", hyp_id}, {"conv_id", conv}, {"previous_status", json::get_string(meta, "status")}};
  if (const std::string rid = json::get_string(meta, "rule_id"); !rid.empty()) {
    LOOM_TRY_ASSIGN(bool removed, actions::OverlayStore(d.root).remove("guardian", {"rules", rid}));
    out["rule"] = rid;
    out["rule_removed"] = removed;
  }
  meta["status"] = "undone";
  meta["undone_at"] = timeutil::utc_now_iso();
  LOOM_TRY(save_meta(db, hyp_id, meta));
  // The representation without it; when nothing is left, the conversation's choice goes back to what it was.
  LOOM_TRY_ASSIGN(Json rep, rebuild_representation(db, d, conv));
  LOOM_TRY_ASSIGN(auto node, db.find_node("rep:" + conv, json::get_string(d.p.at("nodes"), "representation", "guardian_representation")));
  if (rep.is_null() && node && !json::get_string(node->metadata, "selector_rule").empty()) {
    auto off = actions::rules_disable(d.root, Json{{"id", json::get_string(node->metadata, "selector_rule")}});
    Json nm = node->metadata;
    if (off) out["selector_rule_disabled"] = json::get_string(nm, "selector_rule");
    else out["selector_rule_error"] = off.error().message;
    nm["selector_rule"] = "";
    LOOM_TRY(save_meta(db, node->id, nm));
  }
  out["text"] = say(d, lang, "undone", Json{{"what", meta.value("claim", Json::object()).value("text", hyp_id)}});
  return out;
}

Result<Json> Guardian::Impl::status(const Data& d, const Json& request) {
  Database& db = rt.db();
  if (json::get_string(request, "scope") == "person") return remembered(d, request);
  std::string conv = json::get_string(request, "conv_id");
  const std::string only = json::get_string(request, "hypothesis");
  if (conv.empty() && !only.empty()) {
    LOOM_TRY_ASSIGN(Node h, get_node_or(db, only));
    conv = json::get_string(h.metadata, "conv_id");
  }
  if (conv.empty()) {
    if (auto cur = rt.chat().current_conv()) conv = cur->id;
  }
  if (conv.empty()) return Error(Errc::InvalidArgument, "conv_id is required");
  LOOM_TRY_ASSIGN(auto hyps, hypotheses_of(db, d, conv));
  std::sort(hyps.begin(), hyps.end(), [](const Node& a, const Node& b) { return json::get_number(a.metadata, "weight", 0) > json::get_number(b.metadata, "weight", 0); });
  std::string lang = json::get_string(request, "lang");
  if (lang.empty() && !hyps.empty()) lang = json::get_string(hyps.front().metadata, "lang");
  if (lang.empty()) lang = "en";
  Json list = Json::array();
  std::string text;
  for (const auto& h : hyps) {
    if (!only.empty() && h.id != only) continue;
    const Json& m = h.metadata;
    Json claim_row = m.value("claim", Json::object());
    claim_row["display"] = claim_text_of(m);
    Json merged = Json::array();
    for (const auto& o : m.value("occurrences", Json::array()))
      if (o.contains("merge") && !o.value("undone", false)) merged.push_back(Json{{"occurrence", o.value("occurrence", "")}, {"quote", o.value("quote", "")}, {"merge", o["merge"]}});
    Json row{{"id", h.id}, {"signal", m.value("signal", "")}, {"claim", claim_row}, {"scope", m.value("scope", "")}, {"scope_request", m.value("requested_scope", "")},
             {"scope_candidate", m.value("scope_candidate", "")}, {"scope_why", m.value("scope_why", Json::array())}, {"siblings", m.value("siblings", Json::array())}, {"merged", merged},
             {"weight", m.value("weight", 0.0)},
             {"count", m.value("count", 0)}, {"status", m.value("status", "")}, {"exceptions", m.value("exceptions", Json::array())}, {"contradicts", m.value("contradicts", Json::array())}, {"supports", m.value("supports", Json::array())},
             {"occurrences", m.value("occurrences", Json::array())}, {"rule", m.value("rule_id", "")}, {"proposal_message_id", m.value("proposal_message_id", "")}};
    if (m.contains("goal_message_id")) row["goal_message_id"] = m["goal_message_id"];
    list.push_back(row);
    text += (text.empty() ? "" : "\n") + say(d, lang, "status_line", Json{{"kind", json::get_string(d.p.at("kinds").at(m.value("signal", "course_correction")).at("title"), lang, m.value("signal", ""))},
                                                                        {"status", m.value("status", "")}, {"weight", m.value("weight", 0.0)}, {"count", m.value("count", 0)},
                                                                        {"claim", claim_text_of(m)}});
  }
  Json out{{"schema", "loom.guardian_status/1"}, {"conv_id", conv}, {"method", method(d).name}, {"promotion", choose(rt.config(), d.p.at("promotion"))}, {"hypotheses", list}};
  LOOM_TRY_ASSIGN(auto rep, db.find_node("rep:" + conv, json::get_string(d.p.at("nodes"), "representation", "guardian_representation")));
  if (rep) out["representation"] = Json{{"id", rep->id}, {"status", json::get_string(rep->metadata, "status")}, {"instruction", rep->content}, {"statements", rep->metadata.value("statements", Json::array())},
                                        {"selector_rule", json::get_string(rep->metadata, "selector_rule")}};
  LOOM_TRY_ASSIGN(auto selection, select(db, rt.config(), conv));
  out["selector"] = Json{{"preset", selection.preset}, {"substitutions", selection.substitutions.size()}, {"compiled", selection.spec.has_value()}, {"notes", selection.notes}};
  // The segments the layer holds for the conversation (signals only; the noise is in guardian.levels): what was noticed, where, how sure, by whom.
  LOOM_TRY_ASSIGN(auto msgs, db.get_msgs(conv));
  Json segments = Json::array();
  const annotations::Policy pol = policy_of(d);
  for (const auto& msg : msgs) {
    if (msg.status != msg_status::kActive || msg.role != "user" || is_traffic(d, msg)) continue;
    LOOM_TRY_ASSIGN(Json anns, annotations::list(db, pol, msg.id));
    for (const auto& a : anns) {
      if (a.value("class", "") != "signal" && a.value("class", "") != "structure") continue;
      segments.push_back(Json{{"message_id", msg.id}, {"id", a["id"]}, {"category", a["category"]}, {"polarity", a["polarity"]}, {"excerpt", a["excerpt"]}, {"confidence", a["confidence"]},
                              {"producer", a["producer"]}, {"qualifier", a["qualifier"]}, {"start_char", a["start_char"]}, {"end_char", a["end_char"]}, {"cost", a["cost"]}});
    }
  }
  out["segments"] = segments;
  out["text"] = text.empty() ? say(d, lang, "status_empty") : text;
  return out;
}

// What the guardian keeps about the person, across conversations: the rules of the overlay that apply beyond one conversation, and what is on the way.
Result<Json> Guardian::Impl::remembered(const Data& d, const Json& request) {
  const std::string lang = json::get_string(request, "lang", "en");
  Json rules = Json::array();
  std::string text;
  const Json all = d.p.value("rules", Json::object());
  for (auto it = all.begin(); it != all.end(); ++it) {
    const Json& r = it.value();
    if (!r.is_object() || r.value("disabled", false)) continue;
    const std::string scope = r.value("scope", "conversation");
    if (scope == "conversation" || scope == "message") continue;
    rules.push_back(Json{{"id", it.key()}, {"text", r.value("text", "")}, {"scope", scope}, {"since", r.value("created", "")}, {"review_after", r.value("review_after", "")},
                         {"expires", r.value("expires", "")}, {"visible_in", r.value("visible_in", "all")}, {"exceptions", r.value("exceptions", Json::array())}});
    text += (text.empty() ? "" : "\n") + say(d, lang, "remembered_line", Json{{"claim", r.value("text", "")}, {"scope", scope_name(d, lang, scope)}, {"since", r.value("created", "").substr(0, 10)}});
  }
  Json pending = Json::array();
  LOOM_TRY_ASSIGN(auto hyps, all_hypotheses(d));
  for (const auto& h : hyps) {
    const std::string st = json::get_string(h.metadata, "status");
    if (st == "ready" || st == "proposed") pending.push_back(Json{{"id", h.id}, {"claim", claim_text_of(h.metadata)}, {"conv_id", h.metadata.value("conv_id", "")}, {"status", st}, {"scope_candidate", h.metadata.value("scope_candidate", "")}});
  }
  return Json{{"schema", "loom.guardian_remembered/1"}, {"rules", rules}, {"pending", pending},
              {"text", rules.empty() ? say(d, lang, "remembered_empty") : say(d, lang, "remembered_header") + "\n" + text}};
}

// The user says two things are (not) the same: the method `user`. Merging moves the occurrences of `with` into `hypothesis`, each marked with the
// edge; unmerging splits the merged ones out again. Nothing is deleted.
Result<Json> Guardian::Impl::merge(const Data& d, const Json& request) {
  Database& db = rt.db();
  const std::string into = json::get_string(request, "hypothesis"), with = json::get_string(request, "with");
  if (into.empty() || with.empty() || into == with) return Error(Errc::InvalidArgument, "hypothesis and with (two different hypotheses) are required");
  LOOM_TRY_ASSIGN(Node a, get_node_or(db, into));
  LOOM_TRY_ASSIGN(Node b, get_node_or(db, with));
  if (json::get_string(a.metadata, "conv_id") != json::get_string(b.metadata, "conv_id")) return Error(Errc::InvalidArgument, "both hypotheses must belong to one conversation");
  Json am = a.metadata, bm = b.metadata;
  std::string last;
  for (const auto& o : am.value("occurrences", Json::array())) last = o.value("occurrence", last);
  int moved = 0;
  for (auto& o : bm["occurrences"]) {
    if (o.value("undone", false)) continue;
    Json entry = o;
    LOOM_TRY_ASSIGN(auto edge, db.create_link(o.value("occurrence", ""), last, json::get_string(d.p.at("matching"), "edge", "same_as"), 1.0, Json{{"method", "user"}, {"hypothesis", into}}));
    entry["merge"] = Json{{"method", "user"}, {"confidence", 1.0}, {"with", last}, {"edge", edge}, {"from", with}};
    am["occurrences"].push_back(entry);
    o["undone"] = true;
    o["moved_to"] = into;
    ++moved;
  }
  bm["status"] = "merged";
  bm["merged_into"] = into;
  refresh_claim(d, am);
  weigh(d, am, {});
  weigh(d, bm, {});
  LOOM_TRY(save_meta(db, into, am));
  LOOM_TRY(save_meta(db, with, bm));
  LOOM_TRY(reevaluate(d, into));
  return Json{{"schema", "loom.guardian_merge/1"}, {"hypothesis", into}, {"with", with}, {"moved", moved}, {"text", say(d, json::get_string(am, "lang", "en"), "merged", Json{{"what", claim_text_of(am)}})}};
}

// Splits the merged occurrences of a hypothesis (those whose merge confidence is below `below`, default all) into hypotheses of their own.
Result<int> Guardian::Impl::split_merged(const Data& d, const std::string& hyp_id, double below, const std::string& only_occurrence) {
  Database& db = rt.db();
  LOOM_TRY_ASSIGN(Node h, get_node_or(db, hyp_id));
  Json meta = h.metadata;
  const std::string conv = json::get_string(meta, "conv_id");
  int n = 0;
  for (auto& o : meta["occurrences"]) {
    if (o.value("undone", false) || !o.contains("merge")) continue;
    if (!only_occurrence.empty() && o.value("occurrence", "") != only_occurrence) continue;
    if (o["merge"].value("confidence", 1.0) >= below) continue;
    LOOM_TRY_ASSIGN(auto occ, get_node_or(db, o.value("occurrence", "")));
    const std::string signal = json::get_string(meta, "signal");
    const std::string label = "hyp:" + conv + ":" + signal + ":" + short_hash("split|" + occ.id);
    Json nm = meta;
    nm["occurrences"] = Json::array();
    nm["exceptions"] = Json::array();
    nm["contradicts"] = Json::array();
    nm["siblings"] = Json::array();
    nm["status"] = "observing";
    nm["created"] = timeutil::utc_now_iso();
    nm["claim"] = Json{{"text", ""}, {"named", false}, {"source", ""}, {"quotes", Json::array()}, {"lang", meta.value("lang", "en")}};
    nm.erase("rule_id"); nm.erase("proposal_message_id"); nm.erase("rejected_weight"); nm.erase("promoted_at");
    Json entry = o;
    entry["merge_undone"] = o["merge"];
    entry.erase("merge");
    nm["occurrences"].push_back(entry);
    nm["tokens"] = claim_tokens(d, o.value("quote", ""));
    NodeOptions no;
    no.metadata = nm;
    no.tags = Json::array({"guardian_hypothesis", signal});
    LOOM_TRY_ASSIGN(std::string new_id, db.create_node(label, json::get_string(d.p.at("nodes"), "hypothesis", "guardian_hypothesis"), no));
    LOOM_TRY_ASSIGN(auto l, db.create_link(occ.id, new_id, json::get_string(d.p.at("nodes").at("links"), "supports", "supports"), o.value("factor", 1.0), Json{{"split_from", hyp_id}}));
    (void)l;
    refresh_claim(d, nm);
    weigh(d, nm, {});
    LOOM_TRY(save_meta(db, new_id, nm));
    o["undone"] = true;
    o["split_to"] = new_id;
    ++n;
  }
  if (n == 0) return 0;
  refresh_claim(d, meta);
  weigh(d, meta, {});
  LOOM_TRY(save_meta(db, hyp_id, meta));
  return n;
}

Result<Json> Guardian::Impl::unmerge(const Data& d, const Json& request) {
  const std::string hyp = json::get_string(request, "hypothesis");
  if (hyp.empty()) return Error(Errc::InvalidArgument, "hypothesis is required");
  LOOM_TRY_ASSIGN(int n, split_merged(d, hyp, 2.0, json::get_string(request, "occurrence")));
  return Json{{"schema", "loom.guardian_unmerge/1"}, {"hypothesis", hyp}, {"split", n}, {"text", say(d, "en", "unmerged", Json{{"what", std::to_string(n) + " occurrence(s)"}})}};
}

Result<Json> Guardian::Impl::compile(const Data& d, const Json& request) {
  const std::string conv = json::get_string(request, "conv_id");
  if (conv.empty()) return Error(Errc::InvalidArgument, "conv_id is required");
  LOOM_TRY_ASSIGN(Json pre, rebuild_representation(rt.db(), d, conv, json::get_string(request, "hypothesis"), false));
  if (pre.is_null()) return Json{{"schema", "loom.guardian_compile/1"}, {"conv_id", conv}, {"empty", true}};
  pre["schema"] = "loom.guardian_compile/1";
  pre["conv_id"] = conv;
  return pre;
}

Result<Json> Guardian::Impl::levels(const Data& d, const Json& request) {
  Database& db = rt.db();
  std::vector<Message> msgs;
  if (const std::string mid = json::get_string(request, "message_id"); !mid.empty()) {
    LOOM_TRY_ASSIGN(auto m, db.get_msg(mid));
    if (!m) return Error(Errc::NotFound, "no message '" + mid + "'");
    msgs.push_back(*m);
  } else if (const std::string conv = json::get_string(request, "conv_id"); !conv.empty()) {
    LOOM_TRY_ASSIGN(auto all, db.get_msgs(conv));
    msgs = all;
  } else {
    return Error(Errc::InvalidArgument, "message_id or conv_id is required");
  }
  const bool refresh = json::get_bool(request, "refresh", false);
  Json rows = Json::array();
  for (const auto& m : msgs) {
    if (m.status != msg_status::kActive || m.role == "assistant") continue;
    Json lv = Json::object();
    if (refresh) {
      auto a = annotate_message(db, d, m);
      (void)a;
      LOOM_TRY_ASSIGN(Json built, build_levels(db, d, m));
      lv = built["levels"];
    } else {
      for (const auto& name : text::strings_of(json::find(d.p.at("levels"), "order"))) {
        auto node = db.find_node(level_label(m.id, name), json::get_string(d.p.at("nodes"), "level", "guardian_level"));
        if (node && *node) lv[name] = Json{{"node", (*node)->id}, {"text", (*node)->content}, {"usable", (*node)->metadata.value("usable", false)}, {"dropped", (*node)->metadata.value("dropped", Json::array())},
                                            {"fresh", (*node)->metadata.value("source_text_sha256", "") == sha256_hex(m.text)}};
      }
    }
    LOOM_TRY_ASSIGN(Json anns, annotations_of(db, d, m.id));
    if (lv.empty() && anns.empty() && msgs.size() > 1) continue;
    rows.push_back(Json{{"message_id", m.id}, {"raw", m.text}, {"levels", lv}, {"annotations", anns}});
  }
  return Json{{"schema", "loom.guardian_levels/1"}, {"messages", rows}};
}

Result<Json> Guardian::Impl::annotate(const Data& d, const Json& request) {
  (void)d;
  const std::string mid = json::get_string(request, "message_id");
  if (mid.empty()) return Error(Errc::InvalidArgument, "message_id is required");
  Json opts{{"method", json::get_string(request, "method", "model")}, {"sync_model", true}, {"post", json::get_bool(request, "post", true)}};
  return observe(mid, opts);
}

Result<Json> Guardian::invoke(std::string_view action, const Json& request) {
  LOOM_TRY_ASSIGN(auto d, impl_->data());
  if (action == "guardian.observe") {
    const std::string mid = json::get_string(request, "message_id");
    if (mid.empty()) return Error(Errc::InvalidArgument, "message_id is required");
    Json opts = request;
    opts["post"] = json::get_bool(request, "post", true);
    if (!opts.contains("sync_model")) opts["sync_model"] = true;
    return impl_->observe(mid, opts);
  }
  if (action == "guardian.status") return impl_->status(d, request);
  if (action == "guardian.promote") return impl_->promote(d, request);
  if (action == "guardian.undo") return impl_->undo(d, request);
  if (action == "guardian.compile") return impl_->compile(d, request);
  if (action == "guardian.levels") return impl_->levels(d, request);
  if (action == "guardian.annotate") return impl_->annotate(d, request);
  if (action == "guardian.remembered") return impl_->remembered(d, request);
  if (action == "guardian.merge") return impl_->merge(d, request);
  if (action == "guardian.unmerge") return impl_->unmerge(d, request);
  return Error(Errc::NotFound, "unknown action '" + std::string(action) + "'");
}

}  // namespace loom::guardian
