// The cycle: message -> signal -> occurrence -> hypothesis (weight, scope, exceptions) -> proposal. See loom/guardian.h.
#include <algorithm>
#include <cmath>
#include <set>

#include "impl.h"
#include "loom/chat_route.h"
#include "loom/log.h"
#include "loom/use_model.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"
#include "text.h"

namespace loom::guardian {
using namespace detail;
namespace {

std::string short_hash(const std::string& s) { return sha256_hex(s).substr(0, 8); }

std::vector<std::string> all_phrases(const Json& by_lang) {
  std::vector<std::string> out;
  if (by_lang.is_object())
    for (auto it = by_lang.begin(); it != by_lang.end(); ++it)
      for (const auto& p : text::strings_of(&it.value())) out.push_back(p);
  return out;
}

std::string clip_text(const std::string& s, std::size_t n) {
  std::string t(utf8::strip(s));
  return std::string(utf8::prefix(t, n));
}

std::size_t scope_rank(const Data& d, const std::string& scope) {
  const auto order = text::strings_of(json::find(d.p.at("scopes"), "order"));
  auto it = std::find(order.begin(), order.end(), scope);
  return it == order.end() ? 0 : static_cast<std::size_t>(it - order.begin());
}

double decay(const Data& d, const std::string& created) {
  const double half = json::get_number(d.p.at("weighting"), "recency_half_life_hours", 0);
  if (half <= 0) return 1.0;
  auto t = timeutil::parse_iso_utc(created);
  if (!t) return 1.0;
  const double age_h = std::max(0.0, std::chrono::duration<double>(timeutil::Clock::now() - *t).count() / 3600.0);
  return std::pow(0.5, age_h / half);
}

Result<Node> get_node_or(Database& db, const std::string& id) {
  LOOM_TRY_ASSIGN(auto n, db.get_node(id));
  if (!n) return Error(Errc::NotFound, "no node '" + id + "'");
  return *n;
}

Status save_meta(Database& db, const std::string& id, const Json& meta, const std::optional<std::string>& content = std::nullopt) {
  NodePatch p;
  p.metadata = meta;
  if (content) p.content = *content;
  return db.update_node(id, p);
}

}  // namespace

// ── data and method ─────────────────────────────────────────────────────────────────────────

Result<Data> Guardian::Impl::data() { return load_data(rt.config(), rt.paths().root); }

Method Guardian::Impl::method(const Data& d, const std::string& override_name) {
  const Json& section = d.p.at("method");
  std::string name = override_name;
  if (name.empty() || !section.at("presets").contains(name)) name = choose(rt.config(), section);
  const Json& preset = section.at("presets").at(name);
  Method m;
  m.name = name;
  m.heuristic = json::get_bool(preset, "heuristic", false);
  m.model = json::get_bool(preset, "model", false);
  m.ask_model_when = json::get_string(preset, "ask_model_when");
  return m;
}

// ── detection ───────────────────────────────────────────────────────────────────────────────

std::vector<Signal> Guardian::Impl::detect_heuristic(const Data& d, const Message& m, bool has_goal) {
  const auto toks = text::tokenize(utf8::decode(m.text), d.fold);
  Signal best;
  double best_key = 0;
  const Json& kinds = d.p.at("kinds");
  for (auto it = kinds.begin(); it != kinds.end(); ++it) {
    const Json& detect = it.value().value("detect", Json::object());
    const Json* h = json::find(detect, "heuristic");
    if (!h) continue;
    if (json::get_bool(detect, "needs_goal", false) && !has_goal) continue;
    const auto hits = text::find(toks, text::compile(all_phrases(h->value("markers", Json::object())), d.fold));
    double score = 0;
    const std::size_t window = static_cast<std::size_t>(json::get_int(*h, "start_window", 5));
    for (const auto& hit : hits) score += hit.first < window ? json::get_number(*h, "start_weight", 1.0) : json::get_number(*h, "anywhere_weight", 0.6);
    if (score < json::get_number(*h, "min_score", 0.6)) continue;
    const double key = score * 10 + json::get_number(it.value(), "priority", 0);
    if (key > best_key) {
      best_key = key;
      best = Signal{};
      best.kind = it.key();
      best.score = score;
      best.by = "heuristic";
    }
  }
  if (best.kind.empty()) return {};
  // What the message adds: tone, a repeated demand, a scope, exceptions.
  const std::string lowered = utf8::to_lower(m.text);
  const Json& esc = d.p.at("escalation");
  const auto tone = text::compile(all_phrases(esc.at("tone").at("markers")), d.fold);
  const auto repeat = text::compile(all_phrases(esc.at("repeat").at("markers")), d.fold);
  best.tone = !text::find(toks, tone).empty() || text::contains_raw(lowered, tone);
  best.repeat = !text::find(toks, repeat).empty() || text::contains_raw(lowered, repeat);
  for (const std::string scope : {"person", "conversation", "message"}) {
    const Json* markers = json::find(d.p.at("scopes").at("markers"), scope);
    if (markers && !text::find(toks, text::compile(all_phrases(*markers), d.fold)).empty()) {
      best.scope = scope;
      break;
    }
  }
  const Json& ex = d.p.at("exceptions");
  const auto cps = utf8::decode(m.text);
  for (const auto& hit : text::find(toks, text::compile(all_phrases(ex.at("markers")), d.fold))) {
    std::size_t from = toks[hit.last].end, to = from;
    std::size_t words = 0;
    const std::size_t max_words = static_cast<std::size_t>(json::get_int(ex, "max_words", 12));
    for (std::size_t i = hit.last + 1; i < toks.size() && words < max_words; ++i) {
      bool stop = false;
      for (std::size_t c = toks[i - 1].end; c < toks[i].start; ++c) stop = stop || cps[c] == U'.' || cps[c] == U';' || cps[c] == U'!' || cps[c] == U'?' || cps[c] == U'\n';
      if (stop) break;
      to = toks[i].end;
      ++words;
    }
    if (to > from) {
      const std::string phrase = clip_text(utf8::encode(std::u32string_view(cps).substr(from, to - from)), 200);
      if (!phrase.empty()) best.exceptions.push_back(phrase);
    }
  }
  best.lang = guess_lang(d, m.text);
  return {best};
}

Result<std::optional<Signal>> Guardian::Impl::detect_model(const Data& d, const Message& m, Json& issues) {
  const Json& call = d.p.at("method").at("model_call");
  std::string model;
  for (const char* key : {"model_setting", "model_fallback_setting"}) {
    const Json v = rt.config().get(json::get_string(d.p.at("method"), key));
    if (v.is_string() && !v.get<std::string>().empty()) {
      model = v.get<std::string>();
      break;
    }
  }
  if (model.empty()) {
    issues.push_back("no model is configured for the guardian");
    return std::optional<Signal>();
  }
  std::string names, help;
  const Json& kinds = d.p.at("kinds");
  for (auto it = kinds.begin(); it != kinds.end(); ++it) {
    const auto methods = text::strings_of(json::find(it.value().value("detect", Json::object()), "methods"));
    if (std::find(methods.begin(), methods.end(), "model") == methods.end()) continue;
    names += (names.empty() ? "" : ", ") + it.key();
    help += (help.empty() ? "" : "; ") + it.key() + " = " + json::get_string(it.value().value("title", Json::object()), "en");
  }
  LOOM_TRY_ASSIGN(auto msgs, rt.db().get_msgs(m.conv_id));
  std::string previous;
  for (const auto& x : msgs) {
    if (x.id == m.id) break;
    if (x.role == "assistant" && !is_traffic(d, x)) previous = x.text;
  }
  LOOM_TRY_ASSIGN(std::string system, render_profile_template(json::get_string(call, "system"), Json{{"kinds", names}, {"kind_help", help}}));
  LOOM_TRY_ASSIGN(std::string user, render_profile_template(json::get_string(call, "user"), Json{{"previous", clip_text(previous, 1200)}, {"text", clip_text(m.text, 4000)}}));
  llm::ModelRequest req;
  req.kind = json::get_string(call, "kind", "guardian.annotate");
  req.action = "guardian.model_read";
  req.models = {llm::ModelSpec{model, "", Json::object()}};
  req.messages = Json::array({Json{{"role", "system"}, {"content", system}}});
  req.prompt = user;
  if (const Json* p = json::find(call, "parameters"); p && p->is_object()) req.parameters = *p;
  req.wants = {"json"};
  req.title = json::get_string(call, "title");
  req.timeout_ms = static_cast<int>(json::get_int(call, "timeout_ms", 30000));
  llm::Services services(rt.config(), rt.secrets(), rt.http());
  services.data_root = rt.paths().root;
  auto outcome = llm::use_model(services, std::move(req));
  if (!outcome) {
    issues.push_back(outcome.error().message);
    return std::optional<Signal>();
  }
  const llm::VariantResult& r = outcome->last();
  if (r.state == llm::State::Held) {
    issues.push_back("the usage guard holds the guardian's model call (" + json::get_string(r.hold, "status") + "); nothing was sent");
    return std::optional<Signal>();
  }
  if (!r.ok()) {
    issues.push_back(r.error ? r.error->message : std::string("the model call failed"));
    return std::optional<Signal>();
  }
  Json answer = nullptr;
  for (const auto& part : r.outputs)
    if (part.kind == "json") answer = part.value;
  if (!answer.is_object()) {
    issues.push_back("the model did not return a JSON object");
    return std::optional<Signal>();
  }
  const std::string kind = json::get_string(answer, "kind");
  const double confidence = json::get_number(answer, "confidence", 0.0);
  if (kind == "none" || !kinds.contains(kind) || confidence < json::get_number(call, "min_confidence", 0.5)) return std::optional<Signal>();
  Signal s;
  s.kind = kind;
  s.score = std::min(1.0, confidence);
  s.by = "model";
  s.subject = clip_text(json::get_string(answer, "subject"), 400);
  s.tone = json::get_string(answer, "tone") == "sharp";
  s.scope = json::get_string(answer, "scope");
  for (const auto& e : answer.value("exceptions", Json::array()))
    if (e.is_string() && !e.get<std::string>().empty()) s.exceptions.push_back(clip_text(e.get<std::string>(), 200));
  s.lang = guess_lang(d, m.text);
  return std::optional<Signal>(s);
}

// ── occurrences and hypotheses ──────────────────────────────────────────────────────────────

Json Guardian::Impl::weigh(const Data& d, Json& meta, const std::vector<Node>& others) {
  double w = 0;
  int count = 0;
  for (const auto& o : meta.value("occurrences", Json::array())) {
    if (o.value("undone", false)) continue;
    ++count;
    w += o.value("strength", 1.0) * o.value("factor", 1.0) * decay(d, o.value("created", ""));
  }
  double against = 0;
  for (const auto& c : meta.value("contradicts", Json::array()))
    for (const auto& n : others)
      if (n.id == c.get<std::string>()) against += json::get_number(n.metadata, "weight", 0);
  w = std::max(0.0, w - json::get_number(d.p.at("weighting"), "contradiction_factor", 0.8) * against);
  w = std::round(w * 1000) / 1000;
  meta["weight"] = w;
  meta["count"] = count;
  const Json& by_weight = d.p.at("scopes").at("by_weight");
  std::string scope = json::get_string(d.p.at("scopes"), "default", "conversation");
  for (const auto& step : by_weight)
    if (w >= json::get_number(step, "min", 0)) scope = json::get_string(step, "scope", scope);
  const std::string explicit_scope = json::get_string(meta, "explicit_scope");
  std::string current = json::get_string(meta, "scope");
  for (const auto& cand : {scope, explicit_scope})
    if (!cand.empty() && (current.empty() || scope_rank(d, cand) > scope_rank(d, current))) current = cand;
  meta["scope"] = current;
  meta["scope_id"] = (current == "conversation" || current == "message") ? meta.value("conv_id", "") : std::string();
  return meta;
}

Status Guardian::Impl::reevaluate(const Data& d, const std::string& hyp_id) {
  Database& db = rt.db();
  LOOM_TRY_ASSIGN(Node h, get_node_or(db, hyp_id));
  Json meta = h.metadata;
  const std::string status = json::get_string(meta, "status");
  const Json& kcfg = d.p.at("kinds").value(json::get_string(meta, "signal"), Json::object());
  const std::string promotes = kcfg.value("hypothesis", Json::object()).value("promotes", "");
  if (promotes != "rule" && promotes != "transform") return ok_status();
  const std::string mode = choose(rt.config(), d.p.at("promotion"));
  const double threshold = json::get_number(d.p.at("thresholds"), json::get_string(d.p.at("promotion").at("presets").at(mode), "at", "propose"), 2.0);
  const double weight = json::get_number(meta, "weight", 0);
  const int need = promotes == "transform" ? static_cast<int>(json::get_int(d.p.at("transforms").at("compile_refinements"), "min_occurrences", 3)) : 1;
  bool ready = false;
  if (status == "observing") ready = weight >= threshold && json::get_int(meta, "count", 0) >= need;
  else if (status == "rejected") ready = weight >= json::get_number(d.p.at("weighting"), "reask_factor", 2.0) * json::get_number(meta, "rejected_weight", 0) && json::get_int(meta, "count", 0) >= need;
  if (!ready) return ok_status();
  meta["status"] = "ready";
  meta["ready_at"] = timeutil::utc_now_iso();
  LOOM_TRY(save_meta(db, hyp_id, meta));
  if (mode == "apply") {
    auto done = promote_node(d, hyp_id, Json{{"origin", "apply"}});
    if (!done) {
      log::write(log::Level::Warning, kLog, "could not apply the rule: " + done.error().message);
      return ok_status();
    }
    LOOM_TRY_ASSIGN(Node again, get_node_or(db, hyp_id));
    Json m2 = again.metadata;
    m2["notice_pending"] = true;
    LOOM_TRY(save_meta(db, hyp_id, m2));
  }
  return ok_status();
}

Result<Json> Guardian::Impl::record(const Data& d, const Message& m, const Signal& s) {
  Database& db = rt.db();
  const Json& links = d.p.at("nodes").at("links");
  const std::string occ_kind = json::get_string(d.p.at("nodes"), "occurrence", "guardian_occurrence");
  const std::string hyp_kind = json::get_string(d.p.at("nodes"), "hypothesis", "guardian_hypothesis");
  const Json& kcfg = d.p.at("kinds").at(s.kind);
  const Json hcfg = kcfg.value("hypothesis", Json::object());
  const std::string occ_label = "occ:" + m.id + ":" + s.kind;
  LOOM_TRY_ASSIGN(auto prior, db.find_node(occ_label, occ_kind));
  if (prior) return Json{{"occurrence", prior->id}, {"hypothesis", prior->metadata.value("hypothesis", "")}, {"kind", s.kind}, {"duplicate", true}};

  const std::string claim_text = s.subject.empty() ? clip_text(m.text, 400) : s.subject;
  const auto tokens = subject_tokens(d, claim_text);
  const bool neg = negative(d, claim_text);
  const double min_overlap = json::get_number(d.p.at("weighting"), "overlap_min", 0.5);
  LOOM_TRY_ASSIGN(auto hyps, hypotheses_of(db, d, m.conv_id));

  // The goal a clarification belongs to.
  std::string goal;
  if (hcfg.value("promotes", "") == "transform") {
    LOOM_TRY_ASSIGN(auto msgs, db.get_msgs(m.conv_id));
    const std::size_t min_words = static_cast<std::size_t>(json::get_int(d.p.at("transforms").at("compile_refinements"), "goal_min_words", 4));
    for (const auto& x : msgs) {
      if (x.id == m.id) break;
      if (x.status != msg_status::kActive || x.role != "user" || is_traffic(d, x)) continue;
      if (text::tokenize(utf8::decode(x.text), d.fold).size() < min_words) continue;
      bool is_signal = false;
      for (const auto& kn : d.p.at("kinds").items()) {
        auto o = db.find_node("occ:" + x.id + ":" + kn.key(), occ_kind);
        is_signal = is_signal || (o && *o);
      }
      if (!is_signal) goal = x.id;
    }
    if (goal.empty()) return Json{{"kind", s.kind}, {"skipped", "no_goal"}};
  }

  // Which hypothesis: the clarifications of one goal are one; a correction joins the one it overlaps in polarity, contradicts the one it
  // overlaps against.
  std::optional<Node> hyp;
  std::vector<Node> contradicted;
  for (const auto& h : hyps) {
    if (json::get_string(h.metadata, "signal") != s.kind || json::get_string(h.metadata, "status") == "undone") continue;
    if (!goal.empty()) {
      if (json::get_string(h.metadata, "goal_message_id") == goal) hyp = h;
      continue;
    }
    std::vector<std::string> ht;
    for (const auto& t : h.metadata.value("tokens", Json::array())) ht.push_back(t.get<std::string>());
    if (overlap(tokens, ht) < min_overlap) continue;
    if (h.metadata.value("negative", false) == neg) {
      if (!hyp) hyp = h;
    } else {
      contradicted.push_back(h);
    }
  }
  const std::string now = timeutil::utc_now_iso();
  std::string hyp_id;
  Json meta;
  bool created_hyp = false;
  if (hyp) {
    hyp_id = hyp->id;
    meta = hyp->metadata;
  } else {
    created_hyp = true;
    const std::string label = "hyp:" + m.conv_id + ":" + s.kind + ":" + short_hash(s.kind + "|" + m.conv_id + "|" + (goal.empty() ? m.id : goal));
    meta = Json{{"schema", "loom.guardian_hypothesis/1"}, {"signal", s.kind}, {"conv_id", m.conv_id}, {"claim", {{"text", claim_text}, {"subject", s.subject}, {"lang", s.lang}}},
                {"scope", json::get_string(d.p.at("scopes"), "default", "conversation")}, {"scope_id", m.conv_id}, {"negative", neg}, {"tokens", tokens},
                {"occurrences", Json::array()}, {"exceptions", Json::array()}, {"contradicts", Json::array()}, {"weight", 0.0}, {"count", 0}, {"status", "observing"},
                {"created", now}, {"lang", s.lang}};
    if (!goal.empty()) meta["goal_message_id"] = goal;
    NodeOptions o;
    o.content = claim_text;
    o.metadata = meta;
    o.tags = Json::array({"guardian_hypothesis", s.kind});
    LOOM_TRY_ASSIGN(hyp_id, db.create_node(label, hyp_kind, o));
  }
  // The occurrence and how much it counts (the escalation curve is data).
  int n = 0;
  for (const auto& o : meta["occurrences"])
    if (!o.value("undone", false)) ++n;
  const Json& esc = d.p.at("escalation");
  const Json& curve = esc.at("curve");
  double factor = curve.empty() ? 1.0 : curve[std::min<std::size_t>(n, curve.size() - 1)].get<double>();
  if (!curve.empty() && static_cast<std::size_t>(n) >= curve.size()) factor += json::get_number(esc, "beyond_last_step", 0.5) * (n - static_cast<int>(curve.size()) + 1);
  if (s.tone) factor *= json::get_number(esc.at("tone"), "factor", 1.5);
  if (s.repeat) factor *= json::get_number(esc.at("repeat"), "factor", 1.8);
  factor = std::round(factor * 1000) / 1000;
  const double strength = json::get_number(kcfg, "strength", 1.0) * (s.by == "model" ? s.score : 1.0);
  NodeOptions oo;
  oo.content = clip_text(m.text, 200);
  oo.tags = Json::array({"guardian_occurrence", s.kind});
  oo.metadata = Json{{"schema", "loom.guardian_occurrence/1"}, {"signal", s.kind}, {"conv_id", m.conv_id}, {"message_id", m.id}, {"strength", strength}, {"factor", factor},
                     {"detected_by", s.by}, {"score", s.score}, {"tone", s.tone}, {"repeat", s.repeat}, {"scope", s.scope}, {"exceptions", s.exceptions}, {"hypothesis", hyp_id},
                     {"created", m.created}, {"recorded", now}};
  LOOM_TRY_ASSIGN(std::string occ_id, db.create_node(occ_label, occ_kind, oo));
  meta["occurrences"].push_back(Json{{"occurrence", occ_id}, {"message_id", m.id}, {"factor", factor}, {"strength", strength}, {"created", m.created}, {"tone", s.tone}, {"repeat", s.repeat}});
  if (!s.scope.empty()) {
    const std::string cur = json::get_string(meta, "explicit_scope");
    if (cur.empty() || scope_rank(d, s.scope) > scope_rank(d, cur)) meta["explicit_scope"] = s.scope;
  }
  const std::string evidence = json::get_string(links, "evidence", "derived_from");
  LOOM_TRY_ASSIGN(auto l1, db.create_link(occ_id, m.id, evidence, 1.0, Json{{"role", "evidence"}, {"conv_id", m.conv_id}}));
  LOOM_TRY_ASSIGN(auto l2, db.create_link(occ_id, hyp_id, json::get_string(links, "supports", "supports"), factor, Json{{"strength", strength}}));
  LOOM_TRY_ASSIGN(auto l3, db.create_link(hyp_id, m.id, evidence, 1.0, Json{{"role", "evidence"}}));
  (void)l1; (void)l2; (void)l3;

  // Exceptions the user stated in the same breath (explicit).
  for (const auto& text_ex : s.exceptions) {
    const std::string label = "exc:" + hyp_id + ":" + short_hash(text_ex);
    const std::string exc_kind = json::get_string(d.p.at("nodes"), "exception", "guardian_exception");
    LOOM_TRY_ASSIGN(auto have, db.find_node(label, exc_kind));
    if (have) continue;
    NodeOptions eo;
    eo.content = text_ex;
    eo.tags = Json::array({"guardian_exception"});
    eo.metadata = Json{{"schema", "loom.guardian_exception/1"}, {"explicit", true}, {"message_id", m.id}, {"hypothesis", hyp_id}, {"conv_id", m.conv_id}, {"created", now}};
    LOOM_TRY_ASSIGN(std::string exc_id, db.create_node(label, exc_kind, eo));
    LOOM_TRY_ASSIGN(auto a, db.create_link(exc_id, hyp_id, json::get_string(links, "except", "except"), 1.0, Json{{"explicit", true}}));
    LOOM_TRY_ASSIGN(auto b, db.create_link(exc_id, m.id, evidence, 1.0, Json{{"role", "evidence"}}));
    (void)a; (void)b;
    meta["exceptions"].push_back(Json{{"node", exc_id}, {"text", text_ex}, {"explicit", true}, {"message_id", m.id}});
  }
  // A claim against an older one in an overlapping scope: both are kept, they contradict; when the new one is narrower it is the old
  // one's exception (inferred), not a defeat.
  std::vector<Node> others = hyps;
  if (created_hyp)
    for (const auto& old : contradicted) meta["contradicts"].push_back(old.id);
  // A claim of another kind that says the same thing (a rejected proposal and a correction about the same subject) supports it.
  if (created_hyp)
    for (const auto& other : hyps) {
      if (json::get_string(other.metadata, "signal") == s.kind || json::get_string(other.metadata, "status") == "undone") continue;
      std::vector<std::string> ot;
      for (const auto& t : other.metadata.value("tokens", Json::array())) ot.push_back(t.get<std::string>());
      const double o = overlap(tokens, ot);
      if (o < min_overlap || other.metadata.value("negative", false) != neg) continue;
      LOOM_TRY_ASSIGN(auto sup, db.create_link(hyp_id, other.id, json::get_string(links, "supports", "supports"), o, Json{{"kind", "hypothesis"}}));
      (void)sup;
      if (!meta.contains("supports")) meta["supports"] = Json::array();
      meta["supports"].push_back(other.id);
    }
  weigh(d, meta, others);
  LOOM_TRY(save_meta(db, hyp_id, meta));
  if (created_hyp) {
    Node self;
    self.id = hyp_id;
    self.metadata = meta;
    for (auto& old : contradicted) {
      Json om = old.metadata;
      om["contradicts"].push_back(hyp_id);
      LOOM_TRY_ASSIGN(auto c1, db.create_link(hyp_id, old.id, json::get_string(links, "contradicts", "contradicts"), 1.0, Json{}));
      LOOM_TRY_ASSIGN(auto c2, db.create_link(old.id, hyp_id, json::get_string(links, "contradicts", "contradicts"), 1.0, Json{}));
      (void)c1; (void)c2;
      if (scope_rank(d, json::get_string(om, "scope")) > scope_rank(d, json::get_string(meta, "scope"))) {
        const std::string exc_kind = json::get_string(d.p.at("nodes"), "exception", "guardian_exception");
        NodeOptions eo;
        eo.content = claim_text;
        eo.tags = Json::array({"guardian_exception"});
        eo.metadata = Json{{"schema", "loom.guardian_exception/1"}, {"explicit", false}, {"message_id", m.id}, {"hypothesis", old.id}, {"conv_id", m.conv_id}, {"created", now}, {"inferred_from", hyp_id}};
        LOOM_TRY_ASSIGN(std::string exc_id, db.create_node("exc:" + old.id + ":" + short_hash(claim_text), exc_kind, eo));
        LOOM_TRY_ASSIGN(auto a, db.create_link(exc_id, old.id, json::get_string(links, "except", "except"), 1.0, Json{{"explicit", false}}));
        (void)a;
        om["exceptions"].push_back(Json{{"node", exc_id}, {"text", claim_text}, {"explicit", false}, {"message_id", m.id}});
      }
      weigh(d, om, {self});  // the older claim is weighed against the newer one
      LOOM_TRY(save_meta(db, old.id, om));
    }
  }
  LOOM_TRY(reevaluate(d, hyp_id));
  // The occurrence remembers its hypothesis (for the duplicate check above).
  LOOM_TRY_ASSIGN(Node latest, get_node_or(db, hyp_id));
  return Json{{"occurrence", occ_id}, {"hypothesis", hyp_id}, {"kind", s.kind}, {"weight", json::get_number(latest.metadata, "weight", 0)}, {"count", json::get_int(latest.metadata, "count", 0)},
              {"status", json::get_string(latest.metadata, "status")}, {"scope", json::get_string(latest.metadata, "scope")}, {"factor", factor}, {"detected_by", s.by}};
}

// ── observation ─────────────────────────────────────────────────────────────────────────────

Result<Json> Guardian::Impl::observe(const std::string& message_id, const Json& opts) {
  Database& db = rt.db();
  LOOM_TRY_ASSIGN(const auto data_d, data());
  const Data& d = data_d;
  LOOM_TRY_ASSIGN(auto mopt, db.get_msg(message_id));
  if (!mopt) return Error(Errc::NotFound, "no message '" + message_id + "'");
  const Message m = *mopt;
  const Method meth = method(d, json::get_string(opts, "method"));
  Json out{{"message_id", m.id}, {"conv_id", m.conv_id}, {"method", meth.name}, {"signals", Json::array()}, {"issues", Json::array()}};
  if (meth.name == "off") {
    out["skipped"] = "off";
    return out;
  }
  const bool structural = !json::get_string(opts, "rejected_proposal").empty();
  if (!structural && (m.role != "user" || is_traffic(d, m) || utf8::is_blank(m.text))) {
    out["skipped"] = "not_user_content";
    return out;
  }
  // The shared annotation layer and the levels first: cheap, local, and the same for every method.
  if (!structural) {
    auto written = annotate_message(db, d, m);
    if (written) out["annotations_written"] = *written;
    else out["issues"].push_back(written.error().message);
    auto levels = build_levels(db, d, m);
    if (levels) out["levels"] = (*levels)["levels"];
    else out["issues"].push_back(levels.error().message);
  }

  std::vector<Signal> signals;
  bool want_model = false;
  if (!json::get_string(opts, "kind").empty()) {  // declared by the user: no reading needed
    const std::string kind = json::get_string(opts, "kind");
    if (!d.p.at("kinds").contains(kind)) return Error(Errc::InvalidArgument, "unknown signal kind '" + kind + "'");
    Signal s;
    s.kind = kind;
    s.by = "user";
    s.subject = json::get_string(opts, "subject");
    s.scope = json::get_string(opts, "scope");
    s.tone = json::get_bool(opts, "tone", false);
    s.repeat = json::get_bool(opts, "repeat", false);
    for (const auto& e : opts.value("exceptions", Json::array()))
      if (e.is_string() && !e.get<std::string>().empty()) s.exceptions.push_back(e.get<std::string>());
    s.lang = guess_lang(d, m.text);
    signals.push_back(s);
  } else if (structural) {
    Signal s;
    s.kind = "proposal_rejection";
    s.by = "structural";
    s.subject = json::get_string(opts, "rejected_proposal");
    s.lang = guess_lang(d, m.text);
    signals.push_back(s);
  } else {
    // An annotation of another producer (the semantic analyzer) already says what the message is: no second reading, no model call.
    auto anns = annotations_of(db, d, m.id);
    if (anns) {
      for (const auto& a : *anns) {
        if (a.value("producer", "") == "guardian_heuristic") continue;
        const Json& kinds = d.p.at("kinds");
        for (auto it = kinds.begin(); it != kinds.end(); ++it) {
          const auto methods = text::strings_of(json::find(it.value().value("detect", Json::object()), "methods"));
          if (std::find(methods.begin(), methods.end(), "structural") != methods.end()) continue;
          if (json::get_string(it.value(), "annotation_category") == a.value("category", "")) {
            Signal s;
            s.kind = it.key();
            s.by = "annotation";
            s.score = a.value("confidence", 1.0);
            s.lang = guess_lang(d, m.text);
            signals.push_back(s);
            break;
          }
        }
        if (!signals.empty()) break;
      }
    }
    if (signals.empty() && meth.heuristic) {
      bool has_goal = false;
      if (auto msgs = db.get_msgs(m.conv_id)) {
        const std::size_t min_words = static_cast<std::size_t>(json::get_int(d.p.at("transforms").at("compile_refinements"), "goal_min_words", 4));
        for (const auto& x : *msgs) {
          if (x.id == m.id) break;
          if (x.status == msg_status::kActive && x.role == "user" && !is_traffic(d, x) && text::tokenize(utf8::decode(x.text), d.fold).size() >= min_words) has_goal = true;
        }
      }
      signals = detect_heuristic(d, m, has_goal);
    }
    if (signals.empty() && meth.model && (meth.ask_model_when.empty() || meth.ask_model_when == "no_signal")) want_model = true;
    if (signals.empty() && want_model) {
      if (json::get_bool(opts, "sync_model", false)) {
        auto ms = detect_model(d, m, out["issues"]);
        if (ms && *ms) signals.push_back(**ms);
        else if (!ms) out["issues"].push_back(ms.error().message);
      } else {
        {
          std::lock_guard lk(mu);
          queue.push_back(m.id);
        }
        cv.notify_all();
        out["queued_for_model"] = true;
      }
    }
  }
  for (const auto& s : signals) {
    auto rec = record(d, m, s);
    if (!rec) {
      out["issues"].push_back(rec.error().message);
      continue;
    }
    out["signals"].push_back(*rec);
    // The signal is a segment annotation too (the layer the selector and the analyzer read), once per category.
    if (s.by != "annotation") {
      const std::string category = json::get_string(d.p.at("kinds").at(s.kind), "annotation_category");
      auto existing = annotations_of(db, d, m.id);
      bool have = false;
      if (existing)
        for (const auto& a : *existing) have = have || (a.value("category", "") == category && a.value("producer", "") == "guardian_heuristic");
      if (!category.empty() && !have && !structural) {
        const Json& ac = d.p.at("annotations");
        NodeOptions no;
        no.metadata = Json{{"schema", "loom.annotation_category/1"}, {"category", category}};
        auto node = db.get_or_create_node(json::get_string(ac, "label_prefix", "annotation:") + category, json::get_string(ac, "node_kind", "annotation_category"), no);
        if (node) {
          NewMessageAnnotation n;
          n.message_id = m.id;
          n.node_id = *node;
          n.start_char = 0;
          n.end_char = static_cast<std::int64_t>(utf8::length(m.text));
          n.origin = s.by == "user" ? "user" : (s.by == "model" ? "model" : "recorded");
          n.metadata = Json{{"schema", json::get_string(ac, "schema")}, {"category", category}, {"producer", "guardian_heuristic"}, {"confidence", s.score}, {"signal", s.kind}};
          n.expected_text_hash = sha256_hex(m.text);
          auto made = db.create_message_annotation(n);
          if (!made) out["issues"].push_back(made.error().message);
        }
      }
    }
  }
  if (json::get_bool(opts, "post", false)) {
    auto posted = post_pending(d, m.conv_id);
    if (posted) out["posted"] = *posted;
    else out["issues"].push_back(posted.error().message);
  }
  return out;
}

void Guardian::Impl::on_event(const Json& e) {
  try {
    const std::string id = json::get_string(e, "id");
    const std::string conv = json::get_string(e, "conv_id");
    const std::string role = json::get_string(e, "role");
    if (id.empty() || conv.empty()) return;
    if (role == "user") {
      auto r = observe(id, Json::object());
      if (!r) log::write(log::Level::Debug, kLog, "observe failed: " + r.error().message);
    } else if (role == "assistant") {
      auto d = data();
      if (!d) return;
      if (method(*d).name == "off") return;
      auto r = post_pending(*d, conv);
      if (!r) log::write(log::Level::Debug, kLog, "proposals failed: " + r.error().message);
    }
  } catch (const std::exception& ex) {
    log::write(log::Level::Warning, kLog, std::string("event handler: ") + ex.what());
  }
}

void Guardian::Impl::run_worker() {
  for (;;) {
    std::string id;
    {
      std::unique_lock lk(mu);
      cv.wait(lk, [&] { return stopping || !queue.empty(); });
      if (stopping && queue.empty()) return;
      id = queue.front();
      queue.pop_front();
      ++busy;
    }
    try {
      auto d = data();
      auto m = rt.db().get_msg(id);
      if (d && m && *m) {
        Json issues = Json::array();
        auto ms = detect_model(*d, **m, issues);
        for (const auto& i : issues) log::write(log::Level::Debug, kLog, "model reading: " + i.dump());
        if (ms && *ms) {
          auto rec = record(*d, **m, **ms);
          if (rec) (void)post_pending(*d, (*m)->conv_id);
        }
      }
    } catch (const std::exception& ex) {
      log::write(log::Level::Warning, kLog, std::string("model reading: ") + ex.what());
    }
    {
      std::lock_guard lk(mu);
      --busy;
    }
    idle_cv.notify_all();
  }
}

// ── public surface ──────────────────────────────────────────────────────────────────────────

Guardian::Guardian(Runtime& rt) : impl_(std::make_unique<Impl>(rt)) {}
Guardian::~Guardian() { stop(); }

void Guardian::start() {
  if (impl_->worker.joinable()) return;
  impl_->stopping = false;
  impl_->worker = std::thread([this] { impl_->run_worker(); });
  impl_->sub = ScopedSubscription(impl_->rt.bus(), impl_->rt.bus().on(events::kMsgCreated, [this](std::string_view, const Json& e) { impl_->on_event(e); }));
}

void Guardian::stop() {
  impl_->sub.reset();
  {
    std::lock_guard lk(impl_->mu);
    impl_->stopping = true;
  }
  impl_->cv.notify_all();
  if (impl_->worker.joinable()) impl_->worker.join();
}

void Guardian::drain() {
  std::unique_lock lk(impl_->mu);
  impl_->idle_cv.wait(lk, [&] { return impl_->queue.empty() && impl_->busy == 0; });
}

void Guardian::note_rejection(const std::string& conv_id, const std::string& proposal_message_id, const Json& proposal, const std::string& answer_message_id) {
  try {
    auto d = impl_->data();
    if (!d || impl_->method(*d).name == "off") return;
    Database& db = impl_->rt.db();
    if (json::get_string(proposal, "action") == "guardian.promote") {
      // The user declined the guardian's own proposal: not asked again until the evidence doubles.
      const std::string hyp = json::get_string(proposal.value("args", Json::object()), "hypothesis");
      auto n = db.get_node(hyp);
      if (n && *n) {
        Json meta = (*n)->metadata;
        meta["status"] = "rejected";
        meta["rejected_weight"] = json::get_number(meta, "weight", 0);
        meta["rejected_at"] = timeutil::utc_now_iso();
        meta["proposal_message_id"] = proposal_message_id;
        (void)save_meta(db, hyp, meta);
      }
      return;
    }
    if (answer_message_id.empty()) return;
    std::string title = json::get_string(proposal, "title");
    if (title.empty()) title = json::get_string(proposal, "action");
    auto r = impl_->observe(answer_message_id, Json{{"rejected_proposal", title}, {"conv_id", conv_id}});
    if (!r) log::write(log::Level::Debug, kLog, "rejection not recorded: " + r.error().message);
  } catch (const std::exception& ex) {
    log::write(log::Level::Warning, kLog, std::string("note_rejection: ") + ex.what());
  }
}

}  // namespace loom::guardian
