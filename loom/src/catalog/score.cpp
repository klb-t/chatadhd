// catalog.h: Catalog::score — multi-pass relevance scoring (proposal_scale.md
// §5): pass 0 identity (alias hits + negative-context traps, verified
// against re-read bytes), pass 1 BM25 over sketches against the self/
// philosophy profiles, pass 2 vocabulary expansion (lift-based, from the
// verified-relevant set, logged with reasons) feeding back into BM25, and a
// linking pass (same project/gizmo, MinHash continuation, shared rare
// identifiers, same-session) with one damped propagation step. Every pass
// after 0 reads only loom_cat_* rows (sketches), never raw bytes.
//
// Disclosed simplification: pass 0's mention re-derivation still re-reads
// each unit's exact bytes (read_unit()) rather than matching the sketch, so
// a self-profile rebuilt after scanning is honoured exactly (not
// approximated by bloom/top-K lookups) — correct, but the part of score()
// that does not scale to a multi-GB corpus the way the design intends (see
// the git history for the previous, narrower version of this file). BM25,
// expansion and linking are sketch-only and do scale with corpus size, not
// per-unit byte size.
#include "loom/catalog.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>

#include "catalog_internal.h"
#include "loom/db.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom::catalog {

using namespace loom::catalog::internal;

namespace {

double sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }

Result<SelfProfile> load_profile(Database& db, std::string_view profile_id) {
  auto lk = db.lock();
  Result<std::optional<std::string>> body{std::nullopt};
  if (!profile_id.empty()) {
    body = db.conn().query_text("SELECT body FROM loom_cat_profiles WHERE id = ?", std::string(profile_id));
  } else {
    body = db.conn().query_text("SELECT body FROM loom_cat_profiles ORDER BY created DESC LIMIT 1");
  }
  if (!body) return body.error();
  if (!*body) return Error(Errc::NotFound, "no catalog profile" + (profile_id.empty() ? std::string() : (": " + std::string(profile_id))));
  LOOM_TRY_ASSIGN(Json j, json::parse(**body));
  return SelfProfile::from_json(j);
}

struct QueryTerm {
  std::string term;     // folded key, as stored in profile.terms[].key
  double weight = 1.0;  // term_class_weights[class]
};

struct Row {
  CatalogUnit unit;
  Sketch sketch;
  std::string title;
  std::string date;
  // Working state across passes.
  std::set<std::string> alias_projects;
  int alias_hits = 0, trap_hits = 0, version_hits = 0, code_hits = 0;
  std::string trap_reason;
  Json features = Json::object();
  Json reasons = Json::array();
  double score = 0.0;
  std::string label = "irrelevant";
};

// BM25 (k1, b from policy/relevance.json's "bm25") of `terms` against one
// unit's sketch: tf is exact when the term survived the top-K, 1 (a lower
// bound) when only the bloom filter has it, 0 otherwise -- proposal_scale.md
// §5.2. idf uses the standard Robertson/Sparck-Jones form over `df`/`n`.
double bm25_raw(const std::vector<QueryTerm>& terms, const Row& row, const std::unordered_map<std::string, int>& df,
                int n, double avg_len, double k1, double b) {
  if (terms.empty() || n <= 0) return 0.0;
  double doclen = static_cast<double>(std::max<std::int64_t>(1, row.sketch.n_chars));
  double norm_len = avg_len > 0 ? doclen / avg_len : 1.0;
  double sum = 0.0;
  for (const auto& qt : terms) {
    if (qt.term.empty()) continue;
    int tf = row.sketch.topk_tf(qt.term);
    if (tf == 0 && row.sketch.contains(qt.term)) tf = 1;  // bloom-only lower bound
    if (tf == 0) continue;
    auto it = df.find(qt.term);
    int d = it == df.end() ? 1 : it->second;
    double idf = std::log(1.0 + (static_cast<double>(n) - d + 0.5) / (d + 0.5));
    double denom = tf + k1 * (1.0 - b + b * norm_len);
    sum += qt.weight * idf * (tf * (k1 + 1.0)) / (denom <= 0 ? 1.0 : denom);
  }
  return sum;
}

// Document frequency of each query term across the corpus (sketch top-K
// exact hit or bloom probe) -- one pass over every row per term set, cheap
// for the bounded query sizes (tens to low hundreds of profile terms).
std::unordered_map<std::string, int> doc_frequencies(const std::vector<QueryTerm>& terms, const std::vector<Row>& rows) {
  std::unordered_map<std::string, int> df;
  for (const auto& qt : terms) {
    if (qt.term.empty()) continue;
    int n = 0;
    for (const auto& row : rows) {
      if (row.sketch.contains(qt.term)) ++n;
    }
    df[qt.term] = std::max(1, n);
  }
  return df;
}

// Sketches store single WORD match-keys (Sketch::build tokenises then keys
// each token), never phrases -- so a multi-word alias key ("appka od
// notatek") must be split into its own content-word keys the same way
// (norm.tokens + norm.match_key, stopwords dropped) for BM25 to find
// anything at all in the sketch's top-K/bloom. Each fragment keeps the
// term's class weight; a phrase contributes at most `phrase_cap` fragments
// so one long principle phrasing cannot dominate the query.
std::vector<QueryTerm> terms_of_class(const SelfProfile& profile, const std::map<std::string, double>& class_weights,
                                      bool principle_class, const kb::Normalizer& norm) {
  std::vector<QueryTerm> out;
  std::set<std::string> seen;
  constexpr int kPhraseCap = 6;
  for (const auto& t : profile.terms) {
    std::string cls = json::get_string(t, "class", "alias");
    bool is_principle = (cls == "principle");
    if (is_principle != principle_class) continue;
    std::string surface = json::get_string(t, "term");
    std::string key = json::get_string(t, "key");
    if (key.empty() && surface.empty()) continue;
    double w = 1.0;
    auto it = class_weights.find(cls);
    if (it != class_weights.end()) w = it->second;
    if (key.find(' ') == std::string::npos) {
      if (seen.insert(key).second) out.push_back(QueryTerm{key, w});
      continue;
    }
    int added = 0;
    for (auto& tok : norm.tokens(surface.empty() ? key : surface)) {
      if (norm.is_stopword(tok)) continue;
      std::string mk = norm.match_key(tok);
      if (mk.size() < 3 || !seen.insert(mk).second) continue;
      out.push_back(QueryTerm{mk, w});
      if (++added >= kPhraseCap) break;
    }
  }
  return out;
}

// 99th-percentile normaliser (policy/relevance.json "bm25.normalise_percentile"):
// squashes the raw BM25 sum into a roughly-[0,1] feature comparable across
// runs/corpora, the same way id_hits/etc. are bounded.
double percentile(std::vector<double> values, double pct) {
  if (values.empty()) return 1.0;
  std::sort(values.begin(), values.end());
  std::size_t idx = static_cast<std::size_t>(std::clamp(pct / 100.0, 0.0, 1.0) * (values.size() - 1));
  double v = values[idx];
  return v > 1e-9 ? v : 1.0;
}

// Re-derives the prose of a unit from its exact re-read bytes, the same way
// scan.cpp's ingest_unit did the first time, so mentions can be recomputed
// against a profile built after the scan.
std::string reextract_prose(const CatalogUnit& cu, std::string_view raw) {
  std::string kind;
  if (cu.unit.kind == "conversation") kind = cu.platform == "chatgpt" ? "chatgpt" : "claude";
  else if (cu.unit.kind == "project") kind = "claude_projects";
  else if (cu.unit.kind == "memory") kind = "claude_memories";
  else if (cu.unit.kind == "record") kind = "record";
  if (!kind.empty()) {
    if (auto parsed = json::parse(raw)) return extract_text(*parsed, kind).prose;
  }
  return std::string(raw);
}

}  // namespace

Result<Json> Catalog::score(const ScoreConfig& cfg, const ProgressFn& progress, const CancelToken* cancel) {
  LOOM_TRY(ensure_schema(rt_.db()));

  SelfProfile profile;
  {
    auto p = load_profile(rt_.db(), cfg.profile_id);
    if (!p) {
      LOOM_TRY_ASSIGN(profile, build_profile(ProfileConfig{}));
    } else {
      profile = std::move(*p);
    }
  }
  AliasIndex alias_idx = AliasIndex::from_profile(profile);
  kb::Normalizer norm(*pack_);

  const Json& self = pack_->profile("self");
  std::string date_from;
  double date_penalty = 0.0;
  if (const Json* dp = json::find(self, "date_prior"); dp && dp->is_object()) {
    date_from = json::get_string(*dp, "from");
    date_penalty = json::get_number(*dp, "soft_penalty_before", 0.0);
  }

  const Json& relevance = pack_->policy("relevance");
  double bias = json::get_number(relevance, "bias", 0.0);
  std::map<std::string, double> weights;
  if (const Json* w = json::find(relevance, "weights"); w && w->is_object()) {
    for (auto it = w->begin(); it != w->end(); ++it) weights[it.key()] = it.value().get<double>();
  }
  std::map<std::string, double> class_weights;
  if (const Json* cw = json::find(relevance, "term_class_weights"); cw && cw->is_object()) {
    for (auto it = cw->begin(); it != cw->end(); ++it) class_weights[it.key()] = it.value().get<double>();
  }
  double bm25_k1 = 1.2, bm25_b = 0.75;
  double normalise_pct = 99.0;
  if (const Json* bm = json::find(relevance, "bm25"); bm && bm->is_object()) {
    bm25_k1 = json::get_number(*bm, "k1", bm25_k1);
    bm25_b = json::get_number(*bm, "b", bm25_b);
    normalise_pct = json::get_number(*bm, "normalise_percentile", normalise_pct);
  }

  const Json& thresholds = pack_->policy("thresholds");
  double tau_relevant = 0.7, tau_candidate = 0.35;
  int max_expansion_passes = 3, expansion_max_terms = 16, link_shared_rare = 3, link_same_session_hours = 2;
  double expansion_min_lift = 3.0, link_min_jaccard = 0.4, link_damping = 0.6;
  int expansion_min_df_rel = 3;
  if (const Json* cat = json::find(thresholds, "catalog"); cat && cat->is_object()) {
    tau_relevant = json::get_number(*cat, "tau_relevant", tau_relevant);
    tau_candidate = json::get_number(*cat, "tau_candidate", tau_candidate);
    max_expansion_passes = static_cast<int>(json::get_int(*cat, "max_expansion_passes", max_expansion_passes));
    expansion_min_lift = json::get_number(*cat, "expansion_min_lift", expansion_min_lift);
    expansion_min_df_rel = static_cast<int>(json::get_int(*cat, "expansion_min_df_relevant", expansion_min_df_rel));
    expansion_max_terms = static_cast<int>(json::get_int(*cat, "expansion_max_terms_per_pass", expansion_max_terms));
    link_min_jaccard = json::get_number(*cat, "link_minhash_jaccard", link_min_jaccard);
    link_shared_rare = static_cast<int>(json::get_int(*cat, "link_shared_rare_terms", link_shared_rare));
    link_same_session_hours = static_cast<int>(json::get_int(*cat, "link_same_session_hours", link_same_session_hours));
    link_damping = json::get_number(*cat, "link_damping", link_damping);
  }
  int max_passes = cfg.max_passes > 0 ? std::min(cfg.max_passes, max_expansion_passes) : max_expansion_passes;

  Json fp{{"profile", profile.input_hash}, {"pack", pack_->hash()}, {"cfg", cfg.to_json()}};
  std::string run_id = "run_" + Sha256::hex(json::canonical(fp)).substr(0, 16);

  // ── Load every unit + its sketch ────────────────────────────────────
  std::vector<Row> rows;
  {
    auto lk = rt_.db().lock();
    LOOM_TRY_ASSIGN(sql::Stmt st, rt_.db().conn().prepare("SELECT body, sketch FROM loom_cat_units ORDER BY id"));
    while (true) {
      LOOM_TRY_ASSIGN(bool has, st.step());
      if (!has) break;
      auto j = json::parse(st.get_text(0));
      if (!j) continue;
      auto cu = CatalogUnit::from_json(*j);
      if (!cu) continue;
      auto sk = Sketch::from_json(json::parse_or(st.get_text(1), Json::object()));
      Row row;
      row.title = cu->unit.title;
      row.date = cu->unit.date;
      row.unit = std::move(*cu);
      row.sketch = sk ? std::move(*sk) : Sketch{};
      rows.push_back(std::move(row));
    }
  }
  int N = static_cast<int>(rows.size());
  double avg_len = 0.0;
  for (auto& r : rows) avg_len += static_cast<double>(std::max<std::int64_t>(1, r.sketch.n_chars));
  if (N > 0) avg_len /= N;

  // ── Pass 0: identity (verified mentions) ────────────────────────────
  std::int64_t idx_i = 0;
  for (auto& row : rows) {
    ++idx_i;
    if (cancel && cancel->cancelled()) break;
    if (progress) progress("score.identity", idx_i, static_cast<std::int64_t>(rows.size()), row.unit.unit.id);
    const CatalogUnit& cu = row.unit;

    Json mentions = cu.mentions;  // fallback: scan-time mentions
    if (auto raw = read_unit(cu.unit.id)) {
      std::string prose = reextract_prose(cu, *raw);
      std::string folded = norm.fold(prose.substr(0, static_cast<std::size_t>(std::min<std::int64_t>(
                                        kSketchByteCap, static_cast<std::int64_t>(prose.size())))));
      auto fresh = alias_idx.find(folded, 50);
      auto vers = find_version_mentions(folded, fresh, 80);
      for (auto& v : vers) fresh.push_back(std::move(v));
      mentions = Json::array();
      for (auto& m : fresh) mentions.push_back(m.to_json());
    }
    for (const auto& mj : mentions) {
      std::string kind = json::get_string(mj, "kind");
      bool trap = json::get_bool(mj, "trap");
      if (trap) {
        ++row.trap_hits;
        if (row.trap_reason.empty()) row.trap_reason = json::get_string(mj, "trap_reason");
        continue;
      }
      if (kind == "alias" || kind == "principle") {
        ++row.alias_hits;
        std::string key = json::get_string(mj, "key");
        if (!key.empty()) row.alias_projects.insert(key);
      } else if (kind == "version") {
        ++row.version_hits;
      }
    }
    if (cu.n_code_chars > 0 && row.alias_hits > 0) row.code_hits = 1;
  }

  // ── Pass 1+: BM25 over sketches, expanded by lift-based vocabulary
  // expansion fed back from the verified-relevant set each round. ────────
  std::vector<QueryTerm> self_terms = terms_of_class(profile, class_weights, /*principle_class=*/false, norm);
  std::vector<QueryTerm> phil_terms = terms_of_class(profile, class_weights, /*principle_class=*/true, norm);
  std::set<std::string> known_self_terms;
  for (auto& t : self_terms) known_self_terms.insert(t.term);
  double expansion_weight = 0.8;
  {
    auto it = class_weights.find("expansion");
    if (it != class_weights.end()) expansion_weight = it->second;
  }

  Json expanded_terms_log = Json::array();
  auto recompute_bm25 = [&] {
    auto df_self = doc_frequencies(self_terms, rows);
    auto df_phil = doc_frequencies(phil_terms, rows);
    std::vector<double> raw_self, raw_phil;
    raw_self.reserve(rows.size());
    raw_phil.reserve(rows.size());
    for (auto& row : rows) {
      raw_self.push_back(bm25_raw(self_terms, row, df_self, N, avg_len, bm25_k1, bm25_b));
      raw_phil.push_back(bm25_raw(phil_terms, row, df_phil, N, avg_len, bm25_k1, bm25_b));
    }
    double p_self = percentile(raw_self, normalise_pct);
    double p_phil = percentile(raw_phil, normalise_pct);
    for (std::size_t i = 0; i < rows.size(); ++i) {
      rows[i].features["bm25_self"] = std::min(2.0, raw_self[i] / p_self);
      rows[i].features["bm25_phil"] = std::min(2.0, raw_phil[i] / p_phil);
    }
  };

  auto compute_linear_and_label = [&](Row& row) {
    double linear = bias;
    Json reasons = Json::array();
    for (auto& [k, w] : weights) {
      double f = json::get_number(row.features, k, 0.0);
      double contribution = w * f;
      linear += contribution;
      if (std::abs(contribution) > 1e-9) reasons.push_back(Json{{"feature", k}, {"contribution", contribution}, {"evidence", f}});
    }
    if (!date_from.empty() && !row.date.empty() && row.date < date_from) {
      linear += date_penalty;
      reasons.push_back(Json{{"feature", "date_prior"}, {"contribution", date_penalty}, {"evidence", row.date}});
    }
    std::sort(reasons.begin(), reasons.end(),
             [](const Json& a, const Json& b) { return std::abs(a["contribution"].get<double>()) > std::abs(b["contribution"].get<double>()); });
    row.reasons = reasons;
    row.score = std::round(sigmoid(linear) * 1e4) / 1e4;
    row.label = row.score >= tau_relevant ? "relevant" : (row.score >= tau_candidate ? "candidate" : "irrelevant");
  };

  auto set_base_features = [&](Row& row) {
    std::string folded_title = norm.fold(row.title);
    bool title_hit = !alias_idx.find(folded_title, 5).empty();
    row.features["id_hits"] = row.alias_hits > 0 ? 1.0 + std::log(static_cast<double>(row.alias_hits)) : 0.0;
    row.features["class_diversity"] = static_cast<double>(std::min<std::size_t>(row.alias_projects.size(), 5));
    row.features["version_mention"] = row.version_hits > 0 ? 1.0 : 0.0;
    row.features["code_evidence"] = row.code_hits ? 1.0 : 0.0;
    row.features["title"] = title_hit ? 1.0 : 0.0;
    row.features["neg_context"] = static_cast<double>(row.trap_hits);
    row.features["project_member"] = 0.0;  // set by the linking pass below
    row.features["link"] = 0.0;            // set by the linking pass below
    row.features["date_prior"] = 0.0;
  };
  for (auto& row : rows) set_base_features(row);
  recompute_bm25();
  for (auto& row : rows) compute_linear_and_label(row);

  // Vocabulary expansion: candidate terms are the top-K terms of the
  // VERIFIED relevant set (pass 0 already confirmed their alias hits against
  // re-read bytes); lift = (df_rel/|R|) / (df/N) over the whole corpus.
  for (int pass = 1; pass <= max_passes; ++pass) {
    if (cancel && cancel->cancelled()) break;
    std::vector<int> relevant_idx;
    for (std::size_t i = 0; i < rows.size(); ++i) {
      if (rows[i].label == "relevant") relevant_idx.push_back(static_cast<int>(i));
    }
    if (relevant_idx.empty()) break;
    std::unordered_map<std::string, int> df_rel;
    std::unordered_map<std::string, std::vector<std::string>> evidence;
    for (int i : relevant_idx) {
      std::set<std::string> seen_in_unit;
      for (auto& [term, tf] : rows[static_cast<std::size_t>(i)].sketch.top_terms) {
        if (!seen_in_unit.insert(term).second) continue;
        ++df_rel[term];
        auto& ev = evidence[term];
        if (ev.size() < 5) ev.push_back(rows[static_cast<std::size_t>(i)].unit.unit.id);
      }
    }
    std::vector<std::tuple<double, std::string, int, int>> candidates;  // lift, term, df_rel, df_all
    for (auto& [term, dr] : df_rel) {
      if (dr < expansion_min_df_rel || known_self_terms.count(term)) continue;
      int df_all = 0;
      for (auto& row : rows) {
        if (row.sketch.contains(term)) ++df_all;
      }
      if (df_all <= 0) continue;
      double lift = (static_cast<double>(dr) / relevant_idx.size()) / (static_cast<double>(df_all) / N);
      if (lift >= expansion_min_lift) candidates.emplace_back(lift, term, dr, df_all);
    }
    if (candidates.empty()) break;
    std::sort(candidates.begin(), candidates.end(), [](auto& a, auto& b) { return std::get<0>(a) > std::get<0>(b); });
    if (static_cast<int>(candidates.size()) > expansion_max_terms) candidates.resize(static_cast<std::size_t>(expansion_max_terms));
    for (auto& [lift, term, dr, df_all] : candidates) {
      self_terms.push_back(QueryTerm{term, expansion_weight});
      known_self_terms.insert(term);
      Json ev = Json::array();
      for (auto& e : evidence[term]) ev.push_back(e);
      expanded_terms_log.push_back(Json{{"term", term},
                                        {"pass", pass},
                                        {"lift", std::round(lift * 100.0) / 100.0},
                                        {"df_relevant", dr},
                                        {"df_corpus", df_all},
                                        {"evidence", ev},
                                        {"reason", "lift >= " + std::to_string(expansion_min_lift) +
                                                       " against the verified-relevant set (vocabulary expansion)"}});
    }
    recompute_bm25();
    for (auto& row : rows) compute_linear_and_label(row);
  }

  // ── Linking pass: same project/gizmo, MinHash continuation, shared rare
  // identifiers, same-session -- one damped propagation step. ────────────
  {
    auto lk = rt_.db().lock();
    LOOM_TRY(rt_.db().conn().run("DELETE FROM loom_cat_links WHERE run_id = ?", run_id));
  }
  // idf per corpus term (top-K union) for the "shared rare identifiers" test.
  std::unordered_map<std::string, int> term_df;
  for (auto& row : rows) {
    std::set<std::string> seen;
    for (auto& [t, _] : row.sketch.top_terms) {
      if (seen.insert(t).second) ++term_df[t];
    }
  }
  double rare_idf_floor = N > 20 ? std::log(static_cast<double>(N) / 20.0) : 0.0;

  struct LinkRow { int i, j; std::string type; double strength; };
  std::vector<LinkRow> links;
  for (int i = 0; i < N; ++i) {
    for (int j = i + 1; j < N; ++j) {
      Row& a = rows[static_cast<std::size_t>(i)];
      Row& b = rows[static_cast<std::size_t>(j)];
      // (a) same project/gizmo.
      if (!a.unit.project_ext_id.empty() && a.unit.project_ext_id == b.unit.project_ext_id) {
        links.push_back({i, j, "same_project", 1.0});
      }
      // (b) continuation: MinHash Jaccard of the sketches.
      double jac = a.sketch.minhash.jaccard(b.sketch.minhash);
      if (jac >= link_min_jaccard) links.push_back({i, j, "continuation", jac});
      // (c) shared rare identifiers.
      if (a.unit.platform == b.unit.platform) {
        int shared_rare = 0;
        std::set<std::string> a_terms;
        for (auto& [t, _] : a.sketch.top_terms) a_terms.insert(t);
        for (auto& [t, _] : b.sketch.top_terms) {
          auto dfit = term_df.find(t);
          double idf = dfit == term_df.end() ? 0.0 : std::log(1.0 + (static_cast<double>(N) - dfit->second + 0.5) / (dfit->second + 0.5));
          if (a_terms.count(t) && idf > rare_idf_floor) ++shared_rare;
        }
        if (shared_rare >= link_shared_rare) links.push_back({i, j, "shared_rare", std::min(1.0, shared_rare / 10.0)});
      }
      // (d) same session: same platform, close in time.
      if (a.unit.platform == b.unit.platform && !a.date.empty() && !b.date.empty()) {
        auto ta = timeutil::parse_iso_utc(a.date), tb = timeutil::parse_iso_utc(b.date);
        if (ta && tb) {
          double hours = std::abs(std::chrono::duration<double>(*ta - *tb).count()) / 3600.0;
          if (hours <= link_same_session_hours) links.push_back({i, j, "same_session", 1.0 - hours / (link_same_session_hours + 1)});
        }
      }
    }
  }
  {
    auto lk = rt_.db().lock();
    for (auto& l : links) {
      LOOM_TRY(rt_.db().conn().run("INSERT OR REPLACE INTO loom_cat_links (run_id, src, dst, link_type, strength) VALUES (?,?,?,?,?)",
                                   run_id, rows[static_cast<std::size_t>(l.i)].unit.unit.id,
                                   rows[static_cast<std::size_t>(l.j)].unit.unit.id, l.type, l.strength));
    }
  }
  // Project-member feature: any same_project link at all.
  std::vector<double> best_strength(static_cast<std::size_t>(N), 0.0);
  std::vector<bool> has_project_link(static_cast<std::size_t>(N), false);
  for (auto& l : links) {
    best_strength[static_cast<std::size_t>(l.i)] = std::max(best_strength[static_cast<std::size_t>(l.i)], l.strength);
    best_strength[static_cast<std::size_t>(l.j)] = std::max(best_strength[static_cast<std::size_t>(l.j)], l.strength);
    if (l.type == "same_project") {
      has_project_link[static_cast<std::size_t>(l.i)] = true;
      has_project_link[static_cast<std::size_t>(l.j)] = true;
    }
  }
  // One damped propagation step: link only helps a unit that already has its
  // own evidence (alias hit or non-trivial BM25), so it cannot single-handedly
  // pull in a unit with zero own signal (proposal_scale.md §5.5).
  std::vector<double> pre_link_score(static_cast<std::size_t>(N));
  for (int i = 0; i < N; ++i) pre_link_score[static_cast<std::size_t>(i)] = rows[static_cast<std::size_t>(i)].score;
  for (auto& l : links) {
    Row& a = rows[static_cast<std::size_t>(l.i)];
    Row& b = rows[static_cast<std::size_t>(l.j)];
    bool a_has_own = a.alias_hits > 0 || json::get_number(a.features, "bm25_self", 0.0) > 0.05;
    bool b_has_own = b.alias_hits > 0 || json::get_number(b.features, "bm25_self", 0.0) > 0.05;
    if (a_has_own) {
      double v = link_damping * pre_link_score[static_cast<std::size_t>(l.j)] * l.strength;
      if (v > json::get_number(a.features, "link", 0.0)) a.features["link"] = v;
    }
    if (b_has_own) {
      double v = link_damping * pre_link_score[static_cast<std::size_t>(l.i)] * l.strength;
      if (v > json::get_number(b.features, "link", 0.0)) b.features["link"] = v;
    }
  }
  for (int i = 0; i < N; ++i) rows[static_cast<std::size_t>(i)].features["project_member"] = has_project_link[static_cast<std::size_t>(i)] ? 1.0 : 0.0;
  for (auto& row : rows) compute_linear_and_label(row);

  // ── Persist scores ───────────────────────────────────────────────────
  std::int64_t n_relevant = 0, n_candidate = 0, n_irrelevant = 0, n_traps = 0;
  {
    auto lk = rt_.db().lock();
    for (auto& row : rows) {
      if (row.label == "relevant") ++n_relevant; else if (row.label == "candidate") ++n_candidate; else ++n_irrelevant;
      if (row.alias_hits == 0 && row.trap_hits > 0) ++n_traps;
      std::vector<std::string> projects(row.alias_projects.begin(), row.alias_projects.end());
      Json projs = Json::array();
      for (auto& p : projects) projs.push_back(p);
      std::string trap = (row.alias_hits == 0 && row.trap_hits > 0) ? row.trap_reason : "";
      LOOM_TRY(rt_.db().conn().run(
          "INSERT OR REPLACE INTO loom_cat_scores (run_id, unit_id, score, label, features, reasons, projects, trap) "
          "VALUES (?,?,?,?,?,?,?,?)",
          run_id, row.unit.unit.id, row.score, row.label, json::dump(row.features), json::dump(row.reasons), json::dump(projs), trap));
    }
  }

  return Json{{"run_id", run_id},
              {"relevant", n_relevant},
              {"candidate", n_candidate},
              {"irrelevant", n_irrelevant},
              {"traps", n_traps},
              {"expanded_terms", expanded_terms_log},
              {"links", static_cast<std::int64_t>(links.size())}};
}

}  // namespace loom::catalog
