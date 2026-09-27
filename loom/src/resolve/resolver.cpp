// Entity resolution (resolve.h): blocking by normalizer keys, alias keys,
// camel/acronym splits and trigram buckets; pair scores from key equality,
// lexicon aliases, splits, context vectors and kind agreement; union-find in
// (score desc, id, id) order above thresholds.concepts.merge_tau; a split
// guard refuses clusters whose contexts are bimodal. Every merge is a
// `same_as` claim (derived, origin system). Lineage is never an alias merge:
// the resolver merges only on identity evidence (names), and lineage cues
// are extracted as claims between entities. Deterministic.
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>

#include "archive/archive_internal.h"
#include "loom/resolve.h"
#include "loom/util/utf8.h"

namespace loom::resolve {

using model::Claim;
using model::Entity;

Json MergeDecision::to_json() const {
  return Json{{"a", a}, {"b", b}, {"score", score}, {"merged", merged}, {"blocked_by", blocked_by}, {"reasons", reasons}};
}

Json ResolveResult::to_json() const {
  Json ents = Json::array();
  for (const auto& e : entities) ents.push_back(e.to_json());
  Json rm = Json::object();
  for (const auto& [k, v] : remap) rm[k] = v;
  Json sa = Json::array();
  for (const auto& c : same_as) sa.push_back(c.to_json());
  Json ds = Json::array();
  for (const auto& d : decisions) ds.push_back(d.to_json());
  return Json{{"entities", ents}, {"remap", rm}, {"same_as", sa}, {"decisions", ds}};
}

Resolver::Resolver(std::shared_ptr<const kb::Pack> pack) : pack_(std::move(pack)) {}

namespace {

// Kinds that only say "a name": they take the kind of what they merge with.
bool generic_kind(std::string_view k) { return k == "concept" || k == "option" || k == "feature" || k == "topic"; }
// Kinds that never merge with another kind.
bool closed_kind(std::string_view k) { return k == "version" || k == "document" || k == "citation"; }

std::set<std::string> trigrams(std::string_view key) {
  std::u32string u = utf8::decode(std::string(" ") + std::string(key) + " ");
  std::set<std::string> out;
  for (std::size_t i = 0; i + 3 <= u.size(); ++i) out.insert(utf8::encode(u.substr(i, 3)));
  return out;
}

double jaccard(const std::set<std::string>& a, const std::set<std::string>& b) {
  if (a.empty() || b.empty()) return 0.0;
  std::size_t inter = 0;
  for (const auto& x : a) inter += b.count(x);
  return static_cast<double>(inter) / static_cast<double>(a.size() + b.size() - inter);
}

double cosine(const std::map<std::string, double>& a, const std::map<std::string, double>& b) {
  double dot = 0, na = 0, nb = 0;
  for (const auto& [k, v] : a) {
    na += v * v;
    if (auto it = b.find(k); it != b.end()) dot += v * it->second;
  }
  for (const auto& [k, v] : b) nb += v * v;
  return na > 0 && nb > 0 ? dot / std::sqrt(na * nb) : 0.0;
}

// One word without inner capitals or digits: a name that is also a common word.
bool plain_word(std::string_view label) {
  if (label.empty() || label.find(' ') != std::string_view::npos) return false;
  for (std::size_t i = 0; i < label.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(label[i]);
    if (std::isdigit(c) || (i > 0 && std::isupper(c)) || c == '-' || c == '_' || c == '.') return false;
  }
  return true;
}

struct Node {
  const Entity* e = nullptr;
  std::set<std::string> keys;       // canonical + alias keys (glossary-mapped)
  std::set<std::string> lex_keys;   // keys of lexicon aliases
  std::set<std::string> split_keys; // camel / acronym split keys
  std::map<std::string, double> ctx;
  std::map<std::string, std::set<std::string>> tri;  // trigrams of the long keys
};

struct DSU {
  std::vector<std::size_t> p;
  explicit DSU(std::size_t n) : p(n) { std::iota(p.begin(), p.end(), 0); }
  std::size_t find(std::size_t x) {
    while (p[x] != x) x = p[x] = p[p[x]];
    return x;
  }
};

}  // namespace

Result<ResolveResult> Resolver::resolve(const std::vector<Entity>& candidates,
                                        const std::vector<model::Observation>& observations) const {
  kb::Normalizer norm(*pack_);
  const Json& th = pack_->policy("thresholds");
  const Json* cc = json::find(th, "concepts");
  const double tau = cc ? json::get_number(*cc, "merge_tau", 0.6) : 0.6;
  const double fuzzy_tau = cc ? json::get_number(*cc, "fuzzy_trigram_jaccard", 0.6) : 0.6;
  const std::size_t fuzzy_min = cc ? static_cast<std::size_t>(json::get_int(*cc, "fuzzy_min_chars", 6)) : 6;
  const double bimodal = cc ? json::get_number(*cc, "split_guard_bimodality", 0.35) : 0.35;

  // candidates with the same id are one entity already
  std::map<std::string, Entity> uniq;
  for (const auto& e : candidates) {
    auto [it, fresh] = uniq.try_emplace(e.id, e);
    if (fresh) continue;
    for (const auto& a : e.aliases) {
      bool found = false;
      for (auto& x : it->second.aliases) {
        if (x.key == a.key) {
          x.count += a.count;
          found = true;
        }
      }
      if (!found) it->second.aliases.push_back(a);
    }
    if (const Json* o = json::find(e.attrs, "observations"); o && o->is_array()) {
      for (const auto& x : *o) it->second.attrs["observations"].push_back(x);
    }
  }
  std::vector<Entity> ents;
  for (auto& [id, e] : uniq) ents.push_back(std::move(e));

  std::map<std::string, const model::Observation*> obs;
  for (const auto& o : observations) obs[o.id] = &o;

  std::vector<Node> nodes(ents.size());
  for (std::size_t i = 0; i < ents.size(); ++i) {
    Node& n = nodes[i];
    const Entity& e = ents[i];
    n.e = &e;
    auto add_key = [&](const std::string& k, bool lex) {
      if (k.empty()) return;
      n.keys.insert(k);
      if (lex) n.lex_keys.insert(k);
    };
    add_key(e.canonical_key, false);
    add_key(norm.phrase_key(e.label), false);
    for (const auto& a : e.aliases) {
      add_key(a.key, a.method == "lexicon");
      add_key(norm.phrase_key(a.surface), a.method == "lexicon");
    }
    // camel / acronym splits: "ChatADHD" -> "chat adhd", "NoteFlow" -> "note flow"
    std::vector<std::string> surfaces{e.label};
    for (const auto& a : e.aliases) surfaces.push_back(a.surface);
    for (const auto& s : surfaces) {
      auto parts = archive::split_identifier(s);
      if (parts.size() >= 2) {
        std::string joined;
        for (const auto& p : parts) joined += (joined.empty() ? "" : " ") + p;
        std::string k = norm.phrase_key(joined);
        if (!k.empty()) n.split_keys.insert(k);
        // acronym of a multi-word name ("knowledge graph" -> "kg")
      }
      auto toks = norm.tokens(s);
      if (toks.size() >= 2) {
        std::string acr;
        for (const auto& t : toks) acr += t.substr(0, 1);
        n.split_keys.insert(acr);
      }
      // a split of a multi-word name, joined ("chat adhd" -> "chatadhd")
      std::string glued;
      for (const auto& t : toks) glued += t;
      if (toks.size() >= 2) n.split_keys.insert(norm.phrase_key(glued));
    }
    for (const auto& k : n.keys) {
      if (utf8::length(k) >= fuzzy_min) n.tri[k] = trigrams(k);
    }
    // context: content keys of the entity's observations, minus its own names
    std::set<std::string> own;
    for (const auto& k : n.keys) {
      for (std::size_t s = 0, t; s < k.size(); s = t + 1) {
        t = k.find(' ', s);
        if (t == std::string::npos) t = k.size();
        own.insert(k.substr(s, t - s));
      }
    }
    if (const Json* os = json::find(e.attrs, "observations"); os && os->is_array()) {
      for (const auto& x : *os) {
        if (!x.is_string()) continue;
        auto it = obs.find(x.get<std::string>());
        if (it == obs.end()) continue;
        for (const auto& t : norm.tokens(it->second->text)) {
          if (norm.is_stopword(t) || utf8::length(t) < 3) continue;
          std::string k = norm.match_key(t);
          if (!own.count(k)) n.ctx[k] += 1.0;
        }
      }
    }
  }

  // blocking
  std::map<std::string, std::vector<std::size_t>> block;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    for (const auto& k : nodes[i].keys) block["k:" + k].push_back(i);
    for (const auto& k : nodes[i].split_keys) block["k:" + k].push_back(i);
    for (const auto& k : nodes[i].keys) {
      if (utf8::length(k) >= fuzzy_min && k.find(' ') == std::string::npos) block["f:" + k.substr(0, 1)].push_back(i);
    }
  }
  std::set<std::pair<std::size_t, std::size_t>> pairs;
  for (auto& [bk, v] : block) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    if (v.size() > (bk.rfind("f:", 0) == 0 ? 120u : 200u)) continue;  // a key shared by everything is no evidence
    for (std::size_t a = 0; a < v.size(); ++a) {
      for (std::size_t b = a + 1; b < v.size(); ++b) pairs.emplace(v[a], v[b]);
    }
  }

  ResolveResult out;
  struct Scored {
    double s;
    std::size_t a, b;
    Json reasons;
    std::string blocked;
  };
  std::vector<Scored> scored;
  for (auto [a, b] : pairs) {
    const Node& x = nodes[a];
    const Node& y = nodes[b];
    const std::string& ka = x.e->kind;
    const std::string& kb_ = y.e->kind;
    Json reasons = Json::array();
    std::string blocked;
    if ((closed_kind(ka) || closed_kind(kb_)) && ka != kb_) blocked = "kind";
    bool kind_conflict = ka != kb_ && !generic_kind(ka) && !generic_kind(kb_);
    double s = 0.0;
    bool key_eq = false;
    bool lex = false;
    for (const auto& k : x.keys) {
      if (y.keys.count(k)) {
        key_eq = true;
        lex = lex || x.lex_keys.count(k) || y.lex_keys.count(k);
      }
    }
    bool split = false;
    for (const auto& k : x.split_keys) split = split || y.keys.count(k) > 0;
    for (const auto& k : y.split_keys) split = split || x.keys.count(k) > 0;
    double fuzzy = 0.0;
    if (!key_eq && !split) {
      for (const auto& k1 : x.keys) {
        for (const auto& k2 : y.keys) {
          if (utf8::length(k1) < fuzzy_min || utf8::length(k2) < fuzzy_min || k1[0] != k2[0]) continue;
          fuzzy = std::max(fuzzy, jaccard(x.tri.at(k1), y.tri.at(k2)));
        }
      }
      if (fuzzy < fuzzy_tau) fuzzy = 0.0;
    }
    if (key_eq) {
      s += 0.5;
      reasons.push_back("same_key");
    }
    if (lex) {
      s += 0.3;
      reasons.push_back("lexicon_alias");
    }
    if (split) {
      s += 0.5;
      reasons.push_back("camel_acronym_split");
    }
    if (fuzzy > 0) {
      s += 0.5 * fuzzy;
      reasons.push_back("trigram:" + json::format_float_py(std::round(fuzzy * 100) / 100));
    }
    double cos = cosine(x.ctx, y.ctx);
    if (cos > 0) {
      s += 0.3 * cos;
      reasons.push_back("context:" + json::format_float_py(std::round(cos * 100) / 100));
    }
    if (kind_conflict) {
      s -= 0.6;
      reasons.push_back("kind_conflict");
      if (blocked.empty()) blocked = "kind";
    } else {
      s += 0.2;
      reasons.push_back("kind_agrees");
    }
    // split guard: a plain dictionary word ("loom", "watchdog") shared by two
    // well-attested entities whose contexts are disjoint names two senses
    bool plain = plain_word(x.e->label) && plain_word(y.e->label);
    if (blocked.empty() && !lex && plain && x.ctx.size() >= 4 && y.ctx.size() >= 4 && cos < bimodal) {
      blocked = "split_guard";
      reasons.push_back("bimodal_context");
    }
    scored.push_back(Scored{s, a, b, reasons, blocked});
  }
  std::sort(scored.begin(), scored.end(), [&](const Scored& p, const Scored& q) {
    if (p.s != q.s) return p.s > q.s;
    if (nodes[p.a].e->id != nodes[q.a].e->id) return nodes[p.a].e->id < nodes[q.a].e->id;
    return nodes[p.b].e->id < nodes[q.b].e->id;
  });
  DSU dsu(nodes.size());
  // cluster context (for the split guard across transitive merges)
  std::vector<std::map<std::string, double>> cctx(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) cctx[i] = nodes[i].ctx;
  std::vector<std::string> ckind(nodes.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) ckind[i] = nodes[i].e->kind;
  for (const auto& sc : scored) {
    MergeDecision d;
    d.a = nodes[sc.a].e->id;
    d.b = nodes[sc.b].e->id;
    d.score = std::round(sc.s * 1000) / 1000;
    d.reasons = sc.reasons;
    d.blocked_by = sc.blocked;
    if (sc.s < tau) {
      if (sc.s >= tau - 0.3 || !sc.blocked.empty()) out.decisions.push_back(d);
      continue;
    }
    std::size_t ra = dsu.find(sc.a), rb = dsu.find(sc.b);
    if (d.blocked_by.empty() && ra != rb) {
      // cluster-level checks: kinds and bimodality of the merged cluster
      bool conflict = ckind[ra] != ckind[rb] && !generic_kind(ckind[ra]) && !generic_kind(ckind[rb]);
      if (conflict) d.blocked_by = "kind";
      else if (plain_word(nodes[sc.a].e->label) && plain_word(nodes[sc.b].e->label) && cctx[ra].size() >= 4 &&
               cctx[rb].size() >= 4 && cosine(cctx[ra], cctx[rb]) < bimodal &&
               std::find(sc.reasons.begin(), sc.reasons.end(), Json("lexicon_alias")) == sc.reasons.end()) {
        d.blocked_by = "split_guard";
      }
    }
    if (d.blocked_by.empty() && ra != rb) {
      dsu.p[rb] = ra;
      for (const auto& [k, v] : cctx[rb]) cctx[ra][k] += v;
      if (generic_kind(ckind[ra])) ckind[ra] = ckind[rb];
      d.merged = true;
    }
    out.decisions.push_back(d);
  }

  // clusters -> canonical entities
  std::map<std::size_t, std::vector<std::size_t>> clusters;
  for (std::size_t i = 0; i < nodes.size(); ++i) clusters[dsu.find(i)].push_back(i);
  auto rank = [&](std::size_t i) {
    const Entity& e = *nodes[i].e;
    int alias_n = 0;
    for (const auto& a : e.aliases) alias_n += a.count;
    bool lexicon = std::any_of(e.aliases.begin(), e.aliases.end(), [](const model::Alias& a) { return a.method == "lexicon"; });
    return std::make_tuple(generic_kind(e.kind) ? 0 : 1, lexicon ? 1 : 0, alias_n);
  };
  std::map<std::string, double> pair_score;
  for (const auto& d : out.decisions) {
    if (d.merged) pair_score[d.a + "|" + d.b] = d.score;
  }
  for (auto& [root, members] : clusters) {
    std::size_t best = members.front();
    for (std::size_t m : members) {
      auto rm = rank(m), rb = rank(best);
      if (rm > rb || (rm == rb && nodes[m].e->id < nodes[best].e->id)) best = m;
    }
    Entity canon = *nodes[best].e;
    for (std::size_t m : members) {
      const Entity& e = *nodes[m].e;
      out.remap[e.id] = canon.id;
      if (m == best) continue;
      for (auto a : e.aliases) {
        bool found = false;
        for (auto& x : canon.aliases) {
          if (x.key == a.key) {
            x.count += a.count;
            found = true;
          }
        }
        if (!found) {
          a.method = a.method == "lexicon" ? "lexicon" : "merge";
          canon.aliases.push_back(a);
        }
      }
      if (!e.first_seen.empty() && (canon.first_seen.empty() || e.first_seen < canon.first_seen)) canon.first_seen = e.first_seen;
      if (e.last_seen > canon.last_seen) canon.last_seen = e.last_seen;
      std::set<std::string> os;
      for (const Entity* src : {static_cast<const Entity*>(&canon), &e}) {
        if (const Json* o = json::find(src->attrs, "observations"); o && o->is_array()) {
          for (const auto& x : *o) {
            if (x.is_string()) os.insert(x.get<std::string>());
          }
        }
      }
      Json oj = Json::array();
      for (const auto& x : os) oj.push_back(x);
      canon.attrs["observations"] = oj;
      Json merged = canon.attrs.contains("merged") ? canon.attrs["merged"] : Json::array();
      merged.push_back(e.id);
      canon.attrs["merged"] = merged;
      // same_as claim: derived by the resolver, origin system
      Claim c;
      c.subject = e.id;
      c.predicate = "same_as";
      c.object = canon.id;
      double sc = 0.6;
      for (const auto& d : out.decisions) {
        if (d.merged && ((d.a == e.id) || (d.b == e.id))) sc = std::max(sc, d.score);
      }
      c.qualifiers.extra = Json{{"resolver", std::string(kResolverVersion)}};
      c.assessment.evidence = model::EvidenceClass::Derived;
      c.assessment.origin = model::Origin::System;
      c.assessment.derivation = model::Derivation{"resolve.same_as", 1, "", 0};
      c.assessment.confidence = std::clamp(sc, 0.0, 1.0);
      c.id = Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
      out.same_as.push_back(std::move(c));
    }
    std::sort(canon.aliases.begin(), canon.aliases.end(), [](const model::Alias& a, const model::Alias& b) { return a.key < b.key; });
    out.entities.push_back(std::move(canon));
  }
  std::sort(out.entities.begin(), out.entities.end(), [](const Entity& a, const Entity& b) { return a.id < b.id; });
  std::sort(out.same_as.begin(), out.same_as.end(), [](const Claim& a, const Claim& b) { return a.id < b.id; });
  return out;
}

std::vector<Claim> Resolver::apply_remap(const std::vector<Claim>& claims, const ResolveResult& r) const {
  std::map<std::string, Claim> out;
  std::vector<Claim> superseded;
  auto map_id = [&](const std::string& id) {
    auto it = r.remap.find(id);
    return it == r.remap.end() ? id : it->second;
  };
  for (const auto& c : claims) {
    Claim n = c;
    n.subject = map_id(c.subject);
    if (!c.object.empty()) n.object = map_id(c.object);
    if (!n.object.empty() && n.object == n.subject) {
      // a claim between two names of one entity says nothing new
      Claim old = c;
      old.assessment.status = model::ClaimStatus::Superseded;
      superseded.push_back(std::move(old));
      continue;
    }
    n.id = Claim::make_id(n.subject, n.predicate, n.object, n.value, n.qualifiers);
    if (n.id != c.id) {
      Claim old = c;
      old.assessment.status = model::ClaimStatus::Superseded;
      old.assessment.consequences.claims.push_back(n.id);
      superseded.push_back(std::move(old));
    }
    auto [it, fresh] = out.try_emplace(n.id, n);
    if (!fresh) {
      for (const auto& s : n.assessment.support) {
        bool dup = false;
        for (const auto& x : it->second.assessment.support) dup = dup || (x.observation == s.observation && x.extractor == s.extractor);
        if (!dup) it->second.assessment.support.push_back(s);
      }
      std::sort(it->second.assessment.support.begin(), it->second.assessment.support.end(),
                [](const model::Support& a, const model::Support& b) {
                  return std::tie(a.observation, a.extractor) < std::tie(b.observation, b.extractor);
                });
      it->second.assessment.confidence = std::max(it->second.assessment.confidence, n.assessment.confidence);
    }
  }
  std::vector<Claim> v;
  for (auto& [id, c] : out) v.push_back(std::move(c));
  for (auto& c : superseded) {
    if (!out.count(c.id)) v.push_back(std::move(c));
  }
  std::sort(v.begin(), v.end(), [](const Claim& a, const Claim& b) { return a.id < b.id; });
  return v;
}

}  // namespace loom::resolve
