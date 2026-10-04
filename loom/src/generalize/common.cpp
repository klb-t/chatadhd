// generalize: shared helpers — evidence index, text terms and cues,
// similarity, policy lookups, and the expression engine (conditions, values,
// Expected-Property predicates). See internal.h.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <regex>

#include "internal.h"
#include "loom/util/sha256.h"

namespace loom::generalize::detail {

using model::CheckState;
using model::Claim;
using model::EvidenceClass;

// ── dates ───────────────────────────────────────────────────────────
std::string day(std::string_view iso) { return std::string(iso.substr(0, std::min<std::size_t>(10, iso.size()))); }

namespace {
long days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long>(doe) - 719468;
}
std::optional<long> to_days(std::string_view iso) {
  if (iso.size() < 10) return std::nullopt;
  int y = 0;
  unsigned m = 0, d = 0;
  if (std::sscanf(std::string(iso.substr(0, 10)).c_str(), "%d-%u-%u", &y, &m, &d) != 3) return std::nullopt;
  if (m < 1 || m > 12 || d < 1 || d > 31) return std::nullopt;
  return days_from_civil(y, m, d);
}
}  // namespace

long days_between(std::string_view a, std::string_view b) {
  auto x = to_days(a), y = to_days(b);
  if (!x || !y) return 0;
  return *y - *x;
}

// ── Index ───────────────────────────────────────────────────────────
Index::Index(const Evidence& e) : ev(e) {
  for (const auto& x : ev.entities) entity.emplace(x.id, &x);
  for (const auto& o : ev.observations) {
    obs.emplace(o.id, &o);
    unit_obs[o.unit].push_back(&o);
    if (day(o.date) > corpus_end) corpus_end = day(o.date);
  }
  for (auto& [u, v] : unit_obs) {
    std::sort(v.begin(), v.end(), [](const model::Observation* a, const model::Observation* b) {
      if (a->ordinal != b->ordinal) return a->ordinal < b->ordinal;
      return a->id < b->id;
    });
  }
  for (const auto& c : ev.claims) {
    claim.emplace(c.id, &c);
    by_subject[c.subject].push_back(&c);
    for (const auto& s : c.assessment.support) {
      if (auto it = obs.find(s.observation); it != obs.end()) {
        subject_units[c.subject].insert(it->second->unit);
        if (observed_grade(c)) subject_obs[c.subject].insert(s.observation);
      }
    }
    if (c.predicate == "mentioned_in" && c.value.is_string()) subject_units[c.subject].insert(c.value.get<std::string>());
  }
  for (auto& [s, v] : by_subject) {
    std::sort(v.begin(), v.end(), [](const Claim* a, const Claim* b) { return a->id < b->id; });
  }
  for (const auto& d : ev.decisions) {
    decision.emplace(d.id, &d);
    if (day(d.date) > corpus_end) corpus_end = day(d.date);
  }
}

const Claim* Index::find_claim(std::string_view id) const {
  auto it = claim.find(id);
  return it == claim.end() ? nullptr : it->second;
}
const model::Entity* Index::find_entity(std::string_view id) const {
  auto it = entity.find(id);
  return it == entity.end() ? nullptr : it->second;
}
const model::Observation* Index::find_obs(std::string_view id) const {
  auto it = obs.find(id);
  return it == obs.end() ? nullptr : it->second;
}

std::string Index::claim_date(const Claim& c) const {
  if (!c.qualifiers.valid_from.empty()) return day(c.qualifiers.valid_from);
  std::string best;
  for (const auto& s : c.assessment.support) {
    if (auto* o = find_obs(s.observation); o && !o->date.empty()) {
      auto d = day(o->date);
      if (best.empty() || d < best) best = d;
    }
  }
  return best;
}

std::vector<std::string> Index::claim_units(const Claim& c) const {
  std::set<std::string> u;
  for (const auto& s : c.assessment.support) {
    if (auto* o = find_obs(s.observation)) u.insert(o->unit);
  }
  return {u.begin(), u.end()};
}

std::vector<const model::Observation*> Index::decision_obs(const model::Decision& d) const {
  std::vector<const model::Observation*> out;
  if (auto* c = find_claim(d.id)) {
    for (const auto& s : c->assessment.support) {
      if (auto* o = find_obs(s.observation)) out.push_back(o);
    }
  }
  std::sort(out.begin(), out.end(), [](auto* a, auto* b) { return a->id < b->id; });
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

Evidence slice(const Evidence& ev, std::string_view cut, bool before) {
  Index ix(ev);
  std::string c = day(cut);
  auto keep = [&](const std::string& date) {
    std::string d = day(date);
    if (d.empty()) return before;
    return before ? d <= c : d > c;
  };
  Evidence out;
  out.entities = ev.entities;
  for (const auto& o : ev.observations) {
    if (keep(o.date)) out.observations.push_back(o);
  }
  for (const auto& x : ev.claims) {
    if (keep(ix.claim_date(x))) out.claims.push_back(x);
  }
  for (const auto& d : ev.decisions) {
    if (!keep(d.date)) continue;
    auto dd = d;
    // A supersession that happens after the cut is not known before it.
    if (before && !dd.superseded_by.empty()) {
      auto it = ix.decision.find(dd.superseded_by);
      if (it != ix.decision.end() && !keep(it->second->date)) {
        dd.superseded_by.clear();
        dd.status = model::DecisionStatus::Active;
      }
    }
    out.decisions.push_back(std::move(dd));
  }
  for (const auto& a : ev.areas) {
    auto* o = ix.find_obs(a.observation);
    if (keep(o ? o->date : std::string())) out.areas.push_back(a);
  }
  for (const auto& p : ev.principles) {
    std::string first;
    for (const auto& id : p.evidence_for) {
      if (auto* o = ix.find_obs(id); o && !o->date.empty() && (first.empty() || day(o->date) < first)) first = day(o->date);
    }
    if (keep(first)) out.principles.push_back(p);
  }
  return out;
}

// ── text ────────────────────────────────────────────────────────────
std::vector<std::string> terms(const kb::Normalizer& norm, std::string_view text) {
  std::set<std::string> out;
  std::string key = norm.phrase_key(text, true);
  std::size_t i = 0;
  while (i < key.size()) {
    auto j = key.find(' ', i);
    if (j == std::string::npos) j = key.size();
    if (j > i) {
      std::string t = key.substr(i, j - i);
      if (t.size() >= 2) out.insert(std::move(t));
    }
    i = j + 1;
  }
  return {out.begin(), out.end()};
}

namespace {
bool has_word_char(std::string_view s) {
  for (unsigned char c : s) {
    if (std::isalnum(c) || c >= 0x80) return true;
  }
  return false;
}
bool starts_with(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }

bool cue_hit_tokens(const kb::Normalizer& norm, std::string_view folded_text, const std::vector<std::string>& toks,
                    std::string_view phrase) {
  std::string p = norm.fold(phrase);
  bool star = !p.empty() && p.back() == '*';
  if (star) p.pop_back();
  if (p.empty()) return false;
  // Symbolic cues ("≠", "->", "→"): substring.
  std::vector<std::string> pt = norm.tokens(p);
  if (pt.empty() || !has_word_char(p)) return folded_text.find(p) != std::string_view::npos;
  if (pt.size() > toks.size()) return false;
  for (std::size_t i = 0; i + pt.size() <= toks.size(); ++i) {
    bool ok = true;
    for (std::size_t k = 0; k < pt.size() && ok; ++k) {
      bool last = k + 1 == pt.size();
      ok = (last && star) ? starts_with(toks[i + k], pt[k]) : toks[i + k] == pt[k];
    }
    if (ok) return true;
  }
  return false;
}
}  // namespace

bool cue_hit(const kb::Normalizer& norm, std::string_view folded_text, std::string_view phrase) {
  return cue_hit_tokens(norm, folded_text, norm.tokens(folded_text), phrase);
}

FoldedText fold_text(const kb::Normalizer& norm, std::string_view text) {
  FoldedText t;
  t.folded = norm.fold(text);
  t.toks = norm.tokens(t.folded);
  return t;
}

PreparedCues::PreparedCues(const kb::Normalizer& norm, const Json& cls) {
  const Json* ph = json::find(cls, "phrases");
  if (!ph || !ph->is_array()) return;
  for (const auto& p : *ph) {
    std::string phrase = json::get_string(p, "p");
    if (phrase.empty()) continue;
    Phrase x;
    x.text = norm.fold(phrase);
    x.star = !x.text.empty() && x.text.back() == '*';
    if (x.star) x.text.pop_back();
    if (x.text.empty()) continue;  // never matches
    x.toks = norm.tokens(x.text);
    x.symbolic = x.toks.empty() || !has_word_char(x.text);
    x.w = json::get_number(p, "w", 1.0);
    phrases_.push_back(std::move(x));
  }
}

bool PreparedCues::hits(const Phrase& p, const FoldedText& t) const {
  if (p.symbolic) return t.folded.find(p.text) != std::string::npos;
  if (p.toks.size() > t.toks.size()) return false;
  for (std::size_t i = 0; i + p.toks.size() <= t.toks.size(); ++i) {
    bool ok = true;
    for (std::size_t k = 0; k < p.toks.size() && ok; ++k) {
      bool last = k + 1 == p.toks.size();
      ok = (last && p.star) ? starts_with(t.toks[i + k], p.toks[k]) : t.toks[i + k] == p.toks[k];
    }
    if (ok) return true;
  }
  return false;
}

double PreparedCues::score(const FoldedText& t) const {
  double s = 0.0;
  for (const auto& p : phrases_) {
    if (hits(p, t)) s += p.w;
  }
  return s;
}

void PreparedCues::hit_phrases(const FoldedText& t, std::set<std::size_t>& seen) const {
  for (std::size_t i = 0; i < phrases_.size(); ++i) {
    if (hits(phrases_[i], t)) seen.insert(i);
  }
}

double cue_score(const kb::Normalizer& norm, std::string_view text, const Json& cls) {
  return PreparedCues(norm, cls).score(fold_text(norm, text));
}

const Json& cue_class(const kb::Pack& pack, std::string_view name) {
  static const Json kNull;
  const Json& cues = pack.lexicon("cues");
  const Json* classes = json::find(cues, "classes");
  if (!classes || !classes->is_object()) return kNull;
  auto it = classes->find(std::string(name));
  return it == classes->end() ? kNull : *it;
}

void TermWeights::add(const std::vector<std::string>& doc) {
  ++n_;
  for (const auto& t : doc) ++df_[t];
}
double TermWeights::w(const std::string& t) const {
  auto it = df_.find(t);
  double df = it == df_.end() ? 1.0 : static_cast<double>(it->second);
  double n = static_cast<double>(std::max<std::size_t>(n_, 1));
  return std::log(1.0 + n / df);
}

std::vector<std::string> set_union(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  std::vector<std::string> out;
  std::set_union(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
  return out;
}
std::vector<std::string> set_minus(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  std::vector<std::string> out;
  std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
  return out;
}

// ── policy ──────────────────────────────────────────────────────────
double threshold(const kb::Pack& pack, std::string_view section, std::string_view key, double fallback) {
  const Json& t = pack.policy("thresholds");
  const Json* s = json::find(t, section);
  if (!s) return fallback;
  const Json* v = json::find(*s, key);
  return v && v->is_number() ? v->get<double>() : fallback;
}

double clamp01(double v) {
  if (!std::isfinite(v)) return 0.0;
  return std::min(1.0, std::max(0.0, v));
}

double calibrate(const kb::Pack& pack, EvidenceClass e, double raw) {
  raw = clamp01(raw);
  const Json* iso = json::find(pack.policy("calibration"), "isotonic");
  const Json* pts = iso ? json::find(*iso, kb::to_string(e)) : nullptr;
  if (!pts || !pts->is_array() || pts->size() < 2) return raw;
  double px = 0, py = 0;
  bool first = true;
  for (const auto& p : *pts) {
    if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) continue;
    double x = p[0].get<double>(), y = p[1].get<double>();
    if (first) {
      if (raw <= x) return clamp01(y);
      first = false;
    } else if (raw <= x) {
      double t = x > px ? (raw - px) / (x - px) : 1.0;
      return clamp01(py + t * (y - py));
    }
    px = x;
    py = y;
  }
  return clamp01(py);
}

// ── claims ──────────────────────────────────────────────────────────
bool usable(const Claim& c) {
  auto e = c.assessment.evidence;
  return e != EvidenceClass::Absent && e != EvidenceClass::Extrapolated && c.assessment.status != model::ClaimStatus::Rejected;
}
bool observed_grade(const Claim& c) {
  auto e = c.assessment.evidence;
  return (e == EvidenceClass::Observed || e == EvidenceClass::Derived || e == EvidenceClass::User) &&
         c.assessment.status != model::ClaimStatus::Rejected;
}
bool transferred(const Claim& c) { return c.assessment.derivation && !c.assessment.derivation->morphism.empty(); }

std::string value_key(const kb::Normalizer& norm, const Json& v) {
  if (v.is_string()) {
    std::string k = norm.phrase_key(v.get<std::string>(), true);
    return k.empty() ? norm.fold(v.get<std::string>()) : k;
  }
  if (v.is_null()) return "";
  return json::canonical(v);
}
std::string value_key(const kb::Normalizer& norm, const Index& ix, const Claim& c) {
  if (!c.object.empty()) {
    if (auto* e = ix.find_entity(c.object)) return e->canonical_key.empty() ? e->id : e->canonical_key;
    return c.object;
  }
  return value_key(norm, c.value);
}
std::string value_text(const Index& ix, const Claim& c) {
  if (!c.object.empty()) {
    if (auto* e = ix.find_entity(c.object)) return e->label.empty() ? e->canonical_key : e->label;
    return c.object;
  }
  if (c.value.is_string()) return c.value.get<std::string>();
  return c.value.is_null() ? std::string() : json::dump(c.value);
}

Claim finish(Claim c) {
  c.id = Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
  return c;
}

void dedupe(std::vector<Claim>& v) {
  std::stable_sort(v.begin(), v.end(), [](const Claim& a, const Claim& b) { return a.id < b.id; });
  v.erase(std::unique(v.begin(), v.end(), [](const Claim& a, const Claim& b) { return a.id == b.id; }), v.end());
}

std::string hash_json(const Json& j) { return Sha256::hex(json::canonical(j)); }

std::shared_ptr<const kb::Pack> builtin_pack() {
  static std::once_flag once;
  static std::shared_ptr<const kb::Pack> pack;
  std::call_once(once, [] {
    if (auto p = kb::Pack::load_builtin()) pack = *p;
  });
  return pack;
}

// ── slots ───────────────────────────────────────────────────────────
std::vector<const Claim*> slot_claims(const Match& m, std::string_view slot, const Index& ix) {
  std::vector<const Claim*> out;
  for (const auto& sv : m.instance.slots) {
    if (sv.slot != slot) continue;
    const Claim* c = nullptr;
    for (const auto& x : m.claims) {
      if (x.id == sv.claim) {
        c = &x;
        break;
      }
    }
    if (!c) c = ix.find_claim(sv.claim);
    if (c && usable(*c)) out.push_back(c);
  }
  return out;
}

const model::DomainKind* slot_kind(const kb::Pack& pack, const Match& m, std::string_view slot, model::ProjectKind* pk_buf,
                                   model::Facet* facet_buf) {
  auto dot = slot.find('.');
  if (dot != std::string_view::npos) {
    auto f = model::facet(pack, slot.substr(0, dot));
    if (!f) return nullptr;
    *facet_buf = std::move(*f);
    return facet_buf->domain_kind(slot.substr(dot + 1));
  }
  if (m.instance.paradigm_kind != model::ParadigmKind::ProjectKind) return nullptr;
  auto pk = model::project_kind(pack, m.instance.paradigm);
  if (!pk) return nullptr;
  *pk_buf = std::move(*pk);
  return pk_buf->domain_kind(slot);
}

std::set<std::string> active_principles(const kb::Pack& pack, const Evidence& ev, const model::PriorFilter& priors) {
  std::set<std::string> out;
  if (auto seeds = model::principles(pack, priors)) {
    for (const auto& p : *seeds) {
      if (p.validation != model::ValidationStatus::Rejected && p.validation != model::ValidationStatus::Retired) out.insert(p.id);
    }
  }
  for (const auto& p : ev.principles) {
    if (p.validation != model::ValidationStatus::Rejected && p.validation != model::ValidationStatus::Retired) out.insert(p.id);
  }
  return out;
}

// ── expressions ─────────────────────────────────────────────────────
namespace {


const Match* instance_of(const ExprCtx& ctx, std::string_view paradigm) {
  if (!ctx.matches) return nullptr;
  for (const auto& m : *ctx.matches) {
    if (m.instance.subject == ctx.subject && m.instance.paradigm == paradigm) return &m;
  }
  return nullptr;
}

// Resolves "$slot:<kind>" / "$slot:<paradigm>.<kind>" to (instance, slot name).
std::pair<const Match*, std::string> slot_ref(const ExprCtx& ctx, std::string_view ref) {
  std::string_view name = ref.substr(6);
  auto dot = name.find('.');
  if (dot == std::string_view::npos) return {ctx.inst, std::string(name)};
  std::string_view par = name.substr(0, dot);
  if (ctx.inst) {
    const auto& f = ctx.inst->instance.facets;
    if (std::find(f.begin(), f.end(), par) != f.end()) return {ctx.inst, std::string(name)};
    if (ctx.inst->instance.paradigm == par) return {ctx.inst, std::string(name.substr(dot + 1))};
  }
  return {instance_of(ctx, par), std::string(name.substr(dot + 1))};
}

Json claims_values(const std::vector<const Claim*>& cs, const ExprCtx& ctx) {
  Json out = Json::array();
  std::set<std::string> seen;
  for (auto* c : cs) {
    std::string k = value_key(ctx.norm, ctx.ix, *c);
    if (ctx.used_claims) ctx.used_claims->push_back(c->id);
    if (!seen.insert(k).second) continue;
    out.push_back(value_text(ctx.ix, *c));
  }
  return out;
}

std::vector<const Claim*> query_claims(const ExprCtx& ctx, const Json& ref, bool observed_only) {
  std::vector<const Claim*> out;
  std::string subject = json::get_string(ref, "subject", ctx.subject);
  std::string predicate = json::get_string(ref, "predicate");
  std::string kind = json::get_string(ref, "entity_kind");
  std::string origin = json::get_string(ref, "origin");
  auto it = ctx.ix.by_subject.find(subject);
  if (it == ctx.ix.by_subject.end()) return out;
  for (auto* c : it->second) {
    if (c->predicate != predicate || c->id == ctx.self_claim) continue;
    if (observed_only ? !observed_grade(*c) : !usable(*c)) continue;
    if (!origin.empty() && model::to_string(c->assessment.origin) != origin) continue;
    if (!kind.empty() && !c->object.empty()) {
      auto* e = ctx.ix.find_entity(c->object);
      if (e && e->kind != kind) continue;
    }
    out.push_back(c);
  }
  return out;
}

Json rejected_values(const ExprCtx& ctx, std::string_view subject) {
  Json out = Json::array();
  std::set<std::string> seen;
  for (const auto& d : ctx.ix.ev.decisions) {
    if (d.subject != subject) continue;
    for (const auto& a : d.alternatives) {
      bool rejected = (d.chosen() && !a.chosen) || (a.chosen && d.status != model::DecisionStatus::Active);
      if (!rejected) continue;
      std::string label = a.label.empty() && a.value.is_string() ? a.value.get<std::string>() : a.label;
      if (label.empty()) continue;
      if (seen.insert(value_key(ctx.norm, Json(label))).second) out.push_back(label);
    }
  }
  return out;
}

std::string max_version_of(const Json& list) {
  std::string best;
  for (const auto& v : list) {
    if (!v.is_string()) continue;
    std::string n = kb::normalize_version(v.get<std::string>());
    if (n.empty()) continue;
    if (best.empty() || kb::compare_versions(n, best) > 0) best = n;
  }
  return best;
}

Json lexicon_members(const kb::Pack& pack, const kb::Normalizer& norm, std::string_view cls) {
  Json out = Json::array();
  const Json& g = pack.lexicon("gazetteer");
  const Json* entries = json::find(g, "entries");
  if (!entries || !entries->is_array()) return out;
  std::string prefix = std::string(cls) + ".";
  for (const auto& e : *entries) {
    std::string c = json::get_string(e, "class");
    if (c != cls && !starts_with(c, prefix)) continue;
    std::set<std::string> keys;
    keys.insert(value_key(norm, Json(json::get_string(e, "id"))));
    if (const Json* l = json::find(e, "labels"); l && l->is_object()) {
      for (const auto& [k, v] : l->items()) {
        if (v.is_string()) keys.insert(value_key(norm, v));
      }
    }
    if (const Json* f = json::find(e, "forms"); f && f->is_array()) {
      for (const auto& v : *f) {
        if (v.is_string()) keys.insert(value_key(norm, v));
      }
    }
    for (const auto& k : keys) {
      if (!k.empty()) out.push_back(k);
    }
  }
  return out;
}

Json enum_members(const kb::Pack& pack, std::string_view name) {
  const Json* en = json::find(pack.types(), "enums");
  if (!en) return Json::array();
  const Json* v = json::find(*en, name);
  return v && v->is_array() ? *v : Json::array();
}

bool list_contains(const ExprCtx& ctx, const Json& list, const Json& v) {
  std::string k = value_key(ctx.norm, v);
  for (const auto& x : list) {
    if (value_key(ctx.norm, x) == k) return true;
  }
  return false;
}

Json as_list(const Json& v) {
  if (v.is_array()) return v;
  if (v.is_null()) return Json::array();
  return Json::array({v});
}

CheckState tri(bool b) { return b ? CheckState::Holds : CheckState::Violated; }

}  // namespace

Json bind(const Json& expr, const ExprCtx& ctx) {
  if (expr.is_array()) {
    Json out = Json::array();
    for (const auto& x : expr) out.push_back(bind(x, ctx));
    return out;
  }
  if (expr.is_object()) {
    if (json::find(expr, "ref") || json::find(expr, "members")) return expr;
    Json out = expr;
    std::string op = json::get_string(expr, "op");
    if (const Json* a = json::find(expr, "args"); a && a->is_array()) {
      Json args = bind(*a, ctx);
      if (ctx.pack && (op == "in_class" || op == "in_enum") && !args.empty() && args[0].is_string() && args.size() == 1) {
        std::string name = args[0].get<std::string>();
        args.push_back(Json{{"members", op == "in_class" ? lexicon_members(*ctx.pack, ctx.norm, name) : enum_members(*ctx.pack, name)}});
      }
      out["args"] = std::move(args);
    }
    return out;
  }
  if (!expr.is_string()) return expr;
  const std::string& s = expr.get_ref<const std::string&>();
  if (s == "$subject") return Json{{"ref", "subject"}, {"subject", ctx.subject}};
  if (s == "$rejected_values") return Json{{"ref", "rejected"}, {"subject", ctx.subject}};
  if (s == "$git_max_version") return Json{{"ref", "git_max_version"}, {"subject", ctx.subject}};
  if (s == "$corpus_end_date") return Json{{"members", Json::array({ctx.ix.corpus_end})}};
  if (starts_with(s, "$temporal:versions")) return Json{{"ref", "versions"}, {"subject", ctx.subject}};
  if (starts_with(s, "$temporal:current_version")) return Json{{"ref", "current_version"}, {"subject", ctx.subject}};
  if (starts_with(s, "$slot:") && ctx.pack) {
    auto [m, slot] = slot_ref(ctx, s);
    if (!m) return expr;
    model::ProjectKind pk;
    model::Facet fc;
    const model::DomainKind* dk = slot_kind(*ctx.pack, *m, slot, &pk, &fc);
    if (!dk || dk->relation.empty()) return expr;
    return Json{{"ref", "claims"}, {"subject", m->instance.subject}, {"predicate", dk->relation}, {"entity_kind", dk->entity_kind}};
  }
  if (starts_with(s, "$analog:")) {
    Json vals = resolve_list(expr, ctx);
    return Json{{"members", vals}};
  }
  return expr;
}

Json resolve_list(const Json& arg, const ExprCtx& ctx) {
  if (arg.is_object()) {
    if (const Json* m = json::find(arg, "members")) return as_list(*m);
    std::string ref = json::get_string(arg, "ref");
    if (ref == "claims") return claims_values(query_claims(ctx, arg, true), ctx);
    if (ref == "rejected") return rejected_values(ctx, json::get_string(arg, "subject", ctx.subject));
    if (ref == "subject") return Json::array({json::get_string(arg, "subject", ctx.subject)});
    if (ref == "versions" || ref == "git_max_version" || ref == "current_version") {
      Json q{{"subject", json::get_string(arg, "subject", ctx.subject)}, {"predicate", "has_version"}};
      if (ref == "git_max_version") q["origin"] = "repo";
      Json vals = claims_values(query_claims(ctx, q, true), ctx);
      if (ref == "versions") return vals;
      std::string mx = max_version_of(vals);
      return mx.empty() ? Json::array() : Json::array({mx});
    }
    if (json::find(arg, "op")) return as_list(eval_value(arg, ctx).values);
    return Json::array();
  }
  if (arg.is_string()) {
    const std::string& s = arg.get_ref<const std::string&>();
    if (s == "$value") return as_list(ctx.value);
    if (starts_with(s, "$slot:")) {
      if (ctx.inst || ctx.matches) {
        auto [m, slot] = slot_ref(ctx, s);
        if (!m) return Json::array();
        return claims_values(slot_claims(*m, slot, ctx.ix), ctx);
      }
      return Json::array();
    }
    if (starts_with(s, "$analog:")) {
      // Observed values of the same slot in instances analogous to this one
      // (same paradigm, another subject), best score first; depth 1.
      Json out = Json::array();
      if (!ctx.inst || !ctx.matches) return out;
      std::string slot = s.substr(8);
      std::vector<const Match*> others;
      for (const auto& m : *ctx.matches) {
        if (&m != ctx.inst && m.instance.paradigm == ctx.inst->instance.paradigm && m.instance.subject != ctx.subject) others.push_back(&m);
      }
      std::sort(others.begin(), others.end(), [](auto* a, auto* b) {
        if (a->score != b->score) return a->score > b->score;
        return a->instance.id < b->instance.id;
      });
      std::set<std::string> seen;
      for (auto* m : others) {
        for (auto* c : slot_claims(*m, slot, ctx.ix)) {
          if (!observed_grade(*c)) continue;
          if (seen.insert(value_key(ctx.norm, ctx.ix, *c)).second) out.push_back(value_text(ctx.ix, *c));
        }
      }
      return out;
    }
    if (s.size() > 1 && s[0] == '$') {
      Json b = bind(arg, ctx);
      if (b != arg) return resolve_list(b, ctx);
      return Json::array();
    }
  }
  return as_list(arg);
}

CheckState eval_pred(const Json& expr, const ExprCtx& ctx) {
  if (!expr.is_object()) return CheckState::Pending;
  std::string op = json::get_string(expr, "op");
  Json args = json::find(expr, "args") ? expr["args"] : Json::array();
  if (!args.is_array()) args = Json::array({args});
  auto arg = [&](std::size_t i) -> Json { return i < args.size() ? args[i] : Json(); };
  auto val = [&]() { return ctx.value; };
  if (op == "all" || op == "any") {
    bool any_hold = false, any_viol = false, any_pend = false;
    for (const auto& a : args) {
      auto s = eval_pred(a, ctx);
      any_hold |= s == CheckState::Holds;
      any_viol |= s == CheckState::Violated;
      any_pend |= s == CheckState::Pending || s == CheckState::NotApplicable;
    }
    if (op == "all") return any_viol ? CheckState::Violated : any_pend ? CheckState::Pending : CheckState::Holds;
    return any_hold ? CheckState::Holds : any_pend ? CheckState::Pending : CheckState::Violated;
  }
  if (op == "not") {
    auto s = eval_pred(arg(0), ctx);
    return s == CheckState::Holds ? CheckState::Violated : s == CheckState::Violated ? CheckState::Holds : s;
  }
  if (op == "nonempty") return resolve_list(arg(0), ctx).empty() ? CheckState::Pending : CheckState::Holds;
  if (op == "in_enum" || op == "in_class") {
    Json members = args.size() > 1 ? resolve_list(arg(1), ctx) : Json::array();
    if (members.empty()) return CheckState::Pending;
    Json vs = as_list(val());
    if (vs.empty()) return CheckState::Pending;
    for (const auto& v : vs) {
      if (!list_contains(ctx, members, v)) return CheckState::Violated;
    }
    return CheckState::Holds;
  }
  if (op == "not_in" || op == "subset_of") {
    Json a = resolve_list(arg(0), ctx), b = resolve_list(arg(1), ctx);
    if (a.empty()) return CheckState::Pending;
    if (op == "not_in") {
      for (const auto& v : a) {
        if (list_contains(ctx, b, v)) return CheckState::Violated;
      }
      return CheckState::Holds;
    }
    if (b.empty()) return CheckState::Pending;
    for (const auto& v : a) {
      if (!list_contains(ctx, b, v)) return CheckState::Violated;
    }
    return CheckState::Holds;
  }
  if (op == "version_gt" || op == "version_gte" || op == "version_between") {
    auto one = [&](std::size_t i) {
      Json l = resolve_list(arg(i), ctx);
      return l.empty() ? std::string() : max_version_of(l);
    };
    std::string a = one(0), b = one(1);
    if (a.empty() || b.empty()) return CheckState::Pending;
    int c = kb::compare_versions(a, b);
    if (op == "version_gt") return tri(c > 0);
    if (op == "version_gte") return tri(c >= 0);
    std::string hi = one(2);
    if (hi.empty()) return tri(c >= 0);
    return tri(c >= 0 && kb::compare_versions(a, hi) <= 0);
  }
  if (op == "date_between") {
    auto first = [&](const Json& a) {
      Json l = resolve_list(a, ctx);
      return l.empty() || !l[0].is_string() ? std::string() : day(l[0].get<std::string>());
    };
    std::string v, lo, hi;
    if (args.size() >= 3) {
      v = first(arg(0));
      lo = first(arg(1));
      hi = first(arg(2));
    } else {
      v = val().is_string() ? day(val().get<std::string>()) : std::string();
      lo = first(arg(0));
      hi = first(arg(1));
    }
    if (v.empty() || (lo.empty() && hi.empty())) return CheckState::Pending;
    if (!lo.empty() && v < lo) return CheckState::Violated;
    if (!hi.empty() && v > hi) return CheckState::Violated;
    return lo.empty() || hi.empty() ? CheckState::Pending : CheckState::Holds;
  }
  if (op == "matches") {
    Json vs = as_list(val());
    std::string pattern = arg(0).is_string() ? arg(0).get<std::string>() : std::string();
    if (vs.empty() || pattern.empty()) return CheckState::Pending;
    try {
      std::regex re(pattern);
      for (const auto& v : vs) {
        if (!v.is_string() || !std::regex_search(v.get<std::string>(), re)) return CheckState::Violated;
      }
      return CheckState::Holds;
    } catch (const std::exception&) {
      return CheckState::Pending;
    }
  }
  if (op == "consistent_with") {
    std::string pid = arg(0).is_string() ? arg(0).get<std::string>() : std::string();
    if (pid.empty()) return CheckState::Pending;
    auto it = ctx.ix.by_subject.find(ctx.subject);
    bool applies = false;
    if (it != ctx.ix.by_subject.end()) {
      for (auto* c : it->second) {
        if (!observed_grade(*c) || c->id == ctx.self_claim) continue;
        bool names = c->object == pid || (c->value.is_string() && c->value.get<std::string>() == pid);
        if (!names) continue;
        if (c->predicate == "violates") return CheckState::Violated;
        if (c->predicate == "applies_principle" || c->predicate == "states_principle") applies = true;
      }
    }
    if (auto* self = ctx.ix.find_claim(ctx.self_claim); self && !self->assessment.counter.empty()) return CheckState::Violated;
    return applies ? CheckState::Holds : CheckState::Pending;
  }
  if (op == "not_contradicted_by") {
    std::string rel = arg(0).is_string() ? arg(0).get<std::string>() : std::string();
    if (auto* self = ctx.ix.find_claim(ctx.self_claim)) {
      if (!self->assessment.counter.empty() || self->assessment.status == model::ClaimStatus::Rejected) return CheckState::Violated;
    }
    if (rel.empty()) return CheckState::Pending;
    auto cs = query_claims(ctx, Json{{"predicate", rel}}, true);
    if (cs.empty() || val().is_null()) return CheckState::Pending;
    Json vs = as_list(val());
    for (auto* c : cs) {
      std::string k = value_key(ctx.norm, ctx.ix, *c);
      for (const auto& v : vs) {
        if (value_key(ctx.norm, v) == k) return CheckState::Holds;
      }
    }
    // Only an explicit, observed statement about the same relation can
    // contradict; `rel` is a single-valued relation by the rule's contract.
    return CheckState::Violated;
  }
  if (op == "exists_symbol") {
    Json vs = as_list(arg(0).is_null() ? val() : Json(resolve_list(arg(0), ctx)));
    auto cs = query_claims(ctx, Json{{"predicate", "has_symbol"}}, true);
    if (cs.empty()) return CheckState::Pending;
    for (const auto& v : vs) {
      bool found = false;
      for (auto* c : cs) found |= value_key(ctx.norm, ctx.ix, *c) == value_key(ctx.norm, v);
      if (!found) return CheckState::Pending;
    }
    return CheckState::Holds;
  }
  // available_on, co_mentioned_with, ordered_by_date and anything unknown:
  // not decidable from the evidence alone -> stays pending (I9).
  return CheckState::Pending;
}

bool eval_cond(const Json& expr, const ExprCtx& ctx) {
  if (expr.is_boolean()) return expr.get<bool>();
  if (!expr.is_object()) return false;
  std::string op = json::get_string(expr, "op");
  Json args = json::find(expr, "args") ? expr["args"] : Json::array();
  if (!args.is_array()) args = Json::array({args});
  auto arg = [&](std::size_t i) -> Json { return i < args.size() ? args[i] : Json(); };
  if (op == "const") return json::truthy(arg(0));
  if (op == "all") {
    for (const auto& a : args) {
      if (!eval_cond(a, ctx)) return false;
    }
    return true;
  }
  if (op == "any") {
    for (const auto& a : args) {
      if (eval_cond(a, ctx)) return true;
    }
    return false;
  }
  if (op == "not") return !eval_cond(arg(0), ctx);
  if (op == "nonempty" || op == "empty") {
    const Json& a = arg(0);
    // An unresolvable reference never fires a rule.
    if (a.is_string() && starts_with(a.get<std::string>(), "$slot:")) {
      auto [m, slot] = slot_ref(ctx, a.get<std::string>());
      if (!m) return false;
    }
    if (a.is_string() && starts_with(a.get<std::string>(), "$field:")) return false;
    bool empty = resolve_list(a, ctx).empty();
    return op == "empty" ? empty : !empty;
  }
  if (op == "has_claim") {
    std::string rel = arg(0).is_string() ? arg(0).get<std::string>() : std::string();
    return !query_claims(ctx, Json{{"predicate", rel}}, false).empty();
  }
  if (op == "has_items") {
    std::string type = arg(0).is_string() ? arg(0).get<std::string>() : std::string();
    if (type == "decision") {
      for (const auto& d : ctx.ix.ev.decisions) {
        if (d.subject == ctx.subject) return true;
      }
    }
    return !query_claims(ctx, Json{{"predicate", "has_" + type}}, false).empty();
  }
  if (op == "principle_active") {
    std::string pid = arg(0).is_string() ? arg(0).get<std::string>() : std::string();
    return ctx.active_principles && ctx.active_principles->count(pid) > 0;
  }
  if (op == "eq") return resolve_list(arg(0), ctx) == resolve_list(arg(1), ctx);
  if (op == "gt") {
    Json a = resolve_list(arg(0), ctx), b = resolve_list(arg(1), ctx);
    if (a.size() != 1 || b.size() != 1 || !a[0].is_number() || !b[0].is_number()) return false;
    return a[0].get<double>() > b[0].get<double>();
  }
  return false;
}

ValueResult eval_value(const Json& expr, const ExprCtx& ctx) {
  ValueResult r;
  std::string op = json::get_string(expr, "op");
  Json args = json::find(expr, "args") ? expr["args"] : Json::array();
  if (!args.is_array()) args = Json::array({args});
  auto arg = [&](std::size_t i) -> Json { return i < args.size() ? args[i] : Json(); };
  if (op == "const") {
    r.values = as_list(arg(0));
  } else if (op == "slot") {
    r.values = resolve_list(arg(0), ctx);
  } else if (op == "principle_template") {
    r.values = as_list(arg(0));
  } else if (op == "union") {
    std::set<std::string> seen;
    for (const auto& a : args) {
      for (const auto& v : resolve_list(a, ctx)) {
        if (seen.insert(value_key(ctx.norm, v)).second) r.values.push_back(v);
      }
    }
  } else if (op == "complement") {
    Json b = resolve_list(arg(1), ctx);
    for (const auto& v : resolve_list(arg(0), ctx)) {
      if (!list_contains(ctx, b, v)) r.values.push_back(v);
    }
  } else if (op == "max_version") {
    Json l = resolve_list(Json{{"ref", "versions"}, {"subject", ctx.subject}}, ctx);
    std::string mx = max_version_of(l);
    if (!mx.empty()) r.values.push_back(mx);
  } else if (op == "versions_above") {
    Json git = resolve_list(arg(0), ctx);
    std::string g = max_version_of(git);
    if (!g.empty()) {
      for (const auto& v : resolve_list(Json{{"ref", "versions"}, {"subject", ctx.subject}}, ctx)) {
        std::string n = v.is_string() ? kb::normalize_version(v.get<std::string>()) : std::string();
        if (!n.empty() && kb::compare_versions(n, g) > 0) r.values.push_back(n);
      }
    }
  } else if (op == "option_from_decision") {
    Json options = resolve_list(arg(0), ctx);
    const model::Decision* best = nullptr;
    for (const auto& d : ctx.ix.ev.decisions) {
      if (d.subject != ctx.subject || d.status != model::DecisionStatus::Active || !d.chosen()) continue;
      if (!options.empty() && !list_contains(ctx, options, Json(d.chosen()->label))) continue;
      if (!best || d.date > best->date || (d.date == best->date && d.id > best->id)) best = &d;
    }
    if (best) {
      r.values.push_back(best->chosen()->label);
      if (ctx.used_claims) ctx.used_claims->push_back(best->id);
    }
  } else if (op == "derive_project_status") {
    // Explicit status claims win; then lineage; then inactivity (thresholds.status).
    auto it = ctx.ix.by_subject.find(ctx.subject);
    std::string last_date, explicit_status, explicit_date;
    bool merged = false;
    if (it != ctx.ix.by_subject.end()) {
      for (auto* c : it->second) {
        if (!observed_grade(*c)) continue;
        std::string d = ctx.ix.claim_date(*c);
        if (d > last_date) last_date = d;
        if (c->predicate == "merged_into" || c->predicate == "evolved_into") merged = true;
        if (c->predicate == "has_status" && c->value.is_string() && d >= explicit_date) {
          explicit_status = c->value.get<std::string>();
          explicit_date = d;
          if (ctx.used_claims) ctx.used_claims->push_back(c->id);
        }
      }
    }
    for (const auto& d : ctx.ix.ev.decisions) {
      if (d.subject == ctx.subject && day(d.date) > last_date) last_date = day(d.date);
    }
    std::string status;
    if (merged) {
      status = "merged";
    } else if (!explicit_status.empty()) {
      status = explicit_status;
    } else if (!last_date.empty() && ctx.pack) {
      long idle = days_between(last_date, ctx.ix.corpus_end);
      if (idle >= threshold(*ctx.pack, "status", "abandoned_after_days", 180)) {
        status = "abandoned";
      } else if (idle >= threshold(*ctx.pack, "status", "paused_after_days", 60)) {
        status = "paused";
      } else {
        status = "active";
      }
    }
    if (!status.empty()) r.values.push_back(status);
  } else if (op == "lexicon_pick") {
    std::string cls = arg(0).is_string() ? arg(0).get<std::string>() : std::string();
    Json members = ctx.pack ? lexicon_members(*ctx.pack, ctx.norm, cls) : Json::array();
    Json prefer = Json::array();
    for (std::size_t i = 1; i < args.size(); ++i) {
      if (const Json* p = json::find(args[i], "prefer")) {
        for (const auto& v : resolve_list(*p, ctx)) prefer.push_back(v);
      }
    }
    for (const auto& v : prefer) {
      if (members.empty() || list_contains(ctx, members, v)) {
        r.values.push_back(v);
        break;
      }
    }
    if (r.values.empty() && ctx.pack) {
      // Deterministic pick: the first entry (by id) of the class whose
      // "implies" covers every required property.
      std::vector<std::string> need;
      for (std::size_t i = 1; i < args.size(); ++i) {
        if (const Json* p = json::find(args[i], "implies"); p && p->is_string()) need.push_back(p->get<std::string>());
      }
      const Json* entries = json::find(ctx.pack->lexicon("gazetteer"), "entries");
      std::vector<std::pair<std::string, std::string>> cands;  // id, label
      if (entries && entries->is_array()) {
        for (const auto& e : *entries) {
          std::string c = json::get_string(e, "class");
          if (c != cls && !starts_with(c, cls + ".")) continue;
          bool ok = true;
          const Json* imp = json::find(e, "implies");
          for (const auto& n : need) {
            bool has = false;
            if (imp && imp->is_array()) {
              for (const auto& x : *imp) has |= x.is_string() && x.get<std::string>() == n;
            }
            ok &= has;
          }
          if (!ok) continue;
          std::string label = json::get_string(e, "id");
          if (const Json* l = json::find(e, "labels")) label = json::get_string(*l, "en", label);
          cands.emplace_back(json::get_string(e, "id"), label);
        }
      }
      std::sort(cands.begin(), cands.end());
      if (!cands.empty()) {
        r.values.push_back(cands.front().second);
        double score = 1.0 / static_cast<double>(cands.size());
        for (std::size_t i = 1; i < cands.size() && i < 4; ++i) r.alternatives.push_back(model::Alternative{"", Json(cands[i].second), score});
      }
    }
  } else {
    // codebase_field, derive_component_status, order_chain,
    // comentioned_components, base_version, version_forks,
    // first_mention_date, date_add: need the codebase digest / version records
    // of other areas; honest skip (I9).
    r.unsupported = "value op '" + op + "' needs data this area does not have (codebase digest / version records)";
  }
  return r;
}

}  // namespace loom::generalize::detail
