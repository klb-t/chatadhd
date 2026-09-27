// catalog.h: Catalog::score — turns each unit's mentions into an explainable
// feature vector, combined by the logistic model of policy/relevance.json
// (proposal_scale.md §5.3).
//
// A self-profile is normally rebuilt (new project aliases, expansion terms)
// AFTER units have already been scanned, so score() re-derives mentions
// against the ACTIVE profile by re-reading each unit's exact bytes
// (read_unit(), the same verified path preview()/import_selected() use)
// instead of trusting the mentions baked in at scan time against whatever
// profile existed then. This is the most significant disclosed
// simplification versus the design: proposal_scale.md's sketch-only BM25
// pass (§5.2) is what should make re-scoring against a new profile NOT need
// raw bytes at all (bounded by sketch size, not corpus size); re-reading
// every unit here is correct but does not scale to a multi-GB corpus the way
// the design intends (a real zip member gets re-decompressed once per unit
// that lives in it) -- true sketch-token matching is future work. When a
// source has moved/vanished, score() falls back to the scan-time mentions
// (still computed against the full pack-derived alias index at scan time)
// rather than failing the whole run.
//
// No corpus-wide BM25 pass and no vocabulary-expansion pass (5.5/5.6) are
// implemented, so `bm25_self`/`bm25_phil`/`project_member`/`link` are always
// 0 and `score()`'s `expanded_terms` is always empty.
#include "loom/catalog.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

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

struct Row {
  CatalogUnit unit;
  std::string title;
  std::string date;
};

// Re-derives the prose of a unit from its exact re-read bytes, the same way
// scan.cpp's ingest_unit did the first time, so mentions can be recomputed
// against a profile built after the scan (see the file header comment).
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
  const Json& thresholds = pack_->policy("thresholds");
  double tau_relevant = 0.7, tau_candidate = 0.35;
  if (const Json* cat = json::find(thresholds, "catalog"); cat && cat->is_object()) {
    tau_relevant = json::get_number(*cat, "tau_relevant", tau_relevant);
    tau_candidate = json::get_number(*cat, "tau_candidate", tau_candidate);
  }

  Json fp{{"profile", profile.input_hash}, {"pack", pack_->hash()}, {"cfg", cfg.to_json()}};
  std::string run_id = "run_" + Sha256::hex(json::canonical(fp)).substr(0, 16);

  std::vector<Row> rows;
  {
    auto lk = rt_.db().lock();
    LOOM_TRY_ASSIGN(sql::Stmt st, rt_.db().conn().prepare("SELECT body FROM loom_cat_units ORDER BY id"));
    while (true) {
      LOOM_TRY_ASSIGN(bool has, st.step());
      if (!has) break;
      auto j = json::parse(st.get_text(0));
      if (!j) continue;
      auto cu = CatalogUnit::from_json(*j);
      if (!cu) continue;
      std::string title = cu->unit.title, date = cu->unit.date;
      rows.push_back(Row{std::move(*cu), std::move(title), std::move(date)});
    }
  }

  std::int64_t n_relevant = 0, n_candidate = 0, n_irrelevant = 0, n_traps = 0;
  std::int64_t idx_i = 0;
  for (auto& row : rows) {
    ++idx_i;
    if (cancel && cancel->cancelled()) break;
    if (progress) progress("score", idx_i, static_cast<std::int64_t>(rows.size()), row.unit.unit.id);
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

    std::set<std::string> alias_projects;
    int alias_hits = 0, trap_hits = 0, version_hits = 0, code_hits = 0;
    std::string trap_reason;
    for (const auto& mj : mentions) {
      std::string kind = json::get_string(mj, "kind");
      bool trap = json::get_bool(mj, "trap");
      if (trap) {
        ++trap_hits;
        if (trap_reason.empty()) trap_reason = json::get_string(mj, "trap_reason");
        continue;
      }
      if (kind == "alias" || kind == "principle") {
        ++alias_hits;
        std::string key = json::get_string(mj, "key");
        if (!key.empty()) alias_projects.insert(key);
      } else if (kind == "version") {
        ++version_hits;
      }
    }
    if (cu.n_code_chars > 0 && alias_hits > 0) code_hits = 1;

    std::string folded_title = norm.fold(row.title);
    bool title_hit = !alias_idx.find(folded_title, 5).empty();

    Json features{
        // A single confirmed hit already establishes identity (the whole
        // point of the identity pass, proposal_scale.md §5.1): the feature
        // starts at 1.0 for one hit and grows slowly (log) after that,
        // rather than log(1+n) which under-weights exactly-one-mention
        // units (common for a chat that names the project once up top and
        // refers to it by pronoun afterwards).
        {"id_hits", alias_hits > 0 ? 1.0 + std::log(static_cast<double>(alias_hits)) : 0.0},
        {"class_diversity", std::min<std::size_t>(alias_projects.size(), 5)},
        {"version_mention", version_hits > 0 ? 1.0 : 0.0},
        {"code_evidence", code_hits ? 1.0 : 0.0},
        {"title", title_hit ? 1.0 : 0.0},
        {"neg_context", static_cast<double>(trap_hits)},
        {"bm25_self", 0.0},
        {"bm25_phil", 0.0},
        {"project_member", 0.0},
        {"link", 0.0},
        {"date_prior", 0.0},
    };

    double linear = bias;
    Json reasons = Json::array();
    for (auto& [k, w] : weights) {
      double f = json::get_number(features, k, 0.0);
      double contribution = w * f;
      linear += contribution;
      if (std::abs(contribution) > 1e-9) {
        reasons.push_back(Json{{"feature", k}, {"contribution", contribution}, {"evidence", f}});
      }
    }
    if (!date_from.empty() && !row.date.empty() && row.date < date_from) {
      linear += date_penalty;
      reasons.push_back(Json{{"feature", "date_prior"}, {"contribution", date_penalty}, {"evidence", row.date}});
    }
    std::sort(reasons.begin(), reasons.end(), [](const Json& a, const Json& b) {
      return std::abs(a["contribution"].get<double>()) > std::abs(b["contribution"].get<double>());
    });

    double score = std::round(sigmoid(linear) * 1e4) / 1e4;
    std::string label = score >= tau_relevant ? "relevant" : (score >= tau_candidate ? "candidate" : "irrelevant");
    if (label == "relevant") ++n_relevant; else if (label == "candidate") ++n_candidate; else ++n_irrelevant;
    if (alias_hits == 0 && trap_hits > 0) ++n_traps;

    UnitScore us;
    us.unit_id = cu.unit.id;
    us.score = score;
    us.label = label;
    us.features = features;
    us.reasons = reasons;
    us.projects.assign(alias_projects.begin(), alias_projects.end());
    us.trap = (alias_hits == 0 && trap_hits > 0) ? trap_reason : "";

    Json projs = Json::array();
    for (auto& p : us.projects) projs.push_back(p);
    auto lk = rt_.db().lock();
    LOOM_TRY(rt_.db().conn().run(
        "INSERT OR REPLACE INTO loom_cat_scores (run_id, unit_id, score, label, features, reasons, projects, trap) "
        "VALUES (?,?,?,?,?,?,?,?)",
        run_id, us.unit_id, us.score, us.label, json::dump(us.features), json::dump(us.reasons), json::dump(projs),
        us.trap));
  }

  return Json{{"run_id", run_id},
              {"relevant", n_relevant},
              {"candidate", n_candidate},
              {"irrelevant", n_irrelevant},
              {"traps", n_traps},
              {"expanded_terms", Json::array()}};
}

}  // namespace loom::catalog
