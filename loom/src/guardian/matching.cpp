// How occurrences are recognised as the same thing (a swappable method), what a hypothesis is called (normalized, unnamed until worth asking),
// and the two model calls this needs (match, name). All of it is data in the profile guardian (matching, naming).
#include <algorithm>
#include <cmath>
#include <set>

#include "impl.h"
#include "loom/port_profile.h"
#include "loom/annotation_layer.h"
#include "loom/log.h"
#include "loom/use_model.h"
#include "loom/util/utf8.h"
#include "text.h"

namespace loom::guardian {
using namespace detail;

namespace detail {
namespace {

std::vector<std::string> phrases_of(const Json& by_lang) {
  std::vector<std::string> out;
  if (by_lang.is_object())
    for (auto it = by_lang.begin(); it != by_lang.end(); ++it)
      for (const auto& p : text::strings_of(&it.value())) out.push_back(p);
  return out;
}

const Json& matching_cfg(const Data& d) { return d.p.at("matching"); }

}  // namespace

namespace {
// Concept hits of a text: {id, negated, first token, last token}. A concept is negated when a negation stands before it in its clause.
struct ConceptHit { std::string id; bool negated; std::size_t first, last; };
std::vector<ConceptHit> concept_hits(const Data& d, const std::u32string& cps, const std::vector<text::Tok>& toks) {
  std::vector<ConceptHit> out;
  const Json& concepts = matching_cfg(d).at("concepts");
  for (auto it = concepts.begin(); it != concepts.end(); ++it) {
    const Json phrases = it.value().value("phrases", Json::object());
    for (const auto& hit : text::find(toks, text::compile(phrases_of(phrases), d.fold))) {
      const auto [from, to] = text::clause(cps, toks, hit.first, hit.last);
      (void)to;
      const std::string before = utf8::encode(std::u32string_view(cps).substr(from, toks[hit.first].start > from ? toks[hit.first].start - from : 0));
      out.push_back({it.key(), negative(d, before), hit.first, hit.last});
    }
  }
  return out;
}
}  // namespace

std::vector<std::string> concepts_in(const Data& d, const std::string& txt) {
  const auto cps = utf8::decode(txt);
  const auto toks = text::tokenize(cps, d.fold);
  std::set<std::string> out;
  for (const auto& h : concept_hits(d, cps, toks)) out.insert((h.negated ? "!" : "") + h.id);
  return {out.begin(), out.end()};
}

std::string concept_statement(const Data& d, const std::vector<std::string>& ids, const std::string& lang) {
  std::string out;
  const Json& concepts = matching_cfg(d).at("concepts");
  for (const auto& raw : ids) {
    const bool neg = !raw.empty() && raw[0] == '!';
    const Json* c = json::find(concepts, neg ? raw.substr(1) : raw);
    if (!c) continue;
    const Json st = c->value(neg ? "negative_statement" : "statement", Json::object());
    std::string s = json::get_string(st, lang, json::get_string(st, "en"));
    if (s.empty()) continue;
    out += (out.empty() ? "" : " ") + s;
  }
  return out;
}

std::vector<std::string> claim_tokens(const Data& d, const std::string& txt) {
  // The content of a correction is what it asks for, not the words that make it a correction: the markers of every kind are not content, and a
  // concept (brevity ...) replaces the words that say it, so that "shorter" and "too long" are the same token.
  const auto cps = utf8::decode(txt);
  const auto toks = text::tokenize(cps, d.fold);
  std::vector<bool> drop(toks.size(), false);
  const Json& kinds = d.p.at("kinds");
  for (auto it = kinds.begin(); it != kinds.end(); ++it) {
    const Json detect_copy = it.value().value("detect", Json::object());
    const Json* h = json::find(detect_copy, "heuristic");
    if (!h) continue;
    const Json markers = h->value("markers", Json::object());
    for (const auto& hit : text::find(toks, text::compile(phrases_of(markers), d.fold)))
      for (std::size_t k = hit.first; k <= hit.last; ++k) drop[k] = true;
  }
  std::set<std::string> out;
  for (const auto& h : concept_hits(d, cps, toks)) {
    out.insert(std::string("@") + (h.negated ? "!" : "") + h.id);
    for (std::size_t k = h.first; k <= h.last; ++k) drop[k] = true;
  }
  std::set<std::string> stop;
  for (const std::string lang : {"pl", "en"}) {
    const Json sw = d.p.value("tokens", Json::object()).value("stopwords", Json::object());
    for (const auto& w : text::strings_of(json::find(sw, lang))) stop.insert(w);
  }
  const std::size_t min_len = static_cast<std::size_t>(json::get_int(d.p.value("weighting", Json::object()), "min_token_len", 3));
  for (std::size_t i = 0; i < toks.size(); ++i)
    if (!drop[i] && utf8::length(toks[i].folded) >= min_len && !stop.count(toks[i].folded)) out.insert(toks[i].folded);
  return {out.begin(), out.end()};
}

double overlap_of(const std::vector<std::string>& a, const std::vector<std::string>& b, std::size_t* common_out) {
  if (a.empty() || b.empty()) {
    if (common_out) *common_out = 0;
    return 0;
  }
  std::size_t common = 0;
  const std::set<std::string> sb(b.begin(), b.end());
  for (const auto& x : a) common += sb.count(x);
  if (common_out) *common_out = common;
  return static_cast<double>(common) / static_cast<double>(std::min(a.size(), b.size()));
}

std::string claim_text_of(const Json& hyp_meta) {
  const Json claim = hyp_meta.value("claim", Json::object());
  std::string t = json::get_string(claim, "text");
  if (!t.empty()) return t;
  const Json quotes = claim.value("quotes", Json::array());
  if (quotes.is_array() && !quotes.empty()) return quotes.back().get<std::string>();
  return {};
}

}  // namespace detail

// ── the model: one call, JSON out ─────────────────────────────────────────────────────────────

annotations::ModelChoice Guardian::Impl::choice_for(const Data& d, const std::string& conv_id) {
  // Owner decision (2026-10-15): "never the conversation's model unless the user set it by hand, never inherited". The choice is a policy
  // (models.policies.guardian): guardian_model, the ports role guardian, the role analysis.fast, else NO model. The conversation's model is read
  // only for a relative_to step that the user wrote himself.
  (void)d;
  std::string conversation_model;
  if (!conv_id.empty())
    if (auto msgs = rt.db().get_msgs(conv_id))
      for (auto it = msgs->rbegin(); it != msgs->rend() && conversation_model.empty(); ++it)
        if (it->role == "assistant" && it->model && !it->model->empty() && it->model->rfind("loom:", 0) != 0) conversation_model = *it->model;
  return annotations::resolve_model(rt.config(), rt.paths().root, "guardian", conv_id, conversation_model);
}

std::string Guardian::Impl::model_for(const Data& d, const std::string& conv_id) { return choice_for(d, conv_id).model; }

Result<Json> Guardian::Impl::call_json(const Data& d, const std::string& conv_id, const Json& call, const Json& vars, Json& issues, Json& cost) {
  const std::string model = model_for(d, conv_id);
  if (model.empty()) {
    issues.push_back("no analysis model set: the guardian reads with word lists only (set guardian_model, or the ports role guardian / analysis.fast)");
    return Json(nullptr);
  }
  LOOM_TRY_ASSIGN(std::string system, render_profile_template(json::get_string(call, "system"), vars));
  LOOM_TRY_ASSIGN(std::string user, render_profile_template(json::get_string(call, "user"), vars));
  llm::ModelRequest req;
  req.kind = json::get_string(call, "kind", "guardian.call");
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
    return Json(nullptr);
  }
  const llm::VariantResult& r = outcome->last();
  if (r.state == llm::State::Held) {
    issues.push_back("the usage guard holds the guardian's model call (" + json::get_string(r.hold, "status") + "); nothing was sent");
    return Json(nullptr);
  }
  if (!r.ok()) {
    issues.push_back(r.error ? r.error->message : std::string("the model call failed"));
    return Json(nullptr);
  }
  cost = Json{{"calls", 1}, {"input_bytes", system.size() + user.size()}, {"output_bytes", r.text.size()}, {"receipt", r.operation_id}, {"model", r.model}};
  Json answer = nullptr;
  for (const auto& part : r.outputs)
    if (part.kind == "json") answer = part.value;
  if (!answer.is_object()) {
    issues.push_back("the model did not return a JSON object");
    return Json(nullptr);
  }
  return answer;
}

// ── matching ──────────────────────────────────────────────────────────────────────────────────

Result<Guardian::Impl::Match> Guardian::Impl::find_match(const Data& d, const Message& m, const std::string& kind, const std::vector<std::string>& tokens, const std::string& text,
                                                          bool neg, const std::vector<Node>& pool) {
  Match out;
  const Json& cfg = matching_cfg(d);
  const std::string name = choose(rt.config(), cfg);
  const auto methods = text::strings_of(json::find(cfg.at("presets").at(name), "methods"));
  const double join_min = json::get_number(cfg, "join_min", 0.8);
  const double overlap_min = json::get_number(d.p.at("weighting"), "overlap_min", 0.5);
  const std::size_t common_min = static_cast<std::size_t>(json::get_int(cfg, "common_min", 1));
  out.trace = Json{{"preset", name}};
  struct Cand { const Node* node; double score; std::size_t common; };
  std::vector<Cand> cands;
  for (const auto& h : pool) {
    if (json::get_string(h.metadata, "signal") != kind || json::get_string(h.metadata, "status") == "undone") continue;
    if (h.metadata.value("negative", false) != neg) continue;
    std::vector<std::string> ht;
    for (const auto& t : h.metadata.value("tokens", Json::array())) ht.push_back(t.get<std::string>());
    std::size_t common = 0;
    const double s = overlap_of(tokens, ht, &common);
    cands.push_back({&h, s, common});
  }
  // Prefer the hypothesis of this conversation at equal score: evidence from elsewhere only supports it.
  auto better = [&](const Cand& a, const Cand& b) {
    if (std::fabs(a.score - b.score) > 1e-9) return a.score > b.score;
    return (json::get_string(a.node->metadata, "conv_id") == m.conv_id) > (json::get_string(b.node->metadata, "conv_id") == m.conv_id);
  };
  std::sort(cands.begin(), cands.end(), better);
  auto take = [&](const Cand& c, const std::string& method, double conf, bool strict) {
    out.target = *c.node;
    out.confidence = conf;
    out.method = method;
    out.strict = strict;
  };
  if (!cands.empty() && cands.front().score >= join_min && cands.front().common >= common_min && json::get_string(cands.front().node->metadata, "conv_id") == m.conv_id) {
    take(cands.front(), "overlap", cands.front().score, true);
    return out;
  }
  if (std::find(methods.begin(), methods.end(), "overlap") != methods.end())
    for (const auto& c : cands)
      if (c.score >= overlap_min && c.common >= common_min) {
        take(c, "overlap", c.score, false);
        return out;
      }
  if (std::find(methods.begin(), methods.end(), "embedding") != methods.end() && !cands.empty()) {
    SelectorEngine sel = rt.make_selector();
    if (sel.active_tier() == "embedding") {
      std::vector<std::string> texts, ids;
      for (const auto& c : cands) {
        texts.push_back(claim_text_of(c.node->metadata));
        ids.push_back(c.node->id);
      }
      if (sel.index(texts, ids)) {
        const auto hits = sel.search(text, 1);
        if (!hits.empty() && hits.front().score >= json::get_number(cfg, "embedding_min", 0.75))
          for (const auto& c : cands)
            if (c.node->id == hits.front().id) {
              take(c, "embedding", hits.front().score, false);
              return out;
            }
      }
    } else {
      out.trace["embedding"] = "no embedding tier is available; the overlap method decided";
    }
  }
  if (std::find(methods.begin(), methods.end(), "model") != methods.end() && !cands.empty()) {
    const Json call = cfg.value("model_call", Json::object());
    const std::size_t max_c = static_cast<std::size_t>(json::get_int(call, "max_candidates", 2));
    Json issues = Json::array();
    for (std::size_t i = 0; i < cands.size() && i < max_c; ++i) {
      Json cost;
      const std::string other = claim_text_of(cands[i].node->metadata);
      auto answer = call_json(d, m.conv_id, call, Json{{"a", clip_to(other, 400)}, {"b", clip_to(text, 400)}}, issues, cost);
      if (!answer || answer->is_null()) continue;
      const double conf = json::get_number(*answer, "confidence", 0);
      if (json::get_bool(*answer, "same", false) && conf >= json::get_number(call, "min_confidence", 0.7)) {
        take(cands[i], "model", conf, false);
        out.trace["cost"] = cost;
        return out;
      }
    }
    if (!issues.empty()) out.trace["issues"] = issues;
  }
  return out;
}

// ── naming ────────────────────────────────────────────────────────────────────────────────────

bool Guardian::Impl::naming_allowed(const Data& d) {
  const auto allowed = text::strings_of(json::find(d.p.at("naming"), "allow_methods"));
  return std::find(allowed.begin(), allowed.end(), method(d).name) != allowed.end();
}

// A hypothesis has a normalized content, kept apart from its evidence. Order of authority: what the user stated himself, a name given once by a
// model (kept), the concepts the evidence agrees on (local, free), else it stays unnamed and is shown by its quotes.
void Guardian::Impl::refresh_claim(const Data& d, Json& meta) {
  Json claim = meta.value("claim", Json::object());
  const std::string source = json::get_string(claim, "source");
  Json quotes = Json::array();
  std::vector<std::vector<std::string>> concept_sets;
  for (const auto& o : meta.value("occurrences", Json::array())) {
    if (o.value("undone", false)) continue;
    const std::string q = o.value("quote", "");
    if (!q.empty()) quotes.push_back(q);
    concept_sets.push_back(o.value("concepts", Json::array()).get<std::vector<std::string>>());
  }
  claim["quotes"] = quotes;
  if (source == "user" || source == "model" || source == "structural" || source == "stated") {
    meta["claim"] = claim;
    return;
  }
  // The concepts every occurrence agrees on (at least `min_agreement` of them, and at least two occurrences unless there is only one).
  std::map<std::string, int> seen;
  for (const auto& set : concept_sets)
    for (const auto& c : set) ++seen[c];
  std::vector<std::string> agreed;
  const double need = json::get_number(d.p.at("naming"), "concept_agreement", 0.6);
  for (const auto& [c, n] : seen)
    if (!concept_sets.empty() && static_cast<double>(n) / static_cast<double>(concept_sets.size()) >= need) agreed.push_back(c);
  const std::string lang = json::get_string(meta, "lang", "en");
  const std::string statement = concept_statement(d, agreed, lang);
  if (!statement.empty()) {
    claim["text"] = statement;
    claim["source"] = "concept";
    claim["named"] = true;
    claim["concepts"] = agreed;
  } else if (source == "stated") {
    // keep
  } else {
    claim["text"] = "";
    claim["source"] = "";
    claim["named"] = false;
  }
  meta["claim"] = claim;
}

Status Guardian::Impl::name_with_model(const Data& d, Node& hyp, Json& issues) {
  Json meta = hyp.metadata;
  Json claim = meta.value("claim", Json::object());
  if (json::get_bool(claim, "named", false) || json::get_bool(claim, "naming_tried", false)) return ok_status();
  const Json call = d.p.at("naming").at("model_call");
  std::string quotes;
  std::size_t n = 0;
  const std::size_t max_quotes = static_cast<std::size_t>(json::get_int(call, "max_quotes", 5));
  for (const auto& q : claim.value("quotes", Json::array())) {
    if (n++ >= max_quotes) break;
    quotes += "- " + clip_to(q.get<std::string>(), 300) + "\n";
  }
  claim["naming_tried"] = true;  // one call per hypothesis, never repeated
  Json cost;
  auto answer = call_json(d, json::get_string(meta, "conv_id"), call, Json{{"quotes", quotes}}, issues, cost);
  if (answer && !answer->is_null()) {
    const std::string rule = clip_to(json::get_string(*answer, "rule"), 300);
    if (!rule.empty() && json::get_number(*answer, "confidence", 0) >= json::get_number(call, "min_confidence", 0.5)) {
      claim["text"] = rule;
      claim["source"] = "model";
      claim["named"] = true;
      claim["cost"] = cost;
    }
  }
  meta["claim"] = claim;
  NodePatch p;
  p.metadata = meta;
  if (json::get_bool(claim, "named", false)) p.content = json::get_string(claim, "text");
  hyp.metadata = meta;
  return rt.db().update_node(hyp.id, p);
}

}  // namespace loom::guardian
