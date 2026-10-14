// Proposals, promotion, undo and the actions guardian.* (observe, status, promote, undo, compile, levels, annotate).
#include <algorithm>
#include <set>

#include "impl.h"
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
         a == "guardian.annotate";
}

namespace {

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

Result<Json> Guardian::Impl::post_pending(const Data& d, const std::string& conv_id) {
  Database& db = rt.db();
  LOOM_TRY_ASSIGN(auto hyps, hypotheses_of(db, d, conv_id));
  std::sort(hyps.begin(), hyps.end(), [](const Node& a, const Node& b) { return json::get_number(a.metadata, "weight", 0) > json::get_number(b.metadata, "weight", 0); });
  const std::size_t limit = static_cast<std::size_t>(json::get_int(d.p.at("promotion"), "max_per_reply", 1));
  const std::string mode = choose(rt.config(), d.p.at("promotion"));
  Json posted = Json::array();
  for (const auto& h : hyps) {
    Json meta = h.metadata;
    const std::string status = json::get_string(meta, "status");
    const std::string lang = json::get_string(meta, "lang", "en");
    const std::string signal = json::get_string(meta, "signal");
    const std::string claim = meta.value("claim", Json::object()).value("text", "");
    const std::string scope_en = scope_name(d, "en", json::get_string(meta, "scope")), scope_pl = scope_name(d, "pl", json::get_string(meta, "scope"));
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
    if (status != "ready" || posted.size() >= limit) continue;
    const std::string promotes = kind_promotes(d, signal);
    Json texts = Json::object();
    Json shown{{"traffic", true}, {"hypothesis", h.id}, {"signal", signal}, {"claim", claim}, {"scope", json::get_string(meta, "scope")}, {"weight", json::get_number(meta, "weight", 0)},
               {"count", json::get_int(meta, "count", 0)}, {"promotes", promotes}, {"exceptions", meta.value("exceptions", Json::array())}, {"mode", mode}};
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
      const std::string sc = scope_name(d, l, json::get_string(meta, "scope"));
      if (promotes == "transform")
        texts[l] = say(d, l, "propose_compile", Json{{"count", json::get_int(meta, "count", 0)}, {"instruction", instruction}});
      else
        texts[l] = say(d, l, mode == "ask" ? "ask_rule" : "propose_rule",
                       Json{{"count", json::get_int(meta, "count", 0)}, {"claim", claim}, {"scope", sc}, {"exceptions", exceptions_suffix(d, l, meta.value("exceptions", Json::array()))}});
    }
    (void)scope_en; (void)scope_pl;
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
  return posted;
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
  const std::string scope = json::get_string(request, "scope", json::get_string(meta, "scope"));
  if (status == "promoted") {  // answered twice: the same result
    out["already"] = true;
    out["rule"] = json::get_string(meta, "rule_id");
    out["text"] = say(d, lang, "answer_yes", Json{{"what", ""}});
    return out;
  }
  meta["status"] = "promoted";
  meta["promoted_at"] = timeutil::utc_now_iso();
  meta["promoted_by"] = json::get_string(request, "origin", "user");
  if (!json::get_string(request, "scope").empty()) meta["scope"] = scope;
  const std::string claim = json::get_string(request, "text", meta.value("claim", Json::object()).value("text", ""));
  if (!json::get_string(request, "text").empty()) meta["claim"]["text"] = claim;
  std::string what;
  if (promotes == "rule") {
    const std::string rule_id = "g:" + h.label.substr(h.label.rfind(':') + 1);
    Json evidence = Json::array();
    for (const auto& o : meta.value("occurrences", Json::array())) evidence.push_back(o.value("message_id", ""));
    Json record{{"id", rule_id}, {"hypothesis", hyp_id}, {"signal", signal}, {"rule_kind", d.p.at("kinds").at(signal).at("hypothesis").value("rule_kind", "")}, {"scope", json::get_string(meta, "scope")},
                {"scope_id", json::get_string(meta, "scope_id")}, {"text", claim}, {"exceptions", meta.value("exceptions", Json::array())}, {"evidence", evidence}, {"conv_id", conv},
                {"created", timeutil::utc_now_iso()}, {"lang", lang}};
    LOOM_TRY(actions::OverlayStore(d.root).put("guardian", {"rules", rule_id}, record));
    meta["rule_id"] = rule_id;
    out["rule"] = rule_id;
    out["overlay"] = "profiles/guardian.pack";
    what = say(d, lang, "promoted_rule", Json{{"rule", rule_id}, {"scope", scope_name(d, lang, json::get_string(meta, "scope"))}});
  } else {
    what = say(d, lang, "promoted_compile", Json{{"count", json::get_int(meta, "count", 0)}});
  }
  LOOM_TRY(save_meta(db, hyp_id, meta));
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
    Json row{{"id", h.id}, {"signal", m.value("signal", "")}, {"claim", m.value("claim", Json::object())}, {"scope", m.value("scope", "")}, {"weight", m.value("weight", 0.0)},
             {"count", m.value("count", 0)}, {"status", m.value("status", "")}, {"exceptions", m.value("exceptions", Json::array())}, {"contradicts", m.value("contradicts", Json::array())}, {"supports", m.value("supports", Json::array())},
             {"occurrences", m.value("occurrences", Json::array())}, {"rule", m.value("rule_id", "")}, {"proposal_message_id", m.value("proposal_message_id", "")}};
    if (m.contains("goal_message_id")) row["goal_message_id"] = m["goal_message_id"];
    list.push_back(row);
    text += (text.empty() ? "" : "\n") + say(d, lang, "status_line", Json{{"kind", json::get_string(d.p.at("kinds").at(m.value("signal", "course_correction")).at("title"), lang, m.value("signal", ""))},
                                                                        {"status", m.value("status", "")}, {"weight", m.value("weight", 0.0)}, {"count", m.value("count", 0)},
                                                                        {"claim", m.value("claim", Json::object()).value("text", "")}});
  }
  Json out{{"schema", "loom.guardian_status/1"}, {"conv_id", conv}, {"method", method(d).name}, {"promotion", choose(rt.config(), d.p.at("promotion"))}, {"hypotheses", list}};
  LOOM_TRY_ASSIGN(auto rep, db.find_node("rep:" + conv, json::get_string(d.p.at("nodes"), "representation", "guardian_representation")));
  if (rep) out["representation"] = Json{{"id", rep->id}, {"status", json::get_string(rep->metadata, "status")}, {"instruction", rep->content}, {"statements", rep->metadata.value("statements", Json::array())},
                                        {"selector_rule", json::get_string(rep->metadata, "selector_rule")}};
  LOOM_TRY_ASSIGN(auto selection, select(db, rt.config(), conv));
  out["selector"] = Json{{"preset", selection.preset}, {"substitutions", selection.substitutions.size()}, {"compiled", selection.spec.has_value()}, {"notes", selection.notes}};
  out["text"] = text.empty() ? say(d, lang, "status_empty") : text;
  return out;
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
  return Error(Errc::NotFound, "unknown action '" + std::string(action) + "'");
}

}  // namespace loom::guardian
