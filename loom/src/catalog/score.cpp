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
#include <memory>
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
  // Keep the existing score transform while exposing the distinct channels.
  // alias_hits is LEGACY combined alias+principle evidence, not identity alone.
  int alias_hits = 0, identity_alias_hits = 0, principle_hits = 0;
  int trap_hits = 0, version_hits = 0, code_hits = 0;
  bool identity_source_available = false;
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
// each token), never folded surfaces -- so every profile term, including
// a single inflected word or punctuation-separated identifier, must be
// split into its own content-word keys the same way
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
    int added = 0;
    for (auto& tok : norm.tokens(surface.empty() ? key : surface)) {
      if (norm.is_stopword(tok)) continue;
      std::string mk = norm.match_key(tok);
      if (mk.empty() || !seen.insert(mk).second) continue;
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
  kb::Normalizer norm(*pack_);
  AliasIndex alias_idx = AliasIndex::from_profile(profile, alias_context_window_tokens(*pack_));
  alias_idx.enable_inflection(norm, *pack_);

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
  // Evidence channels (R25/R27): every feature belongs to exactly one channel.
  // The lexical and semantic channels are also scored on their own (shadow
  // scores) so each can audit the other; the final score fuses all of them
  // additively in log-odds space, so no channel can veto another.
  std::map<std::string, std::string> channel_of;
  if (const Json* ch = json::find(relevance, "channels"); ch && ch->is_object()) {
    for (auto it = ch->begin(); it != ch->end(); ++it) {
      if (!it.value().is_array()) continue;
      for (const auto& f : it.value()) {
        if (f.is_string()) channel_of[f.get<std::string>()] = it.key();
      }
    }
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
  // Semantic (profile-vector) channel; policy lives in thresholds.json "catalog".
  int sem_ngram = 4, sem_min_seeds = 2, sem_passes = 2;
  int link_exact_max_units = 2000, link_session_max_neighbours = 32, link_project_max_clique = 32;
  double link_session_similarity_ref = 0.15;
  int link_rounds = 2;
  double consensus_min_lex = 0.15, consensus_min_sem = 0.3, consensus_min_link = 0.15;
  double sem_floor_pct = 10.0, sem_ref_pct = 50.0, sem_cap = 1.5, sem_self_doc_weight = 0.5;
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
    sem_ngram = static_cast<int>(json::get_int(*cat, "sem_ngram", sem_ngram));
    sem_floor_pct = json::get_number(*cat, "sem_floor_percentile", sem_floor_pct);
    sem_ref_pct = json::get_number(*cat, "sem_ref_percentile", sem_ref_pct);
    sem_cap = json::get_number(*cat, "sem_cap", sem_cap);
    sem_min_seeds = static_cast<int>(json::get_int(*cat, "sem_min_seeds", sem_min_seeds));
    sem_passes = static_cast<int>(json::get_int(*cat, "sem_passes", sem_passes));
    sem_self_doc_weight = json::get_number(*cat, "sem_self_doc_weight", sem_self_doc_weight);
    link_exact_max_units = static_cast<int>(json::get_int(*cat, "link_exact_max_units", link_exact_max_units));
    link_session_max_neighbours = static_cast<int>(json::get_int(*cat, "link_session_max_neighbours", link_session_max_neighbours));
    link_project_max_clique = static_cast<int>(json::get_int(*cat, "link_project_max_clique", link_project_max_clique));
    link_session_similarity_ref = json::get_number(*cat, "link_session_similarity_ref", link_session_similarity_ref);
    consensus_min_lex = json::get_number(*cat, "consensus_min_lexical", consensus_min_lex);
    consensus_min_sem = json::get_number(*cat, "consensus_min_semantic", consensus_min_sem);
    consensus_min_link = json::get_number(*cat, "consensus_min_link", consensus_min_link);
    link_rounds = std::max(1, static_cast<int>(json::get_int(*cat, "link_rounds", link_rounds)));
  }
  int max_passes = cfg.max_passes > 0 ? std::min(cfg.max_passes, max_expansion_passes) : max_expansion_passes;

  Json fp{{"profile", profile.input_hash}, {"pack", pack_->hash()}, {"cfg", cfg.to_json()},
          {"scoring_evidence_version", 3}};
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

    // Stored mentions do not retain the full local context or the profile
    // that licensed them. If neither original nor retained bytes can be read,
    // do not present stale scan-time identity/principle hits as verified.
    Json mentions = Json::array();
    if (auto raw = read_unit(cu.unit.id)) {
      row.identity_source_available = true;
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
        if (kind == "alias") ++row.identity_alias_hits; else ++row.principle_hits;
        std::string key = json::get_string(mj, "key");
        if (!key.empty()) row.alias_projects.insert(key);
      } else if (kind == "version") {
        // Version evidence always needs a nearby accepted identity, even if
        // future mention producers supply their own version annotations.
        auto offset = json::get_int(mj, "offset", -1);
        if (offset < 0) continue;
        for (const auto& anchor : mentions) {
          if (json::get_string(anchor, "kind") != "alias" || json::get_bool(anchor, "trap")) continue;
          auto anchor_offset = json::get_int(anchor, "offset", -1);
          if (anchor_offset < 0) continue;
          auto distance = anchor_offset > offset ? anchor_offset - offset : offset - anchor_offset;
          if (distance <= 80) {
            ++row.version_hits;
            break;
          }
        }
      }
    }
    // Legacy code_evidence also uses the combined channel; no weight/model
    // recalibration is implied by adding the diagnostic channel counts.
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

  auto round4 = [](double x) { return std::round(x * 1e4) / 1e4; };
  auto compute_linear_and_label = [&](Row& row) {
    // Independent-evidence agreement (R27): 1 when the lexical, the semantic
    // and the structural (link) channel each carry substantive evidence of
    // their own for this unit. Each channel alone stays below the relevant
    // band for these units; three independent channels agreeing is more
    // than any one of them, and units with a channel missing get nothing.
    {
      double lex_e = std::max(json::get_number(row.features, "bm25_self", 0.0), json::get_number(row.features, "bm25_phil", 0.0));
      double sem_e = std::max(json::get_number(row.features, "sem_word", 0.0), json::get_number(row.features, "sem_ngram", 0.0));
      double lnk_e = json::get_number(row.features, "link", 0.0);
      row.features["consensus"] = (lex_e >= consensus_min_lex && sem_e >= consensus_min_sem && lnk_e >= consensus_min_link) ? 1.0 : 0.0;
    }
    double linear = bias;
    double lexical_linear = bias, semantic_linear = bias;
    Json reasons = Json::array();
    for (auto& [k, w] : weights) {
      double f = json::get_number(row.features, k, 0.0);
      double contribution = w * f;
      linear += contribution;
      auto ch = channel_of.find(k);
      const std::string channel = ch == channel_of.end() ? "lexical" : ch->second;
      if (channel == "lexical") lexical_linear += contribution;
      else if (channel == "semantic") semantic_linear += contribution;
      if (std::abs(contribution) > 1e-9) {
        reasons.push_back(Json{{"feature", k}, {"channel", channel}, {"contribution", round4(contribution)}, {"evidence", f}});
      }
    }
    if (!date_from.empty() && !row.date.empty() && row.date < date_from) {
      linear += date_penalty;
      reasons.push_back(Json{{"feature", "date_prior"}, {"channel", "structure"}, {"contribution", date_penalty}, {"evidence", row.date}});
    }
    std::stable_sort(reasons.begin(), reasons.end(),
             [](const Json& a, const Json& b) { return std::abs(a["contribution"].get<double>()) > std::abs(b["contribution"].get<double>()); });
    row.reasons = reasons;
    row.features["lexical_score"] = round4(sigmoid(lexical_linear));
    row.features["semantic_score"] = round4(sigmoid(semantic_linear));
    row.score = round4(sigmoid(linear));
    row.label = row.score >= tau_relevant ? "relevant" : (row.score >= tau_candidate ? "candidate" : "irrelevant");
  };

  auto set_base_features = [&](Row& row) {
    std::string folded_title = norm.fold(row.title);
    auto title_mentions = alias_idx.find(folded_title, 5);
    bool title_hit = std::any_of(title_mentions.begin(), title_mentions.end(), [](const Mention& m) { return !m.trap; });
    row.features["identity_alias_hits"] = row.identity_alias_hits;
    row.features["principle_hits"] = row.principle_hits;
    row.features["identity_source_available"] = row.identity_source_available ? 1.0 : 0.0;
    // id_hits and class_diversity retain their preexisting combined score
    // semantics. The new raw counts have no bundled policy weights.
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

  // ── Semantic channel (R25/R27): cosine of every unit to profile vectors
  // built from the confidently identified units (seeds) and the self-profile
  // terms, in a word-stem space and a character n-gram space. Bounded,
  // deterministic enrichment: units that reach the relevant band join the
  // profile for the next pass (at most `sem_passes`). This channel is fused
  // additively with the lexical one -- it neither gates nor is gated. ─────
  Json semantic_report{{"available", false}, {"passes", 0}};
  std::unique_ptr<SemanticSpace> space;
  {
    std::vector<const Sketch*> sketches;
    sketches.reserve(rows.size());
    for (const auto& r : rows) sketches.push_back(&r.sketch);
    if (sem_passes > 0 && N >= 4) space = std::make_unique<SemanticSpace>(sketches, sem_ngram);
  }
  auto has_semantic_evidence = [](const Row& row) {
    return json::get_number(row.features, "sem_word", 0.0) > 0.05 || json::get_number(row.features, "sem_ngram", 0.0) > 0.05;
  };
  if (space) {
    std::vector<std::pair<std::string, double>> self_pseudo;
    for (const auto& qt : self_terms) self_pseudo.emplace_back(qt.term, qt.weight);
    std::vector<double> seed_w(static_cast<std::size_t>(N), 0.0);
    for (int i = 0; i < N; ++i) {
      if (rows[static_cast<std::size_t>(i)].label != "irrelevant") seed_w[static_cast<std::size_t>(i)] = rows[static_cast<std::size_t>(i)].score;
    }
    for (int pass = 1; pass <= sem_passes; ++pass) {
      if (cancel && cancel->cancelled()) break;
      SemanticProfile prof = space->new_profile();
      int seeds = 0;
      double mass = 0.0;
      for (int i = 0; i < N; ++i) {
        double w = seed_w[static_cast<std::size_t>(i)];
        if (w <= 0.0) continue;
        space->add_unit(prof, static_cast<std::size_t>(i), w);
        ++seeds;
        mass += w;
      }
      if (seeds < sem_min_seeds) {
        semantic_report["reason"] = "fewer than " + std::to_string(sem_min_seeds) + " confidently identified units";
        break;
      }
      space->add_terms(prof, self_pseudo, sem_self_doc_weight * mass / seeds);
      space->finish(prof);
      std::vector<ChannelCosine> cs(static_cast<std::size_t>(N));
      std::vector<double> seed_word, seed_gram, other_word, other_gram;
      for (int i = 0; i < N; ++i) {
        std::size_t u = static_cast<std::size_t>(i);
        cs[u] = space->cosine(u, prof, seed_w[u]);
        (seed_w[u] > 0.0 ? seed_word : other_word).push_back(cs[u].word);
        (seed_w[u] > 0.0 ? seed_gram : other_gram).push_back(cs[u].gram);
      }
      // Contrast normalisation: 0 = the background level of the corpus,
      // 1 = a typical confidently identified unit (measured leave-one-out).
      const double floor_w = percentile_of(other_word, sem_floor_pct), ref_w = percentile_of(seed_word, sem_ref_pct);
      const double floor_g = percentile_of(other_gram, sem_floor_pct), ref_g = percentile_of(seed_gram, sem_ref_pct);
      auto contrast = [&](double c, double floor, double ref) {
        return ref - floor > 1e-9 ? std::clamp((c - floor) / (ref - floor), 0.0, sem_cap) : 0.0;
      };
      for (int i = 0; i < N; ++i) {
        Row& row = rows[static_cast<std::size_t>(i)];
        row.features["sem_word"] = round4(contrast(cs[static_cast<std::size_t>(i)].word, floor_w, ref_w));
        row.features["sem_ngram"] = round4(contrast(cs[static_cast<std::size_t>(i)].gram, floor_g, ref_g));
        row.features["sem_cos_word"] = round4(cs[static_cast<std::size_t>(i)].word);
        row.features["sem_cos_ngram"] = round4(cs[static_cast<std::size_t>(i)].gram);
        compute_linear_and_label(row);
      }
      semantic_report = Json{{"available", true},
                             {"passes", pass},
                             {"seeds", seeds},
                             {"method", "tfidf_cosine(word_stems)+tfidf_cosine(char_" + std::to_string(sem_ngram) + "grams)"},
                             {"floor_word", round4(floor_w)}, {"ref_word", round4(ref_w)},
                             {"floor_gram", round4(floor_g)}, {"ref_gram", round4(ref_g)},
                             {"word_vocabulary", static_cast<std::int64_t>(space->word_vocabulary())},
                             {"gram_vocabulary", static_cast<std::int64_t>(space->gram_vocabulary())}};
      bool changed = false;
      for (int i = 0; i < N; ++i) {
        const Row& row = rows[static_cast<std::size_t>(i)];
        double w = row.label != "irrelevant" ? row.score : 0.0;
        if ((w > 0.0) != (seed_w[static_cast<std::size_t>(i)] > 0.0)) changed = true;
        seed_w[static_cast<std::size_t>(i)] = w;
      }
      if (!changed) break;
    }
  }

  // ── Linking pass: same project/gizmo, MinHash continuation, shared rare
  // identifiers, same-session -- one damped propagation step. Candidate
  // generation (links.cpp) replaces the former all-pairs scan; content is
  // never re-read here. ─────────────────────────────────────────────────
  {
    auto lk = rt_.db().lock();
    LOOM_TRY(rt_.db().conn().run("DELETE FROM loom_cat_links WHERE run_id = ?", run_id));
  }
  std::vector<LinkUnit> link_units(static_cast<std::size_t>(N));
  for (int i = 0; i < N; ++i) {
    const Row& row = rows[static_cast<std::size_t>(i)];
    LinkUnit& lu = link_units[static_cast<std::size_t>(i)];
    lu.sketch = &row.sketch;
    lu.platform = row.unit.platform;
    lu.project = row.unit.project_ext_id;
    if (!row.date.empty()) {
      if (auto t = timeutil::parse_iso_utc(row.date)) {
        lu.time = std::chrono::duration<double>(t->time_since_epoch()).count();
      }
    }
  }
  LinkParams link_params;
  link_params.min_jaccard = link_min_jaccard;
  link_params.shared_rare_min = link_shared_rare;
  link_params.session_hours = link_same_session_hours;
  link_params.exact_max_units = static_cast<std::size_t>(std::max(0, link_exact_max_units));
  link_params.session_max_neighbours = link_session_max_neighbours;
  link_params.project_max_clique = link_project_max_clique;
  LinkResult link_result = build_links(link_units, link_params);
  const std::vector<Link>& links = link_result.links;

  std::vector<bool> has_project_link(static_cast<std::size_t>(N), false);
  for (const auto& l : links) {
    if (l.type == "same_project") {
      has_project_link[static_cast<std::size_t>(l.i)] = true;
      has_project_link[static_cast<std::size_t>(l.j)] = true;
    }
  }
  for (const auto& g : link_result.project_groups) {
    for (int m : g) has_project_link[static_cast<std::size_t>(m)] = true;
  }
  // One damped propagation step: link only helps a unit that already has its
  // own evidence (identity, lexical or semantic), so it cannot single-handedly
  // pull in a unit with zero own signal (proposal_scale.md §5.5). A
  // same_session link is corroborated by content: its strength is scaled by
  // the similarity of the two units, so a nearby timestamp alone is worth 0.
  std::vector<double> pre_link_score(static_cast<std::size_t>(N));
  std::vector<double> link_value(static_cast<std::size_t>(N), 0.0);
  std::int64_t uncorroborated_sessions = 0;
  for (int round = 0; round < link_rounds; ++round) {
  for (int i = 0; i < N; ++i) pre_link_score[static_cast<std::size_t>(i)] = rows[static_cast<std::size_t>(i)].score;
  std::fill(link_value.begin(), link_value.end(), 0.0);
  uncorroborated_sessions = 0;
  auto has_own_evidence = [&](const Row& row) {
    return row.alias_hits > 0 || json::get_number(row.features, "bm25_self", 0.0) > 0.05 ||
           json::get_number(row.features, "bm25_phil", 0.0) > 0.05 || has_semantic_evidence(row);
  };
  auto offer = [&](int to, int from, double strength) {
    if (!has_own_evidence(rows[static_cast<std::size_t>(to)])) return;
    double v = link_damping * pre_link_score[static_cast<std::size_t>(from)] * strength;
    if (v > link_value[static_cast<std::size_t>(to)]) link_value[static_cast<std::size_t>(to)] = v;
  };
  for (const auto& l : links) {
    double strength = l.strength;
    if (l.type == "same_session" && space) {
      double sim = space->similarity(static_cast<std::size_t>(l.i), static_cast<std::size_t>(l.j));
      double corroboration = std::clamp(sim / std::max(1e-9, link_session_similarity_ref), 0.0, 1.0);
      if (corroboration <= 0.0) ++uncorroborated_sessions;
      strength *= corroboration;
    }
    offer(l.i, l.j, strength);
    offer(l.j, l.i, strength);
  }
  // Large project groups: the maximum over the other members, exactly as
  // the clique would give, from the two best pre-link scores.
  for (const auto& g : link_result.project_groups) {
    int top1 = -1, top2 = -1;
    for (int m : g) {
      if (top1 < 0 || pre_link_score[static_cast<std::size_t>(m)] > pre_link_score[static_cast<std::size_t>(top1)]) {
        top2 = top1;
        top1 = m;
      } else if (top2 < 0 || pre_link_score[static_cast<std::size_t>(m)] > pre_link_score[static_cast<std::size_t>(top2)]) {
        top2 = m;
      }
    }
    for (int m : g) offer(m, m == top1 ? top2 : top1, 1.0);
  }
  for (int i = 0; i < N; ++i) {
    Row& row = rows[static_cast<std::size_t>(i)];
    row.features["link"] = round4(link_value[static_cast<std::size_t>(i)]);
    row.features["project_member"] = has_project_link[static_cast<std::size_t>(i)] ? 1.0 : 0.0;
  }
  for (auto& row : rows) compute_linear_and_label(row);
  }
  {
    auto lk = rt_.db().lock();
    for (const auto& l : links) {
      LOOM_TRY(rt_.db().conn().run("INSERT OR REPLACE INTO loom_cat_links (run_id, src, dst, link_type, strength) VALUES (?,?,?,?,?)",
                                   run_id, rows[static_cast<std::size_t>(l.i)].unit.unit.id,
                                   rows[static_cast<std::size_t>(l.j)].unit.unit.id, l.type, l.strength));
    }
    // Aggregated project groups: each member points at the group's four
    // best pre-link members (a bounded stand-in for the clique).
    for (const auto& g : link_result.project_groups) {
      std::vector<int> best = g;
      std::stable_sort(best.begin(), best.end(), [&](int a, int b) {
        return pre_link_score[static_cast<std::size_t>(a)] > pre_link_score[static_cast<std::size_t>(b)];
      });
      if (best.size() > 4) best.resize(4);
      for (int m : g) {
        for (int hub : best) {
          if (hub == m) continue;
          LOOM_TRY(rt_.db().conn().run("INSERT OR REPLACE INTO loom_cat_links (run_id, src, dst, link_type, strength) VALUES (?,?,?,?,?)",
                                       run_id, rows[static_cast<std::size_t>(m)].unit.unit.id,
                                       rows[static_cast<std::size_t>(hub)].unit.unit.id, "same_project", 1.0));
        }
      }
    }
  }
  link_result.stats["uncorroborated_sessions"] = uncorroborated_sessions;
  for (auto& row : rows) compute_linear_and_label(row);

  // ── Persist scores ───────────────────────────────────────────────────
  std::int64_t n_relevant = 0, n_candidate = 0, n_irrelevant = 0, n_traps = 0, n_identity_unavailable = 0;
  // Shadow audit between the independent channels (R27): the elements one
  // channel puts in the relevant band and the other does not.
  Json channel_stats;
  {
    std::int64_t lex = 0, sem = 0, both = 0, lex_only = 0, sem_only = 0;
    for (const auto& row : rows) {
      bool l = json::get_number(row.features, "lexical_score", 0.0) >= tau_relevant;
      bool m = json::get_number(row.features, "semantic_score", 0.0) >= tau_relevant;
      lex += l; sem += m; both += (l && m); lex_only += (l && !m); sem_only += (m && !l);
    }
    channel_stats = Json{{"lexical_relevant", lex}, {"semantic_relevant", sem}, {"both", both},
                         {"lexical_only", lex_only}, {"semantic_only", sem_only}};
  }
  {
    auto lk = rt_.db().lock();
    for (auto& row : rows) {
      if (row.label == "relevant") ++n_relevant; else if (row.label == "candidate") ++n_candidate; else ++n_irrelevant;
      if (row.alias_hits == 0 && row.trap_hits > 0) ++n_traps;
      if (!row.identity_source_available) ++n_identity_unavailable;
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
              {"scoring_evidence_version", 3},
              {"legacy_combined_features", Json::array({"id_hits", "class_diversity", "code_evidence"})},
              {"identity_unavailable", n_identity_unavailable},
              {"relevant", n_relevant},
              {"candidate", n_candidate},
              {"irrelevant", n_irrelevant},
              {"traps", n_traps},
              {"expanded_terms", expanded_terms_log},
              {"semantic", semantic_report},
              {"channels", channel_stats},
              {"links", static_cast<std::int64_t>(links.size())},
              {"link_stats", link_result.stats}};
}

}  // namespace loom::catalog
