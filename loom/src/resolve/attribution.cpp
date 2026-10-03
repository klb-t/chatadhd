// Project attribution of units that never name their project (resolve.h):
// profile vectors, cosine + context features, a bounded deterministic
// fixpoint, multi-label shared foundations, inferred aliases between
// project entities with matching profiles. Everything produced here is
// INFERRED (origin system) with an Expected Property and the competing
// alternatives; nothing overwrites an observed claim (I3).
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

#include "loom/resolve.h"
#include "loom/util/utf8.h"

namespace loom::resolve {

using model::Claim;
using model::Entity;

AttributionConfig AttributionConfig::from_policy(const kb::Pack& pack) {
  AttributionConfig c;
  const Json* a = json::find(pack.policy("thresholds"), "attribution");
  if (!a || !a->is_object()) return c;
  c.max_passes = static_cast<int>(json::get_int(*a, "max_passes", c.max_passes));
  c.tau = json::get_number(*a, "tau", c.tau);
  c.tau_confident = json::get_number(*a, "tau_confident", c.tau_confident);
  c.ambiguity = json::get_number(*a, "ambiguity", c.ambiguity);
  c.margin = json::get_number(*a, "margin", c.margin);
  c.min_cosine = json::get_number(*a, "min_cosine", c.min_cosine);
  c.w_cosine = json::get_number(*a, "w_cosine", c.w_cosine);
  c.w_context = json::get_number(*a, "w_context", c.w_context);
  c.time_scale_days = json::get_number(*a, "time_scale_days", c.time_scale_days);
  c.rare_df = static_cast<int>(json::get_int(*a, "rare_df", c.rare_df));
  c.min_shared_terms = static_cast<int>(json::get_int(*a, "min_shared_terms", c.min_shared_terms));
  c.foundations = json::get_bool(*a, "foundations", c.foundations);
  c.alias_tau = json::get_number(*a, "alias_tau", c.alias_tau);
  c.alias_max_units = static_cast<int>(json::get_int(*a, "alias_max_units", c.alias_max_units));
  return c;
}

Json AttributionCandidate::to_json() const {
  Json s = Json::array();
  for (const auto& x : shared) s.push_back(x);
  auto r = [](double x) { return std::round(x * 1e4) / 1e4; };
  return Json{{"target", target}, {"kind", kind}, {"score", r(score)}, {"cosine", r(cosine)}, {"context", r(context)}, {"shared", s}};
}

Json UnitAttribution::to_json() const {
  Json c = Json::array();
  for (const auto& x : candidates) c.push_back(x.to_json());
  Json ch = Json::array();
  for (const auto& x : chosen) ch.push_back(x);
  return Json{{"unit", unit}, {"subject", subject}, {"candidates", c}, {"chosen", ch}, {"pass", pass}};
}

Json AttributionResult::to_json() const {
  Json u = Json::array();
  for (const auto& x : units) u.push_back(x.to_json());
  Json f = Json::array();
  for (const auto& x : foundations) f.push_back(x.to_json());
  Json c = Json::array();
  for (const auto& x : claims) c.push_back(x.to_json());
  return Json{{"method", method}, {"units", u}, {"foundations", f}, {"claims", c}, {"resubjected", resubjected.size()},
              {"stats", stats}};
}

namespace {

// Kinds whose mentions describe what a project is made of.
bool content_kind(std::string_view k) {
  return k == "component" || k == "concept" || k == "feature" || k == "branch" || k == "option" || k == "storage" ||
         k == "tool" || k == "protocol" || k == "format" || k == "sync" || k == "citation" || k == "party" ||
         k == "ui_framework";
}
bool foundation_kind(std::string_view k) {
  return k == "component" || k == "concept" || k == "storage" || k == "tool" || k == "protocol" || k == "format" ||
         k == "sync" || k == "ui_framework";
}

double days_between(const std::string& a, const std::string& b) {
  auto parse = [](const std::string& d) -> double {
    if (d.size() < 10) return NAN;
    int y = std::atoi(d.substr(0, 4).c_str()), m = std::atoi(d.substr(5, 2).c_str()), dd = std::atoi(d.substr(8, 2).c_str());
    // days since a fixed epoch (civil from days, Howard Hinnant)
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = static_cast<unsigned>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097.0 + static_cast<double>(doe);
  };
  double x = parse(a), y = parse(b);
  if (std::isnan(x) || std::isnan(y)) return NAN;
  return std::fabs(x - y);
}

double isotonic_inferred(const kb::Pack& pack, double x) {
  const Json* iso = json::find(pack.policy("calibration"), "isotonic");
  const Json* p = iso ? json::find(*iso, "inferred") : nullptr;
  if (!p || !p->is_array() || p->empty()) return x;
  std::vector<std::pair<double, double>> pts;
  for (const auto& q : *p) {
    if (q.is_array() && q.size() == 2) pts.emplace_back(q[0].get<double>(), q[1].get<double>());
  }
  std::sort(pts.begin(), pts.end());
  if (pts.empty()) return x;
  if (x <= pts.front().first) return pts.front().second;
  if (x >= pts.back().first) return pts.back().second;
  for (std::size_t i = 1; i < pts.size(); ++i) {
    if (x <= pts[i].first) {
      double t = (x - pts[i - 1].first) / std::max(1e-12, pts[i].first - pts[i - 1].first);
      return pts[i - 1].second + t * (pts[i].second - pts[i - 1].second);
    }
  }
  return x;
}

struct UnitData {
  std::string id;
  std::string text;
  std::string date;
  std::string gizmo;
  std::string document;           // document entity ("" = named)
  std::string project;            // named project ("" = none)
  std::set<std::string> mentions; // entity ids
};

}  // namespace

Result<AttributionResult> attribute_units(const kb::Pack& pack, const std::vector<Entity>& entities,
                                          const std::vector<Claim>& claims,
                                          const std::vector<model::Observation>& observations, VectorSpace& space,
                                          const AttributionConfig& cfg) {
  AttributionResult out;
  out.method = space.method();
  std::map<std::string, const Entity*> ent;
  for (const auto& e : entities) {
    if (e.status == model::ClaimStatus::Active || e.status == model::ClaimStatus::Contested) ent[e.id] = &e;
  }
  // ── units ────────────────────────────────────────────────────────
  std::map<std::string, UnitData> units;
  std::map<std::string, std::string> obs_unit;
  for (const auto& o : observations) {
    obs_unit[o.id] = o.unit;
    UnitData& u = units[o.unit];
    u.id = o.unit;
    bool leaf = o.kind != model::ObservationKind::Utterance || json::get_bool(o.attrs, "leaf");
    if (leaf) {
      u.text += o.text;
      u.text += "\n";
    }
    std::string d = o.date.substr(0, std::min<std::size_t>(10, o.date.size()));
    if (!d.empty() && (u.date.empty() || d < u.date)) u.date = d;
    std::string g = json::get_string(o.attrs, "project_ext_id", json::get_string(o.attrs, "gizmo_id"));
    if (!g.empty()) u.gizmo = g;
  }
  for (const auto& e : entities) {
    if (e.kind == "document" && e.canonical_key.rfind("unit ", 0) == 0) {
      auto it = units.find(e.canonical_key.substr(5));
      if (it != units.end()) {
        it->second.document = e.id;
        it->second.text += e.label + "\n" + e.label + "\n";  // the unit title
      }
    }
  }
  kb::Normalizer norm(pack);
  std::vector<std::pair<std::string, bool>> bridge_cues;  // folded phrase, prefix
  if (const Json* cl = json::find(pack.lexicon("cues"), "classes")) {
    if (const Json* b = json::find(*cl, "bridge")) {
      if (const Json* ps = json::find(*b, "phrases"); ps && ps->is_array()) {
        for (const auto& p : *ps) {
          std::string t = json::get_string(p, "p");
          bool prefix = !t.empty() && t.back() == '*';
          if (prefix) t.pop_back();
          bridge_cues.emplace_back(norm.fold(t), prefix);
        }
      }
    }
  }
  auto bridged = [&](const std::string& text) {
    std::string f = " " + norm.fold(text) + " ";
    for (auto& ch : f) {
      if (ch == ',' || ch == '.' || ch == ':' || ch == ';' || ch == '!' || ch == '?' || ch == '(' || ch == ')') ch = ' ';
    }
    for (const auto& [p, prefix] : bridge_cues) {
      if (f.find(" " + p + (prefix ? "" : " ")) != std::string::npos) return true;
    }
    return false;
  };
  std::map<std::string, std::string> obs_text;
  for (const auto& o : observations) obs_text[o.id] = o.text;
  std::map<std::string, std::map<std::string, int>> project_hits;  // unit -> project -> mentions
  std::map<std::string, std::set<std::string>> ent_units;          // entity -> units
  std::map<std::string, std::set<std::string>> obs_projects;       // observation -> projects it names
  for (const auto& c : claims) {
    if (c.predicate != "mentioned_in" || !c.value.is_string()) continue;
    std::string u = c.value.get<std::string>();
    auto uit = units.find(u);
    auto eit = ent.find(c.subject);
    if (uit == units.end() || eit == ent.end()) continue;
    uit->second.mentions.insert(c.subject);
    ent_units[c.subject].insert(u);
    if (eit->second->kind == "project") {
      for (const auto& s : c.assessment.support) {
        // "the same pattern as with Stroz": a project named inside a
        // comparison is a cross-reference, not what the unit is about
        auto ot = obs_text.find(s.observation);
        if (ot != obs_text.end() && bridged(ot->second)) continue;
        project_hits[u][c.subject] += 1;
        obs_projects[s.observation].insert(c.subject);
      }
    }
  }
  for (auto& [id, u] : units) {
    if (!u.document.empty()) continue;
    int best = 0, total = 0;
    std::string bp;
    for (const auto& [p, n] : project_hits[id]) {
      total += n;
      if (n > best) {
        best = n;
        bp = p;
      }
    }
    // a unit that names several projects (a status overview, a memory) is
    // not evidence of what any single one of them is made of
    if (total > 0 && best * 5 >= total * 3) u.project = bp;
  }
  // ── the space ────────────────────────────────────────────────────
  std::vector<EmbedInput> corpus;
  for (const auto& [id, u] : units) corpus.push_back(EmbedInput{id, "text", u.text, "", ""});
  LOOM_TRY(space.fit(corpus));
  std::map<std::string, SparseVec> unit_vec;
  {
    LOOM_TRY_ASSIGN(auto vs, space.vectors(corpus));
    for (std::size_t i = 0; i < corpus.size(); ++i) unit_vec[corpus[i].id] = std::move(vs[i]);
  }
  // ── project members and profiles ─────────────────────────────────
  std::map<std::string, std::set<std::string>> members;  // project -> units
  for (const auto& [id, u] : units) {
    if (!u.project.empty()) members[u.project].insert(id);
  }
  // Sentence-level seeds: an observation that names exactly one project (in
  // a status overview, a memory, a project description) describes that
  // project even when its unit names several.
  struct Seed {
    std::string unit;
    std::string text;
    SparseVec vector;
  };
  std::map<std::string, std::vector<Seed>> seeds;  // project -> located seed observations
  {
    std::map<std::string, const model::Observation*> ob;
    for (const auto& o : observations) ob[o.id] = &o;
    for (const auto& [oid, ps] : obs_projects) {
      if (ps.size() != 1) continue;
      auto it = ob.find(oid);
      if (it == ob.end()) continue;
      const std::string& p = *ps.begin();
      if (members[p].count(it->second->unit)) continue;  // already in the profile
      seeds[p].push_back(Seed{it->second->unit, it->second->text, {}});
    }
    std::vector<EmbedInput> in;
    std::vector<Seed*> owner;
    for (auto& [p, ts] : seeds) {
      for (auto& seed : ts) {
        in.push_back(EmbedInput{p, "text", seed.text, "", ""});
        owner.push_back(&seed);
      }
    }
    LOOM_TRY_ASSIGN(auto vs, space.vectors(in));
    for (std::size_t i = 0; i < vs.size(); ++i) owner[i]->vector = std::move(vs[i]);
    for (const auto& [p, ts] : seeds) members[p];  // a seeded project has a profile
  }
  auto profile_weight = [&](const std::string& p) { return members[p].size() + (seeds.count(p) ? seeds[p].size() : 0); };
  auto profile_text = [&](const std::string& p, const std::string& excluded = "") {
    std::string t;
    const Entity* e = ent.count(p) ? ent[p] : nullptr;
    for (int k = 0; k < 3 && e; ++k) {
      t += e->label + "\n";
      for (const auto& a : e->aliases) t += a.surface + "\n";
    }
    if (seeds.count(p)) {
      for (const auto& seed : seeds[p]) {
        if (seed.unit != excluded) t += seed.text + "\n";
      }
    }
    for (const auto& u : members[p]) {
      if (u == excluded) continue;
      t += units[u].text;
      for (const auto& m : units[u].mentions) {
        auto it = ent.find(m);
        if (it != ent.end() && content_kind(it->second->kind)) t += it->second->label + "\n" + it->second->label + "\n";
      }
    }
    return t;
  };
  auto build_profiles = [&](const std::vector<std::string>& targets, const std::function<std::string(const std::string&)>& text)
      -> Result<std::map<std::string, SparseVec>> {
    std::vector<EmbedInput> in;
    for (const auto& t : targets) in.push_back(EmbedInput{t, "text", text(t), "", ""});
    LOOM_TRY_ASSIGN(auto vs, space.vectors(in));
    std::map<std::string, SparseVec> m;
    for (std::size_t i = 0; i < in.size(); ++i) m[in[i].id] = std::move(vs[i]);
    return m;
  };
  // ── inferred aliases: small project entities matching a bigger one ──
  std::map<std::string, std::string> alias_of;  // small project -> canonical project
  std::map<std::string, int> named_units;       // project -> units that name it
  Json alias_probe = Json::object();
  for (const auto& [p, m] : members) named_units[p] = static_cast<int>(m.size());
  // Run before the fixpoint (profiles of named units) and after it (profiles
  // enriched by the attributed units).
  auto infer_aliases = [&]() -> Status {
    std::vector<std::string> ps;
    for (const auto& [p, m] : members) ps.push_back(p);
    LOOM_TRY_ASSIGN(auto prof, build_profiles(ps, [&](const std::string& p) { return profile_text(p); }));
    std::map<std::string, std::string> fresh;
    for (const auto& s : ps) {
      if (named_units[s] > cfg.alias_max_units || alias_of.count(s)) continue;
      std::string best, second;
      double bs = 0, ss = 0;
      for (const auto& b : ps) {
        // the canonical side: a proper name over a common noun ("NoteFlow"
        // over "notatnik"), then more evidence, then the id
        auto rank = [&](const std::string& p) {
          const std::string& l = ent.count(p) ? ent[p]->label : p;
          bool proper = std::any_of(l.begin(), l.end(), [](char ch) { return std::isupper(static_cast<unsigned char>(ch)); });
          return std::make_tuple(proper ? 1 : 0, profile_weight(p), p);
        };
        if (b == s || fresh.count(b) || alias_of.count(b) || !(rank(s) < rank(b))) continue;
        // Profile centroids only. (Member-set linkage was tried: after the
        // fixpoint it is contaminated by attributed units and merged distinct
        // projects on synthetic_dev.)
        double c = cosine(prof[s], prof[b]);
        if (c > bs) {
          ss = bs;
          second = best;
          bs = c;
          best = b;
        } else if (c > ss) {
          ss = c;
          second = b;
        }
      }
      if (!best.empty()) {
        alias_probe[ent.count(s) ? ent[s]->label : s] =
            Json{{"best", ent.count(best) ? ent[best]->label : best}, {"cosine", std::round(bs * 1e3) / 1e3},
                 {"second", std::round(ss * 1e3) / 1e3}};
      }
      if (best.empty() || bs < cfg.alias_tau || ss >= cfg.ambiguity * bs) continue;
      fresh[s] = best;
      alias_of[s] = best;
      Claim c;
      c.subject = s;
      c.predicate = "same_as";
      c.object = best;
      c.qualifiers.extra = Json{{"method", out.method}, {"cosine", std::round(bs * 1e4) / 1e4}};
      auto& a = c.assessment;
      a.evidence = model::EvidenceClass::Inferred;
      a.origin = model::Origin::System;
      a.derivation = model::Derivation{"resolve.attribution.alias", 1, "", 0};
      a.confidence = std::round(isotonic_inferred(pack, bs) * 1e4) / 1e4;
      kb::ExpectedProperty ep;
      ep.expr = Json{{"op", "co_mentioned_with"}, {"args", Json::array({s, best, cfg.min_shared_terms})}};
      ep.rationale = "the two names are used for projects described by the same terms (profile cosine " +
                     json::format_float_py(std::round(bs * 100) / 100) + ")";
      ep.confirm_if = {"a unit names both as one project (apposition, 'X / Y')"};
      ep.refute_if = {"a unit names both as different projects"};
      a.expected = ep;
      a.check = model::CheckState::Pending;
      if (!second.empty()) a.alternatives.push_back(model::Alternative{second, Json(), std::round(ss * 1e4) / 1e4});
      c.id = Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
      out.claims.push_back(std::move(c));
    }
    for (const auto& [s, b] : fresh) {
      for (const auto& u : members[s]) members[b].insert(u);
      for (auto& t : seeds[s]) seeds[b].push_back(t);
      seeds.erase(s);
      members.erase(s);
      if (ent.count(b) && ent.count(s)) {
        Entity canon = *ent[b];
        for (auto al : ent[s]->aliases) {
          bool have = false;
          for (const auto& x : canon.aliases) have = have || x.key == al.key;
          if (have) continue;
          al.method = "inferred";
          canon.aliases.push_back(al);
        }
        std::sort(canon.aliases.begin(), canon.aliases.end(), [](const model::Alias& x, const model::Alias& y) { return x.key < y.key; });
        bool replaced = false;
        for (auto& ue : out.updated_entities) {
          if (ue.id == canon.id) {
            for (const auto& al : canon.aliases) {
              bool have = false;
              for (const auto& x : ue.aliases) have = have || x.key == al.key;
              if (!have) ue.aliases.push_back(al);
            }
            replaced = true;
          }
        }
        if (!replaced) out.updated_entities.push_back(std::move(canon));
      }
    }
    for (auto& [id, u] : units) {
      if (auto it = fresh.find(u.project); it != fresh.end()) u.project = it->second;
    }
    return {};
  };
  LOOM_TRY(infer_aliases());
  // A centroid must never contain the unit being scored. In particular,
  // fixpoint membership is an inference, not fresh evidence for itself.
  auto foundation_text = [&](const std::string& e, const std::string& excluded = "") {
    std::string t;
    for (const auto& u : ent_units[e]) {
      if (u != excluded) t += units[u].text;
    }
    for (int k = 0; k < 3; ++k) t += ent[e]->label + "\n";
    return t;
  };
  auto has_content_evidence = [&](const AttributionCandidate& c) {
    return c.cosine >= cfg.min_cosine ||
           static_cast<int>(c.shared.size()) >= std::max(1, cfg.min_shared_terms);
  };
  // ── scoring ──────────────────────────────────────────────────────
  auto score_unit = [&](const UnitData& u, const std::map<std::string, SparseVec>& prof, const std::string& kind)
      -> Result<std::vector<AttributionCandidate>> {
    std::vector<AttributionCandidate> cs;
    for (const auto& [p, pv] : prof) {
      AttributionCandidate c;
      c.target = p;
      c.kind = kind;
      c.cosine = cosine(unit_vec[u.id], pv);
      double time = 0.0;
      bool gizmo = false;
      const auto& punits = kind == "project" ? members[p] : ent_units[p];
      bool contains_unit = punits.count(u.id) > 0;
      if (kind == "project" && seeds.count(p)) {
        for (const auto& seed : seeds[p]) contains_unit = contains_unit || seed.unit == u.id;
      }
      if (contains_unit) {
        std::string text = kind == "project" ? profile_text(p, u.id) : foundation_text(p, u.id);
        LOOM_TRY_ASSIGN(auto independent, space.vectors({EmbedInput{p, "text", text, "", ""}}));
        c.cosine = cosine(unit_vec[u.id], independent.front());
      }
      // nearest member (kNN, k = 1) next to the profile centroid: a project
      // known from few units is still found through its closest one
      for (const auto& m : punits) {
        if (m != u.id) c.cosine = std::max(c.cosine, cosine(unit_vec[u.id], unit_vec[m]));
      }
      if (kind == "project" && seeds.count(p)) {
        for (const auto& seed : seeds[p]) {
          if (seed.unit != u.id) c.cosine = std::max(c.cosine, cosine(unit_vec[u.id], seed.vector));
        }
      }
      for (const auto& m : punits) {
        if (m == u.id) continue;
        double d = days_between(u.date, units[m].date);
        if (!std::isnan(d)) time = std::max(time, std::exp(-d / std::max(1.0, cfg.time_scale_days)));
        gizmo = gizmo || (!u.gizmo.empty() && u.gizmo == units[m].gizmo);
      }
      for (const auto& e : u.mentions) {
        auto it = ent.find(e);
        if (it == ent.end() || !content_kind(it->second->kind)) continue;
        if (static_cast<int>(ent_units[e].size()) > cfg.rare_df) continue;
        for (const auto& m : punits) {
          if (m != u.id && units[m].mentions.count(e)) {
            c.shared.push_back(it->second->label);
            break;
          }
        }
      }
      std::sort(c.shared.begin(), c.shared.end());
      double shared = std::min(1.0, static_cast<double>(c.shared.size()) / std::max(1, cfg.min_shared_terms));
      // A located mention of the entity underlying a foundation is direct
      // structural affiliation, just like a provider project id. It does
      // not rely on including this unit in the foundation's text profile.
      bool direct_foundation = kind == "foundation" && u.mentions.count(p) > 0;
      // Parallel projects make time alone weak evidence: it only sharpens shared identifiers.
      c.context = (gizmo || direct_foundation) ? 1.0 : (shared > 0 ? 0.25 * time + 0.75 * shared : 0.1 * time);
      c.score = cfg.w_cosine * c.cosine + cfg.w_context * c.context;
      cs.push_back(std::move(c));
    }
    std::sort(cs.begin(), cs.end(), [](const AttributionCandidate& a, const AttributionCandidate& b) {
      if (a.score != b.score) return a.score > b.score;
      return a.target < b.target;
    });
    return cs;
  };
  std::vector<std::string> pending;
  for (const auto& [id, u] : units) {
    if (!u.document.empty()) pending.push_back(id);
  }
  std::map<std::string, int> settled_pass;
  int passes = 0;
  for (int pass = 1; pass <= cfg.max_passes; ++pass) {
    std::vector<std::string> ps;
    for (const auto& [p, m] : members) ps.push_back(p);
    if (ps.empty()) break;
    LOOM_TRY_ASSIGN(auto prof, build_profiles(ps, [&](const std::string& p) { return profile_text(p); }));
    // the most confident unit per project joins it (gradual, so one early
    // mistake cannot drag a whole cluster along)
    std::map<std::string, std::pair<double, std::string>> best_for;  // project -> (score, unit)
    for (const auto& uid : pending) {
      if (settled_pass.count(uid)) continue;
      LOOM_TRY_ASSIGN(auto cs, score_unit(units[uid], prof, "project"));
      cs.erase(std::remove_if(cs.begin(), cs.end(), [&](const auto& c) { return !has_content_evidence(c); }), cs.end());
      if (cs.empty()) continue;
      bool unambiguous = cs.size() < 2 || cs[1].score * cfg.margin <= cs[0].score;
      if (cs[0].score < cfg.tau_confident || !unambiguous) continue;
      auto& b = best_for[cs[0].target];
      if (cs[0].score > b.first || (cs[0].score == b.first && uid < b.second)) b = {cs[0].score, uid};
    }
    std::vector<std::pair<std::string, std::string>> accepted;
    for (const auto& [p, b] : best_for) accepted.emplace_back(b.second, p);
    passes = pass;
    if (accepted.empty()) break;
    for (const auto& [uid, p] : accepted) {
      members[p].insert(uid);
      settled_pass[uid] = pass;
    }
  }
  LOOM_TRY(infer_aliases());
  // ── shared foundations ───────────────────────────────────────────
  std::map<std::string, std::set<std::string>> users_of;  // entity -> projects using it
  if (cfg.foundations) {
    // only confident evidence: units that name their project (after alias
    // inference), and only typed entities (a mined concept is too weak)
    for (const auto& [id, u] : units) {
      std::string p = u.project;
      if (auto it = alias_of.find(p); it != alias_of.end()) p = it->second;
      if (p.empty()) continue;
      for (const auto& e : u.mentions) {
        auto it = ent.find(e);
        if (it == ent.end() || !foundation_kind(it->second->kind)) continue;
        if (it->second->kind == "concept" && json::get_string(it->second->attrs, "method") != "lexicon") continue;
        users_of[e].insert(p);
      }
    }
  }
  std::map<std::string, std::string> foundation_of;  // entity -> foundation id
  for (const auto& [e, ps] : users_of) {
    if (ps.size() < 2) continue;
    const Entity* src = ent[e];
    Entity f;
    f.kind = "foundation";
    f.canonical_key = "foundation " + src->canonical_key;
    f.label = src->label;
    f.id = Entity::make_id(f.kind, f.canonical_key);
    f.evidence = model::EvidenceClass::Derived;
    f.origin = model::Origin::System;
    f.confidence = 1.0;
    Json pj = Json::array();
    for (const auto& p : ps) pj.push_back(p);
    f.attrs = Json{{"entity", e}, {"projects", pj}};
    foundation_of[e] = f.id;
    auto derived = [&](const std::string& s, const std::string& pred, const std::string& o) {
      Claim c;
      c.subject = s;
      c.predicate = pred;
      c.object = o;
      c.assessment.evidence = model::EvidenceClass::Derived;
      c.assessment.origin = model::Origin::System;
      c.assessment.derivation = model::Derivation{"resolve.foundations", 1, "", 0};
      c.assessment.confidence = 1.0;
      c.assessment.premises.assumptions.push_back("mentioned in units of " + std::to_string(ps.size()) + " projects");
      c.id = Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
      out.claims.push_back(std::move(c));
    };
    derived(e, "instance_of", f.id);
    for (const auto& p : ps) derived(p, "uses_foundation", f.id);
    out.foundations.push_back(std::move(f));
  }
  // ── final pass: every pending unit, projects + foundations ──────
  std::vector<std::string> ps;
  for (const auto& [p, m] : members) ps.push_back(p);
  LOOM_TRY_ASSIGN(auto prof, build_profiles(ps, [&](const std::string& p) { return profile_text(p); }));
  std::vector<std::string> fs;
  for (const auto& [e, f] : foundation_of) fs.push_back(e);
  LOOM_TRY_ASSIGN(auto fprof, build_profiles(fs, [&](const std::string& e) { return foundation_text(e); }));
  int attributed = 0, ambiguous = 0;
  for (const auto& uid : pending) {
    const UnitData& u = units[uid];
    UnitAttribution ua;
    ua.unit = uid;
    ua.subject = u.document;
    ua.pass = settled_pass.count(uid) ? settled_pass[uid] : 0;
    LOOM_TRY_ASSIGN(ua.candidates, score_unit(u, prof, "project"));
    LOOM_TRY_ASSIGN(auto foundation_candidates, score_unit(u, fprof, "foundation"));
    for (auto& c : foundation_candidates) {
      c.target = foundation_of[c.target];
      ua.candidates.push_back(std::move(c));
    }
    std::sort(ua.candidates.begin(), ua.candidates.end(), [](const AttributionCandidate& a, const AttributionCandidate& b) {
      if (a.score != b.score) return a.score > b.score;
      return a.target < b.target;
    });
    // Context may rank supported candidates, but proximity (including a
    // shared provider project) cannot supply their missing content evidence.
    // Apply the same gate to EVERY label, before comparing ambiguity scores.
    double best = 0.0;
    for (const auto& c : ua.candidates) {
      if (has_content_evidence(c)) best = std::max(best, c.score);
    }
    for (const auto& c : ua.candidates) {
      if (has_content_evidence(c) && c.score >= cfg.tau && c.score >= cfg.ambiguity * best)
        ua.chosen.push_back(c.target);
    }
    if (!ua.chosen.empty()) ++attributed;
    if (ua.chosen.size() > 1) ++ambiguous;
    // about claims, one per chosen target, the others as alternatives
    std::string single_project;
    if (ua.chosen.size() == 1) {
      for (const auto& c : ua.candidates) {
        if (c.target == ua.chosen.front() && c.kind == "project") single_project = c.target;
      }
    }
    std::string about_id;
    for (const auto& c : ua.candidates) {
      if (std::find(ua.chosen.begin(), ua.chosen.end(), c.target) == ua.chosen.end()) continue;
      Claim a;
      a.subject = u.document;
      a.predicate = "about";
      a.object = c.target;
      a.qualifiers.extra = Json{{"method", out.method}, {"target_kind", c.kind}, {"unit", uid}};
      auto& as = a.assessment;
      as.evidence = model::EvidenceClass::Inferred;
      as.origin = model::Origin::System;
      as.derivation = model::Derivation{"resolve.attribution", 2, "", 0};
      as.confidence = std::round(isotonic_inferred(pack, std::clamp(c.score, 0.0, 1.0)) * 1e4) / 1e4;
      kb::ExpectedProperty ep;
      ep.expr = Json{{"op", "co_mentioned_with"}, {"args", Json::array({u.document, c.target, cfg.min_shared_terms})}};
      ep.rationale = "the unit is described by the terms of " + std::string(c.kind == "project" ? "project " : "foundation ") +
                     (ent.count(c.target) ? ent[c.target]->label : c.target) + " (cosine " +
                     json::format_float_py(std::round(c.cosine * 100) / 100) + ", context " +
                     json::format_float_py(std::round(c.context * 100) / 100) + ")";
      ep.confirm_if = {"the unit (or its continuation) names the target",
                       "the unit mentions >= " + std::to_string(cfg.min_shared_terms) + " components/terms of the target"};
      ep.refute_if = {"the unit names another project as its subject"};
      as.expected = ep;
      as.check = model::CheckState::Pending;
      for (const auto& o : ua.candidates) {
        if (o.target != c.target) as.alternatives.push_back(model::Alternative{o.target, Json(), std::round(o.score * 1e4) / 1e4});
      }
      Json feats = c.to_json();
      as.premises.assumptions.push_back("features: " + json::dump(feats));
      a.id = Claim::make_id(a.subject, a.predicate, a.object, a.value, a.qualifiers);
      if (c.target == single_project) about_id = a.id;
      out.claims.push_back(std::move(a));
    }
    // re-subject the unit's claims onto an unambiguous project (inferred copies)
    if (!single_project.empty()) {
      out.subject_of_unit[uid] = single_project;
      double aconf = 0;
      for (const auto& cl : out.claims) {
        if (cl.id == about_id) aconf = cl.assessment.confidence;
      }
      for (const auto& c : claims) {
        if (c.subject != u.document || c.assessment.status != model::ClaimStatus::Active) continue;
        if (c.assessment.evidence != model::EvidenceClass::Observed) continue;
        Claim n = c;
        n.subject = single_project;
        if (!n.qualifiers.extra.is_object()) n.qualifiers.extra = Json::object();
        n.qualifiers.extra["original_subject"] = u.document;
        auto& as = n.assessment;
        as.evidence = model::EvidenceClass::Inferred;
        as.origin = model::Origin::System;
        as.derivation = model::Derivation{"resolve.attribution", 2, "", 1};
        as.premises.claims = {c.id, about_id};
        as.confidence = std::round(std::min(c.assessment.confidence, aconf) * 1e4) / 1e4;
        kb::ExpectedProperty ep;
        ep.expr = Json{{"op", "co_mentioned_with"}, {"args", Json::array({u.document, single_project, cfg.min_shared_terms})}};
        ep.rationale = "stated in a unit attributed to the project (see the about claim)";
        ep.refute_if = {"the unit is shown to be about another project"};
        as.expected = ep;
        as.check = model::CheckState::Pending;
        n.id = Claim::make_id(n.subject, n.predicate, n.object, n.value, n.qualifiers);
        out.resubjected.push_back(std::move(n));
      }
    }
    out.units.push_back(std::move(ua));
  }
  std::sort(out.claims.begin(), out.claims.end(), [](const Claim& a, const Claim& b) { return a.id < b.id; });
  out.claims.erase(std::unique(out.claims.begin(), out.claims.end(), [](const Claim& a, const Claim& b) { return a.id == b.id; }),
                   out.claims.end());
  std::sort(out.resubjected.begin(), out.resubjected.end(), [](const Claim& a, const Claim& b) { return a.id < b.id; });
  std::sort(out.foundations.begin(), out.foundations.end(), [](const Entity& a, const Entity& b) { return a.id < b.id; });
  out.stats = Json{{"method", out.method},
                   {"units", units.size()},
                   {"unattributed", pending.size()},
                   {"attributed", attributed},
                   {"ambiguous", ambiguous},
                   {"passes", passes},
                   {"foundations", out.foundations.size()},
                   {"inferred_aliases", alias_of.size()},
                   {"resubjected", out.resubjected.size()}};
  Json mj = Json::object();
  for (const auto& [p, us] : members) {
    Json l = Json::array();
    for (const auto& u : us) {
      std::string lab = u;
      if (ent.count(units[u].document)) lab = ent[units[u].document]->label;
      l.push_back(lab + (settled_pass.count(u) ? "@" + std::to_string(settled_pass[u]) : ""));
    }
    mj[ent.count(p) ? ent[p]->label : p] = l;
  }
  out.stats["members"] = mj;
  out.stats["alias_probe"] = alias_probe;
  return out;
}

}  // namespace loom::resolve

