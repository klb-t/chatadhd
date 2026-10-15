// The cycle: message -> signal -> occurrence -> hypothesis (weight, scope, exceptions) -> proposal. See loom/guardian.h.
#include <algorithm>
#include <cmath>
#include <set>

#include "impl.h"
#include "loom/annotation_layer.h"
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

std::vector<Signal> Guardian::Impl::detect_heuristic(const Data& d, const Message& m, bool has_goal, const std::set<std::string>& skip_kinds) {
  const auto cps = utf8::decode(m.text);
  const auto toks = text::tokenize(cps, d.fold);
  const Json& kinds = d.p.at("kinds");
  const Json& conf = d.p.at("signal_confidence");
  std::vector<Signal> found;
  for (auto it = kinds.begin(); it != kinds.end(); ++it) {
    if (skip_kinds.count(it.key())) continue;
    const Json& detect = it.value().value("detect", Json::object());
    const Json* h = json::find(detect, "heuristic");
    if (!h) continue;
    if (json::get_bool(detect, "needs_goal", false) && !has_goal) continue;
    const auto hits = text::find(toks, text::compile(all_phrases(h->value("markers", Json::object())), d.fold));
    if (hits.empty()) continue;
    const std::size_t window = static_cast<std::size_t>(json::get_int(*h, "start_window", 5));
    // One segment per cluster of markers that share a clause: a message can hold several segments of one kind.
    struct Cluster { std::size_t from, to, first_hit, last_hit; double score; int n; bool at_start; };
    std::vector<Cluster> clusters;
    for (const auto& hit : hits) {
      const auto [a, b] = text::clause(cps, toks, hit.first, hit.last);
      const bool at_start = hit.first < window;
      const double w = at_start ? json::get_number(*h, "start_weight", 1.0) : json::get_number(*h, "anywhere_weight", 0.6);
      if (!clusters.empty() && a <= clusters.back().to) {
        auto& c = clusters.back();
        c.to = std::max(c.to, b);
        c.last_hit = hit.last;
        c.score += w;
        ++c.n;
        c.at_start = c.at_start || at_start;
      } else {
        clusters.push_back(Cluster{a, b, hit.first, hit.last, w, 1, at_start});
      }
    }
    int index = 0;
    for (const auto& c : clusters) {
      if (c.score < json::get_number(*h, "min_score", 0.6)) continue;
      Signal s;
      s.kind = it.key();
      s.score = c.score;
      s.by = "heuristic";
      s.start = static_cast<std::int64_t>(c.from);
      s.end = static_cast<std::int64_t>(c.to);
      s.cluster = index++;
      s.confidence = std::min(json::get_number(conf, "max", 0.95), (c.at_start ? json::get_number(conf, "start", 0.9) : json::get_number(conf, "anywhere", 0.65)) + json::get_number(conf, "per_extra_hit", 0.05) * (c.n - 1));
      s.segment = utf8::encode(std::u32string_view(cps).substr(c.from, c.to - c.from));
      s.method_version = annotations::method_version(it.value().value("detect", Json::object()).dump());
      if (json::get_bool(detect, "claim_after_marker", false)) {
        // The user states the rule himself: what follows the marker, to the end of the sentence, is the claim.
        std::size_t from = toks[c.last_hit].end, to = from;
        while (to < cps.size() && cps[to] != U'.' && cps[to] != U'!' && cps[to] != U'?' && cps[to] != U'\n') ++to;
        s.subject = clip_text(utf8::encode(std::u32string_view(cps).substr(from, to - from)), 400);
        while (!s.subject.empty() && (s.subject.front() == ',' || s.subject.front() == ':' || s.subject.front() == ' ')) s.subject.erase(s.subject.begin());
        if (s.subject.empty()) continue;
        s.end = static_cast<std::int64_t>(to);
      }
      // A segment that quotes or imagines what it says is annotated with that qualifier and is not evidence.
      std::size_t sentence_first = c.first_hit;
      while (sentence_first > 0 && !text::sentence_start(cps, toks, sentence_first)) --sentence_first;
      std::vector<text::Tok> before(toks.begin() + static_cast<std::ptrdiff_t>(sentence_first), toks.begin() + static_cast<std::ptrdiff_t>(c.first_hit));
      const Json& quals = d.p.at("qualifiers");
      for (auto q = quals.begin(); q != quals.end(); ++q) {
        if (!q.value().is_object() || !q.value().contains("markers")) continue;
        if (text::find(before, text::compile(all_phrases(q.value().at("markers")), d.fold)).empty()) continue;
        s.qualifier = q.key();
        s.counts = json::get_bool(q.value(), "counts_as_evidence", false);
        break;
      }
      found.push_back(std::move(s));
    }
  }
  // A kind that yields to another found in the same message gives way (a correction already says "also ...").
  std::vector<Signal> kept;
  for (auto& s : found) {
    bool yields = false;
    for (const auto& to : text::strings_of(json::find(kinds.at(s.kind), "yields_to")))
      for (const auto& o : found) yields = yields || (o.kind == to && o.qualifier.empty());
    if (!yields) kept.push_back(std::move(s));
  }
  if (kept.empty()) return {};
  std::sort(kept.begin(), kept.end(), [](const Signal& a, const Signal& b) { return a.start < b.start; });
  // What the message adds: tone, a repeated demand, a scope, exceptions.
  const std::string lowered = utf8::to_lower(m.text);
  const Json& esc = d.p.at("escalation");
  const auto tone = text::compile(all_phrases(esc.at("tone").at("markers")), d.fold);
  const auto repeat = text::compile(all_phrases(esc.at("repeat").at("markers")), d.fold);
  const bool has_tone = !text::find(toks, tone).empty() || text::contains_raw(lowered, tone);
  const bool has_repeat = !text::find(toks, repeat).empty() || text::contains_raw(lowered, repeat);
  const std::string lang = guess_lang(d, m.text);
  for (auto& s : kept) {
    s.tone = has_tone;
    s.repeat = has_repeat;
    s.lang = lang;
  }
  // An explicit scope belongs to the segment it stands in (the only segment of the message owns any).
  for (const std::string scope : {"person", "conversation", "message"}) {
    const Json* markers = json::find(d.p.at("scopes").at("markers"), scope);
    if (!markers) continue;
    const auto sh = text::find(toks, text::compile(all_phrases(*markers), d.fold));
    if (sh.empty()) continue;
    const auto pos = static_cast<std::int64_t>(toks[sh.front().first].start);
    Signal* owner = nullptr;
    for (auto& s : kept)
      if (s.start <= pos && pos < s.end && s.scope.empty()) owner = &s;
    if (!owner && kept.size() == 1 && kept.front().scope.empty()) owner = &kept.front();
    if (owner) owner->scope = scope;
  }
  const Json& ex = d.p.at("exceptions");
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
    if (to <= from) continue;
    const std::string phrase = clip_text(utf8::encode(std::u32string_view(cps).substr(from, to - from)), 200);
    if (phrase.empty()) continue;
    // The exception belongs to the nearest segment before it (the first when it stands at the start).
    const auto at = static_cast<std::int64_t>(toks[hit.first].start);
    Signal* owner = &kept.front();
    for (auto& s : kept)
      if (s.end <= at || (s.start <= at && at < s.end)) owner = &s;
    owner->exceptions.push_back(phrase);
    owner->exception_spans.emplace_back(static_cast<std::int64_t>(toks[hit.first].start), static_cast<std::int64_t>(to));
  }
  return kept;
}

Result<std::optional<Signal>> Guardian::Impl::detect_model(const Data& d, const Message& m, Json& issues) {
  const Json& call = d.p.at("method").at("model_call");
  const std::string model = model_for(d, m.conv_id);
  if (model.empty()) {
    issues.push_back("no analysis model set: the guardian reads with word lists only (set guardian_model, or the ports role guardian / analysis.fast)");
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
  // The reading has a cost and a receipt; the receipt is kept even when the model finds nothing, so nobody asks again.
  const Json cost{{"calls", 1}, {"input_bytes", system.size() + user.size()}, {"output_bytes", r.text.size()}, {"receipt", r.operation_id}, {"model", r.model}};
  const std::string version = annotations::method_version(system);
  (void)annotations::record_run(rt.db(), policy_of(d), m.id, "guardian_model", version, text::strings_of(nullptr), cost);
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
  s.cost = cost;
  s.method_version = version;
  s.subject = clip_text(json::get_string(answer, "subject"), 400);
  s.tone = json::get_string(answer, "tone") == "sharp";
  s.scope = json::get_string(answer, "scope");
  for (const auto& e : answer.value("exceptions", Json::array()))
    if (e.is_string() && !e.get<std::string>().empty()) s.exceptions.push_back(clip_text(e.get<std::string>(), 200));
  s.lang = guess_lang(d, m.text);
  return std::optional<Signal>(s);
}

Result<std::string> Guardian::Impl::goal_of(const Data& d, const Message& m, const std::vector<Message>& msgs) {
  // The goal a clarification belongs to: the nearest instruction (or thread opening) before it that the analyzer annotated; else, only when
  // nothing annotates the conversation yet, the first substantial message of the user. A refinement without a sure goal is not compiled.
  const Json& tr = d.p.at("transforms").at("compile_refinements");
  const std::size_t min_words = static_cast<std::size_t>(json::get_int(tr, "goal_min_words", 4));
  const std::string occ_kind = json::get_string(d.p.at("nodes"), "occurrence", "guardian_occurrence");
  const annotations::Policy pol = policy_of(d);
  std::string nearest, first_plain;
  bool conversation_annotated = false;
  for (const auto& x : msgs) {
    if (x.id == m.id) break;
    if (x.status != msg_status::kActive || x.role != "user" || is_traffic(d, x)) continue;
    auto anns = annotations::list(rt.db(), pol, x.id);
    if (anns) {
      for (const auto& a : *anns) {
        const std::string c = a.value("category", "");
        if (a.value("producer", "") == "guardian_heuristic" || a.value("producer", "") == "guardian_model") continue;
        conversation_annotated = true;
        if (c == "instruction" || (c == "thread" && nearest.empty())) nearest = x.id;
        if (c == "instruction") nearest = x.id;
      }
    }
    if (text::tokenize(utf8::decode(x.text), d.fold).size() < min_words) continue;
    bool is_signal = false;
    for (const auto& kn : d.p.at("kinds").items()) {
      auto o = rt.db().find_node("occ:" + x.id + ":" + kn.key(), occ_kind);
      is_signal = is_signal || (o && *o);
    }
    if (!is_signal && first_plain.empty()) first_plain = x.id;
  }
  if (!nearest.empty()) return nearest;
  return conversation_annotated ? std::string() : first_plain;
}

Result<std::vector<Node>> Guardian::Impl::all_hypotheses(const Data& d) {
  return rt.db().list_nodes(json::get_string(d.p.at("nodes"), "hypothesis", "guardian_hypothesis"), 100000);
}

// A claim against a hypothesis that became a rule: once it weighs enough, switching the rule off is proposed (his word decides).
Status Guardian::Impl::lifecycle_against(const Data& d, const std::string& rule_hyp, const std::string& against_hyp) {
  Database& db = rt.db();
  LOOM_TRY_ASSIGN(Node r, get_node_or(db, rule_hyp));
  if (json::get_string(r.metadata, "status") != "promoted") return ok_status();
  LOOM_TRY_ASSIGN(Node a, get_node_or(db, against_hyp));
  // What the claim against it holds on its own (before it is weighed against the rule it contradicts).
  double w = 0;
  for (const auto& o : a.metadata.value("occurrences", Json::array()))
    if (!o.value("undone", false)) w += o.value("strength", 1.0) * o.value("factor", 1.0) * decay(d, o.value("created", ""));
  const double need = json::get_number(d.p.at("lifecycle"), "disable_threshold", 1.0);
  const double declined = json::get_number(r.metadata, "disable_declined_weight", 0);
  if (w + 0.01 < need || (declined > 0 && w < json::get_number(d.p.at("weighting"), "reask_factor", 2.0) * declined)) return ok_status();
  Json meta = r.metadata;
  meta["disable_suggested"] = true;
  meta["disable_against"] = against_hyp;
  meta["disable_weight"] = w;
  return save_meta(db, rule_hyp, meta);
}

// ── occurrences and hypotheses ──────────────────────────────────────────────────────────────

// What a hypothesis stands on: its own occurrences and those of its siblings (the same correction heard in other conversations).
Guardian::Impl::Evidence Guardian::Impl::evidence_of(const Data& d, const Node& h) {
  Evidence ev;
  std::vector<Node> members{h};
  for (const auto& s : h.metadata.value("siblings", Json::array())) {
    if (!s.value("active", true)) continue;
    auto n = rt.db().get_node(s.value("id", ""));
    if (n && *n && json::get_string((*n)->metadata, "status") != "undone") members.push_back(**n);
  }
  const annotations::Policy pol = policy_of(d);
  for (const auto& m : members) {
    ev.weight += json::get_number(m.metadata, "weight", 0);
    ev.count += static_cast<int>(json::get_int(m.metadata, "count", 0));
    ev.convs.insert(json::get_string(m.metadata, "conv_id"));
    for (const auto& o : m.metadata.value("occurrences", Json::array())) {
      if (o.value("undone", false)) continue;
      const std::string created = o.value("created", "");
      if (!created.empty() && (ev.first.empty() || created < ev.first)) ev.first = created;
      if (!created.empty() && created > ev.last) ev.last = created;
      if (auto anns = annotations::list(rt.db(), pol, o.value("message_id", "")))
        for (const auto& a : *anns)
          if (a.value("category", "") == "thread" && !a.value("data", Json::object()).value("thread", "").empty()) ev.threads.insert(a["data"]["thread"].get<std::string>());
    }
  }
  return ev;
}

// The scope the evidence supports: grown from diversity (conversations, kinds of task, time span), never from how often one thing was said in one place.
Json Guardian::Impl::widening(const Data& d, const Evidence& ev, const Json& meta) {
  std::string best = json::get_string(d.p.at("scopes"), "default", "conversation");
  Json why = Json::array();
  for (const auto& step : d.p.at("scopes").value("by_evidence", Json::array())) {
    bool holds = true;
    if (step.contains("distinct_conversations")) holds = holds && static_cast<std::int64_t>(ev.convs.size()) >= json::get_int(step, "distinct_conversations", 0);
    if (step.contains("distinct_task_kinds")) holds = holds && static_cast<std::int64_t>(ev.threads.size()) >= json::get_int(step, "distinct_task_kinds", 0);
    if (step.contains("min_span_hours")) {
      auto a = timeutil::parse_iso_utc(ev.first), b = timeutil::parse_iso_utc(ev.last);
      holds = holds && a && b && std::chrono::duration<double>(*b - *a).count() / 3600.0 >= json::get_number(step, "min_span_hours", 0);
    }
    if (!holds) continue;
    const std::string scope = json::get_string(step, "scope");
    if (scope_rank(d, scope) > scope_rank(d, best)) best = scope;
    why.push_back(Json{{"kind", json::get_string(step, "why", "evidence")}, {"n", json::get_string(step, "why") == "task_kinds" ? ev.threads.size() : ev.convs.size()}});
  }
  const std::string requested = json::get_string(meta, "requested_scope");
  if (!requested.empty() && scope_rank(d, requested) > scope_rank(d, best)) best = requested;
  if (!requested.empty()) why.push_back(Json{{"kind", "requested"}, {"n", 1}});
  return Json{{"candidate", best}, {"why", why}};
}

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
  // The scope in force stays what it was (the default, or what the user chose); what the evidence would support is a question, kept apart.
  const std::string explicit_scope = json::get_string(meta, "explicit_scope");
  std::string current = json::get_string(meta, "scope");
  if (current.empty()) current = json::get_string(d.p.at("scopes"), "default", "conversation");
  if (!explicit_scope.empty() && scope_rank(d, explicit_scope) > scope_rank(d, current)) current = explicit_scope;
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
  const Evidence ev = evidence_of(d, h);
  const Json wide = widening(d, ev, meta);
  meta["scope_candidate"] = wide["candidate"];
  meta["scope_why"] = wide["why"];
  const double weight = ev.weight;
  const int need = promotes == "transform" ? static_cast<int>(json::get_int(d.p.at("transforms").at("compile_refinements"), "min_occurrences", 3)) : 1;
  bool ready = false;
  if (status == "observing") ready = weight >= threshold && ev.count >= need;
  else if (status == "rejected") ready = weight >= json::get_number(d.p.at("weighting"), "reask_factor", 2.0) * json::get_number(meta, "rejected_weight", 0) && ev.count >= need;
  // A sibling (the same correction in another conversation) already asks, or already became a rule: one question, one rule.
  bool covered_by_sibling = false;
  for (const auto& s : meta.value("siblings", Json::array())) {
    auto n = db.get_node(s.value("id", ""));
    if (!n || !*n) continue;
    const std::string ss = json::get_string((*n)->metadata, "status");
    if (ss == "ready" || ss == "proposed" || ss == "promoted") covered_by_sibling = true;
  }
  if (!ready || covered_by_sibling) {
    LOOM_TRY(save_meta(db, hyp_id, meta));
    return ok_status();
  }
  meta["status"] = "ready";
  meta["ready_at"] = timeutil::utc_now_iso();
  LOOM_TRY(save_meta(db, hyp_id, meta));
  // Applying without asking is only for a scope the conversation already holds: a wider scope is always a question.
  const bool widens = scope_rank(d, json::get_string(meta, "scope_candidate")) > scope_rank(d, json::get_string(d.p.at("scopes"), "default", "conversation"));
  if (mode == "apply" && !widens) {
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
  const std::string occ_label = "occ:" + m.id + ":" + s.kind + (s.cluster > 0 ? ":" + std::to_string(s.start) : std::string());
  LOOM_TRY_ASSIGN(auto prior, db.find_node(occ_label, occ_kind));
  if (prior) return Json{{"occurrence", prior->id}, {"hypothesis", prior->metadata.value("hypothesis", "")}, {"kind", s.kind}, {"duplicate", true}};

  // What the occurrence says: the user's words (kept as the evidence quote), and what they ask for (the tokens and concepts the match works on).
  const std::string quote = clip_text(m.text, 300);
  const std::string claim_text = !s.subject.empty() ? s.subject : clip_text(m.text, 400);
  const auto tokens = claim_tokens(d, claim_text);
  const auto concepts = concepts_in(d, claim_text);
  const bool neg = negative(d, claim_text);
  const bool named_by_source = !s.subject.empty();
  const std::string claim_source = s.by == "user" ? "user" : (s.by == "model" ? "model" : (s.by == "structural" ? "structural" : (json::get_bool(hcfg, "named", false) ? "stated" : "")));
  LOOM_TRY_ASSIGN(auto hyps, hypotheses_of(db, d, m.conv_id));

  // The goal a clarification belongs to.
  std::string goal;
  if (hcfg.value("promotes", "") == "transform") {
    LOOM_TRY_ASSIGN(auto msgs, db.get_msgs(m.conv_id));
    LOOM_TRY_ASSIGN(goal, goal_of(d, m, msgs));
    if (goal.empty()) return Json{{"kind", s.kind}, {"skipped", "no_goal"}};
  }

  // Which hypothesis: the clarifications of one goal are one; a correction joins the one it matches by the method in force (overlap, embedding,
  // model, user), contradicts the one it matches against; a match in another conversation is a sibling, evidence of diversity, not a join.
  std::optional<Node> hyp;
  std::optional<Node> sibling;
  Match matched;
  std::vector<Node> contradicted;
  const double min_overlap = json::get_number(d.p.at("weighting"), "overlap_min", 0.5);
  if (!goal.empty()) {
    for (const auto& h : hyps)
      if (json::get_string(h.metadata, "signal") == s.kind && json::get_string(h.metadata, "status") != "undone" && json::get_string(h.metadata, "goal_message_id") == goal) hyp = h;
  } else {
    for (const auto& h : hyps) {
      if (json::get_string(h.metadata, "signal") != s.kind || json::get_string(h.metadata, "status") == "undone") continue;
      std::vector<std::string> ht;
      for (const auto& t : h.metadata.value("tokens", Json::array())) ht.push_back(t.get<std::string>());
      if (overlap(tokens, ht) >= min_overlap && h.metadata.value("negative", false) != neg) contradicted.push_back(h);
    }
    LOOM_TRY_ASSIGN(auto pool, all_hypotheses(d));
    LOOM_TRY_ASSIGN(matched, find_match(d, m, s.kind, tokens, claim_text, neg, pool));
    if (matched.target) {
      if (json::get_string(matched.target->metadata, "conv_id") == m.conv_id) hyp = matched.target;
      else sibling = matched.target;
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
    meta = Json{{"schema", "loom.guardian_hypothesis/2"}, {"signal", s.kind}, {"conv_id", m.conv_id},
                {"claim", {{"text", named_by_source ? claim_text : std::string()}, {"subject", s.subject}, {"lang", s.lang}, {"named", named_by_source}, {"source", named_by_source ? claim_source : std::string()}, {"quotes", Json::array()}}},
                {"scope", json::get_string(d.p.at("scopes"), "default", "conversation")}, {"scope_id", m.conv_id}, {"negative", neg}, {"tokens", tokens},
                {"occurrences", Json::array()}, {"exceptions", Json::array()}, {"contradicts", Json::array()}, {"siblings", Json::array()}, {"weight", 0.0}, {"count", 0}, {"status", "observing"},
                {"created", now}, {"lang", s.lang}};
    if (!goal.empty()) meta["goal_message_id"] = goal;
    NodeOptions o;
    o.content = named_by_source ? claim_text : std::string();
    o.metadata = meta;
    o.tags = Json::array({"guardian_hypothesis", s.kind});
    LOOM_TRY_ASSIGN(hyp_id, db.create_node(label, hyp_kind, o));
  }
  if (named_by_source && claim_source != "" && json::get_string(meta["claim"], "source") != "user") {  // the user's word is never replaced by a model's or a concept's
    meta["claim"]["text"] = claim_text;
    meta["claim"]["subject"] = s.subject;
    meta["claim"]["named"] = true;
    meta["claim"]["source"] = claim_source;
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
  const double strength = json::get_number(kcfg, "strength", 1.0) * (s.by == "model" || s.by == "annotation" ? s.score : 1.0);
  NodeOptions oo;
  oo.content = clip_text(m.text, 200);
  oo.tags = Json::array({"guardian_occurrence", s.kind});
  oo.metadata = Json{{"schema", "loom.guardian_occurrence/2"}, {"signal", s.kind}, {"conv_id", m.conv_id}, {"message_id", m.id}, {"strength", strength}, {"factor", factor},
                     {"detected_by", s.by}, {"score", s.score}, {"confidence", s.confidence}, {"tone", s.tone}, {"repeat", s.repeat}, {"scope", s.scope}, {"exceptions", s.exceptions}, {"hypothesis", hyp_id},
                     {"created", m.created}, {"recorded", now}, {"concepts", concepts}};
  LOOM_TRY_ASSIGN(std::string occ_id, db.create_node(occ_label, occ_kind, oo));
  Json occ_entry{{"occurrence", occ_id}, {"message_id", m.id}, {"factor", factor}, {"strength", strength}, {"created", m.created}, {"tone", s.tone}, {"repeat", s.repeat},
                 {"quote", quote}, {"concepts", concepts}};
  // A join that is not the same words is a merge: recorded as an edge with its confidence and method, so that it can be taken back
  // without losing the occurrence.
  if (hyp && matched.target && !matched.strict && matched.target->id == hyp->id) {
    std::string with;
    for (auto it = hyp->metadata["occurrences"].rbegin(); it != hyp->metadata["occurrences"].rend(); ++it)
      if (!it->value("undone", false)) {
        with = it->value("occurrence", "");
        break;
      }
    LOOM_TRY_ASSIGN(auto edge, db.create_link(occ_id, with, json::get_string(d.p.at("matching"), "edge", "same_as"), matched.confidence, Json{{"method", matched.method}, {"hypothesis", hyp_id}}));
    occ_entry["merge"] = Json{{"method", matched.method}, {"confidence", matched.confidence}, {"with", with}, {"edge", edge}};
  }
  meta["occurrences"].push_back(occ_entry);
  if (!s.scope.empty()) {
    const std::string field = s.by == "user" ? "explicit_scope" : "requested_scope";  // his own word through the API decides; words in a message ask
    const std::string cur = json::get_string(meta, field);
    if (cur.empty() || scope_rank(d, s.scope) > scope_rank(d, cur)) meta[field] = s.scope;
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
  // The same thing heard in another conversation: siblings, both ways, with the confidence and method of the match.
  if (sibling && created_hyp) {
    LOOM_TRY_ASSIGN(auto edge, db.create_link(hyp_id, sibling->id, json::get_string(d.p.at("matching"), "edge", "same_as"), matched.confidence, Json{{"method", matched.method}, {"conv_id", sibling->metadata.value("conv_id", "")}}));
    meta["siblings"].push_back(Json{{"id", sibling->id}, {"conv_id", sibling->metadata.value("conv_id", "")}, {"confidence", matched.confidence}, {"method", matched.method}, {"edge", edge}, {"active", true}});
    Json other = sibling->metadata;
    if (!other.contains("siblings")) other["siblings"] = Json::array();
    other["siblings"].push_back(Json{{"id", hyp_id}, {"conv_id", m.conv_id}, {"confidence", matched.confidence}, {"method", matched.method}, {"edge", edge}, {"active", true}});
    LOOM_TRY(save_meta(db, sibling->id, other));
  }
  refresh_claim(d, meta);
  weigh(d, meta, others);
  LOOM_TRY(save_meta(db, hyp_id, meta, meta["claim"].value("text", "")));
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
      auto effective = [&](const Json& x) {
        std::string a = json::get_string(x, "scope");
        for (const char* k : {"requested_scope", "scope_candidate"})
          if (scope_rank(d, json::get_string(x, k)) > scope_rank(d, a)) a = json::get_string(x, k);
        return a;
      };
      if (scope_rank(d, effective(om)) > scope_rank(d, effective(meta))) {
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
      // A correction against a rule the user accepted: the rule is asked about (switching it off is his word), once the claim against it weighs enough.
      LOOM_TRY(lifecycle_against(d, old.id, hyp_id));
    }
  }
  LOOM_TRY(reevaluate(d, hyp_id));
  // The occurrence remembers its hypothesis (for the duplicate check above).
  LOOM_TRY_ASSIGN(Node latest, get_node_or(db, hyp_id));
  return Json{{"occurrence", occ_id}, {"hypothesis", hyp_id}, {"kind", s.kind}, {"weight", json::get_number(latest.metadata, "weight", 0)}, {"count", json::get_int(latest.metadata, "count", 0)},
              {"status", json::get_string(latest.metadata, "status")}, {"scope", json::get_string(latest.metadata, "scope")}, {"factor", factor}, {"detected_by", s.by},
              {"merged", matched.target && !matched.strict}, {"sibling", sibling ? sibling->id : std::string()}};
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
  const annotations::Policy pol = policy_of(d);
  // The shared annotation layer and the levels first: cheap, local, and the same for every method.
  if (!structural) {
    auto written = annotate_message(db, d, m);
    if (written) out["annotations_written"] = *written;
    else out["issues"].push_back(written.error().message);
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
    // What other producers (the semantic analyzer ...) already say about the message is read from the layer: a consumer knows categories and
    // confidences, not producers. A kind they cover is not read again, and a message a model has already read is not sent to a model a second time.
    std::set<std::string> covered;
    bool model_read = false;
    auto anns = annotations::list(db, pol, m.id);
    if (anns) {
      const Json& kinds = d.p.at("kinds");
      for (const auto& a : *anns) {
        const std::string producer = a.value("producer", "");
        // What the guardian itself wrote is not read back as a second opinion (data: producers.<name>.own); the user's own annotations are his.
        if (json::get_bool(pol.p.value("producers", Json::object()).value(producer, Json::object()), "own", false) || producer == "user") continue;
        if (a.value("origin", "") == "model") model_read = true;
        for (auto it = kinds.begin(); it != kinds.end(); ++it) {
          const auto methods = text::strings_of(json::find(it.value().value("detect", Json::object()), "methods"));
          if (std::find(methods.begin(), methods.end(), "structural") != methods.end()) continue;
          if (json::get_string(it.value(), "annotation_category") != a.value("category", "")) continue;
          Signal s;
          s.kind = it.key();
          s.by = "annotation";
          s.score = a.value("confidence", 1.0);
          s.confidence = a.value("confidence", 1.0);
          s.start = a.value("start_char", 0);
          s.end = a.value("end_char", 0);
          s.qualifier = a.value("qualifier", "");
          if (!s.qualifier.empty()) s.counts = json::get_bool(d.p.at("qualifiers").value(s.qualifier, Json::object()), "counts_as_evidence", false);
          s.segment = a.value("excerpt", "");
          s.lang = guess_lang(d, m.text);
          covered.insert(it.key());
          signals.push_back(std::move(s));
          break;
        }
      }
    }
    if (auto runs = annotations::runs_of(db, pol, m.id)) {
      for (const auto& r : *runs)
        if (json::get_int(r.value("cost", Json::object()), "calls", 0) > 0) model_read = true;
    }
    if (meth.heuristic) {
      bool has_goal = false;
      if (auto msgs = db.get_msgs(m.conv_id)) {
        const std::size_t min_words = static_cast<std::size_t>(json::get_int(d.p.at("transforms").at("compile_refinements"), "goal_min_words", 4));
        for (const auto& x : *msgs) {
          if (x.id == m.id) break;
          if (x.status == msg_status::kActive && x.role == "user" && !is_traffic(d, x) && text::tokenize(utf8::decode(x.text), d.fold).size() >= min_words) {
            has_goal = true;
            break;
          }
        }
      }
      for (auto& s : detect_heuristic(d, m, has_goal, covered)) signals.push_back(std::move(s));
    }
    if (signals.empty() && meth.model && !model_read && (meth.ask_model_when.empty() || meth.ask_model_when == "no_signal")) want_model = true;
    // The analyzer's one prompt contract reads the message for everyone: while its reading is still to come, the guardian does not send the message
    // to a model of its own (it reads the layer when the analyzer announces it); once the analyzer is done without a model reading, it does.
    if (want_model && choice_for(d, m.conv_id).model.empty()) {
      want_model = false;
      out["model_reading"] = "no analysis model set";
      out["issues"].push_back(say(d, m.text.empty() ? "en" : guess_lang(d, m.text), "no_model"));
    }
    if (want_model && (m.semantic_status == "pending" || m.semantic_status == "executing") && annotations::analyzer_expected(rt.config(), m.text)) {
      want_model = false;
      out["model_reading"] = "awaiting_analyzer";
    }
    if (model_read && signals.empty()) out["model_reading"] = "reused";
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
  if (!structural) {
    auto levels = build_levels(db, d, m);
    if (levels) out["levels"] = (*levels)["levels"];
    else out["issues"].push_back(levels.error().message);
  }
  for (const auto& s : signals) {
    // The signal is a segment annotation of the layer (the layer the selector and the analyzer read) whether or not it counts as evidence.
    if (s.by != "annotation" && !structural) {
      auto written = write_signal(d, m, s);
      if (!written) out["issues"].push_back(written.error().message);
    }
    if (!s.counts) {
      out["signals"].push_back(Json{{"kind", s.kind}, {"qualifier", s.qualifier}, {"counted", false}});
      continue;
    }
    if (d.p.at("kinds").at(s.kind).value("hypothesis", Json::object()).value("promotes", "") == "annotation") {
      out["signals"].push_back(Json{{"kind", s.kind}, {"annotation_only", true}});
      continue;
    }
    auto rec = record(d, m, s);
    if (!rec) {
      out["issues"].push_back(rec.error().message);
      continue;
    }
    out["signals"].push_back(*rec);
  }
  if (json::get_bool(opts, "post", false)) {
    auto posted = post_pending(d, m.conv_id);
    if (posted) out["posted"] = *posted;
    else out["issues"].push_back(posted.error().message);
  }
  return out;
}

// A signal as an annotation of the message: the segment, the category of its kind, the confidence it was computed with, the cost of reading it.
Result<std::string> Guardian::Impl::write_signal(const Data& d, const Message& m, const Signal& s) {
  Database& db = rt.db();
  const annotations::Policy pol = policy_of(d);
  const std::string category = json::get_string(d.p.at("kinds").at(s.kind), "annotation_category");
  if (category.empty()) return std::string();
  annotations::Claim c;
  c.message_id = m.id;
  c.category = category;
  c.start_char = s.start < 0 ? 0 : s.start;
  c.end_char = s.end < 0 ? -1 : s.end;
  c.producer = s.by == "user" ? "user" : (s.by == "model" ? "guardian_model" : "guardian_heuristic");
  c.method = s.by;
  c.method_version = s.method_version.empty() ? annotations::method_version(d.p.at("kinds").at(s.kind).dump()) : s.method_version;
  c.confidence = s.confidence;
  c.origin = s.by == "user" ? "user" : (s.by == "model" ? "model" : "recorded");
  c.qualifier = s.qualifier;
  c.cost = s.cost;
  c.extra = Json{{"signal", s.kind}};
  LOOM_TRY_ASSIGN(auto w, annotations::write(db, pol, c));
  // Where the user excepts something, that is a segment too.
  for (const auto& [from, to] : s.exception_spans) {
    annotations::Claim e = c;
    e.category = "exception";
    e.start_char = from;
    e.end_char = to;
    e.extra = Json{{"signal", s.kind}};
    LOOM_TRY_ASSIGN(auto we, annotations::write(db, pol, e));
    (void)we;
  }
  return w.id;
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

// The analyzer wrote annotations (or a receipt that it read the message): read the layer again, which may now name signals the heuristics did not.
void Guardian::Impl::on_annotations(const Json& e) {
  try {
    const std::string id = json::get_string(e, "message_id");
    if (id.empty()) return;
    auto m = rt.db().get_msg(id);
    if (!m || !*m || (*m)->role != "user") return;
    auto r = observe(id, Json{{"post", false}});
    if (!r) {
      log::write(log::Level::Debug, kLog, "observe after annotations failed: " + r.error().message);
      return;
    }
    auto d = data();
    if (d && method(*d).name != "off") (void)post_pending(*d, (*m)->conv_id);
  } catch (const std::exception& ex) {
    log::write(log::Level::Warning, kLog, std::string("annotations handler: ") + ex.what());
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
  impl_->sub_annotations = ScopedSubscription(impl_->rt.bus(), impl_->rt.bus().on(events::kAnnotationsWritten, [this](std::string_view, const Json& e) { impl_->on_annotations(e); }));
}

void Guardian::stop() {
  impl_->sub.reset();
  impl_->sub_annotations.reset();
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
      // The user declined the guardian's own proposal: not asked again until the evidence doubles. A "no" also takes back the weak merges that
      // made it up (occurrences joined by a method that was not sure): "that is not what I meant" separates them, and nothing is lost.
      const std::string hyp = json::get_string(proposal.value("args", Json::object()), "hypothesis");
      auto n = db.get_node(hyp);
      if (n && *n) {
        if (json::get_bool(d->p.at("lifecycle"), "unmerge_on_no", true))
          (void)impl_->split_merged(*d, hyp, json::get_number(d->p.at("matching"), "unmerge_on_no_below", 0.95), "");
        auto again = db.get_node(hyp);
        Json meta = (*again)->metadata;
        meta["status"] = "rejected";
        meta["rejected_weight"] = json::get_number(meta, "weight", 0);
        meta["rejected_at"] = timeutil::utc_now_iso();
        meta["proposal_message_id"] = proposal_message_id;
        (void)save_meta(db, hyp, meta);
        for (const auto& sib : meta.value("siblings", Json::array())) {  // the same thing asked elsewhere is not asked again either
          auto sn = db.get_node(sib.value("id", ""));
          if (!sn || !*sn) continue;
          Json sm = (*sn)->metadata;
          sm["status"] = "rejected";
          sm["rejected_weight"] = json::get_number(sm, "weight", 0);
          (void)save_meta(db, (*sn)->id, sm);
        }
      }
      return;
    }
    if (json::get_string(proposal, "action") == "guardian.undo") {
      // He keeps the rule: not asked again until the evidence against it doubles / the next review period.
      const std::string hyp = json::get_string(proposal.value("args", Json::object()), "hypothesis");
      if (auto n = db.get_node(hyp); n && *n) {
        Json meta = (*n)->metadata;
        meta["disable_declined_weight"] = json::get_number(meta, "disable_weight", json::get_number(meta, "disable_proposed_weight", 1.0));
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
