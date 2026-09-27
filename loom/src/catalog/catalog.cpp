// catalog.h — value-type JSON round trips, the Catalog constructor, and the
// knowledge.catalog stage glue. The methods themselves (scan, build_profile,
// score, select, query, import_selected) are implemented in the sibling
// files of this directory; this file only owns the small (de)serialisers and
// wiring so every catalog_*.cpp stays focused on one concern.
#include "loom/catalog.h"

#include "loom/knowledge.h"
#include "loom/runtime.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom::catalog {

// ── SketchParams ────────────────────────────────────────────────────
SketchParams SketchParams::mobile() {
  SketchParams p;
  p.top_k = 64;
  p.minhash = 32;
  p.access_interval = 16LL << 20;
  return p;
}
Json SketchParams::to_json() const {
  return Json{{"top_k", top_k},         {"bloom_fpr", bloom_fpr}, {"minhash", minhash},
              {"max_mentions", max_mentions}, {"access_interval", access_interval}};
}
Result<SketchParams> SketchParams::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "sketch params: expected an object");
  SketchParams p;
  p.top_k = static_cast<int>(json::get_int(j, "top_k", p.top_k));
  p.bloom_fpr = json::get_number(j, "bloom_fpr", p.bloom_fpr);
  p.minhash = static_cast<int>(json::get_int(j, "minhash", p.minhash));
  p.max_mentions = static_cast<int>(json::get_int(j, "max_mentions", p.max_mentions));
  p.access_interval = json::get_int(j, "access_interval", p.access_interval);
  if (p.top_k <= 0) return Error(Errc::InvalidArgument, "sketch params: top_k must be > 0");
  return p;
}

// ── ScanConfig ──────────────────────────────────────────────────────
Result<ScanConfig> ScanConfig::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "scan config: expected an object");
  ScanConfig c;
  if (const Json* s = json::find(j, "sources"); s && s->is_array()) {
    for (const auto& v : *s) c.sources.push_back(v.get<std::string>());
  }
  if (const Json* sk = json::find(j, "sketch")) {
    LOOM_TRY_ASSIGN(c.sketch, SketchParams::from_json(*sk));
  }
  c.threads = static_cast<int>(json::get_int(j, "threads", 0));
  c.inflight_bytes = json::get_int(j, "inflight_bytes", c.inflight_bytes);
  c.retain_raw = json::get_string(j, "retain_raw", "selected");
  c.force = json::get_bool(j, "force");
  if (c.retain_raw != "selected" && c.retain_raw != "all" && c.retain_raw != "none") {
    return Error(Errc::InvalidArgument, "scan config: retain_raw must be selected|all|none");
  }
  return c;
}
Json ScanConfig::to_json() const {
  Json srcs = Json::array();
  for (auto& s : sources) srcs.push_back(s);
  return Json{{"sources", srcs},         {"sketch", sketch.to_json()}, {"threads", threads},
              {"inflight_bytes", inflight_bytes}, {"retain_raw", retain_raw}, {"force", force}};
}

// ── CatalogUnit ─────────────────────────────────────────────────────
Json CatalogUnit::to_json() const {
  Json attachments_j = Json::array();
  for (auto& a : attachments) attachments_j.push_back(a);
  return Json{{"unit", unit.to_json()},
              {"platform", platform},
              {"ext_id", ext_id},
              {"content_hash", content_hash},
              {"n_msgs", n_msgs},
              {"n_chars", n_chars},
              {"n_code_chars", n_code_chars},
              {"n_forks", n_forks},
              {"attachments", attachments_j},
              {"project_ext_id", project_ext_id},
              {"head", head},
              {"prev_version", prev_version},
              {"mentions", mentions}};
}
Result<CatalogUnit> CatalogUnit::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "catalog unit: expected an object");
  CatalogUnit u;
  if (const Json* un = json::find(j, "unit")) {
    LOOM_TRY_ASSIGN(u.unit, model::Unit::from_json(*un));
  }
  u.platform = json::get_string(j, "platform");
  u.ext_id = json::get_string(j, "ext_id");
  u.content_hash = json::get_string(j, "content_hash");
  u.n_msgs = static_cast<int>(json::get_int(j, "n_msgs"));
  u.n_chars = json::get_int(j, "n_chars");
  u.n_code_chars = json::get_int(j, "n_code_chars");
  u.n_forks = static_cast<int>(json::get_int(j, "n_forks"));
  if (const Json* a = json::find(j, "attachments"); a && a->is_array()) {
    for (const auto& v : *a) u.attachments.push_back(v.get<std::string>());
  }
  u.project_ext_id = json::get_string(j, "project_ext_id");
  u.head = json::get_string(j, "head");
  u.prev_version = json::get_string(j, "prev_version");
  if (const Json* m = json::find(j, "mentions"); m && m->is_array()) u.mentions = *m;
  return u;
}

// ── SelfProfile ─────────────────────────────────────────────────────
Json SelfProfile::to_json() const {
  return Json{{"id", id}, {"input_hash", input_hash}, {"projects", projects}, {"terms", terms}};
}
Result<SelfProfile> SelfProfile::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "self profile: expected an object");
  SelfProfile p;
  p.id = json::get_string(j, "id");
  p.input_hash = json::get_string(j, "input_hash");
  if (const Json* pr = json::find(j, "projects"); pr && pr->is_array()) p.projects = *pr;
  if (const Json* t = json::find(j, "terms"); t && t->is_array()) p.terms = *t;
  return p;
}

// ── ProfileConfig ───────────────────────────────────────────────────
Result<ProfileConfig> ProfileConfig::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "profile config: expected an object");
  ProfileConfig c;
  if (auto s = json::get_opt_string(j, "repo")) c.repo = *s;
  c.git = json::get_bool(j, "git", true);
  if (const Json* d = json::find(j, "documents"); d && d->is_array()) {
    for (const auto& v : *d) c.documents.push_back(v.get<std::string>());
  }
  if (const Json* e = json::find(j, "extra_terms"); e && e->is_array()) {
    for (const auto& v : *e) c.extra_terms.push_back(v.get<std::string>());
  }
  c.priors.enabled = json::get_bool(j, "priors", true);
  c.priors.as_of = json::get_string(j, "prior_cut");
  return c;
}
Json ProfileConfig::to_json() const {
  Json docs = Json::array();
  for (auto& d : documents) docs.push_back(d);
  Json extra = Json::array();
  for (auto& e : extra_terms) extra.push_back(e);
  return Json{{"repo", repo ? Json(*repo) : Json(nullptr)},
              {"git", git},
              {"documents", docs},
              {"extra_terms", extra},
              {"priors", priors.enabled},
              {"prior_cut", priors.as_of}};
}

// ── ScoreConfig ─────────────────────────────────────────────────────
Result<ScoreConfig> ScoreConfig::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "score config: expected an object");
  ScoreConfig c;
  c.profile_id = json::get_string(j, "profile_id");
  c.max_passes = static_cast<int>(json::get_int(j, "max_passes", c.max_passes));
  c.llm = json::get_string(j, "llm", "off");
  c.verify_max_units = static_cast<int>(json::get_int(j, "verify_max_units", c.verify_max_units));
  if (c.llm != "off" && c.llm != "batch" && c.llm != "auto") {
    return Error(Errc::InvalidArgument, "score config: llm must be off|batch|auto");
  }
  return c;
}
Json ScoreConfig::to_json() const {
  return Json{{"profile_id", profile_id}, {"max_passes", max_passes}, {"llm", llm}, {"verify_max_units", verify_max_units}};
}

// ── UnitScore ───────────────────────────────────────────────────────
Json UnitScore::to_json() const {
  Json projs = Json::array();
  for (auto& p : projects) projs.push_back(p);
  return Json{{"unit_id", unit_id}, {"score", score},   {"label", label},
              {"features", features}, {"reasons", reasons}, {"projects", projs}, {"trap", trap}};
}
Result<UnitScore> UnitScore::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "unit score: expected an object");
  UnitScore s;
  s.unit_id = json::get_string(j, "unit_id");
  s.score = json::get_number(j, "score");
  s.label = json::get_string(j, "label");
  if (const Json* f = json::find(j, "features")) s.features = *f;
  if (const Json* r = json::find(j, "reasons"); r && r->is_array()) s.reasons = *r;
  if (const Json* p = json::find(j, "projects"); p && p->is_array()) {
    for (const auto& v : *p) s.projects.push_back(v.get<std::string>());
  }
  s.trap = json::get_string(j, "trap");
  return s;
}

// ── Override ────────────────────────────────────────────────────────
Result<Override> Override::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "override: expected an object");
  Override o;
  o.unit_id = json::get_string(j, "unit_id");
  o.action = json::get_string(j, "action");
  o.reason = json::get_string(j, "reason");
  if (o.unit_id.empty()) return Error(Errc::InvalidArgument, "override: unit_id is required");
  if (o.action != "include" && o.action != "exclude" && o.action != "pin") {
    return Error(Errc::InvalidArgument, "override: action must be include|exclude|pin");
  }
  return o;
}
Json Override::to_json() const { return Json{{"unit_id", unit_id}, {"action", action}, {"reason", reason}}; }

// ── Decision ────────────────────────────────────────────────────────
Json Decision::to_json() const {
  return Json{{"unit_id", unit_id},     {"selected", selected}, {"decided_by", decided_by},
              {"label", label},         {"score", score},       {"reasons", reasons}};
}
Result<Decision> Decision::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "decision: expected an object");
  Decision d;
  d.unit_id = json::get_string(j, "unit_id");
  d.selected = json::get_bool(j, "selected");
  d.decided_by = json::get_string(j, "decided_by");
  d.label = json::get_string(j, "label");
  d.score = json::get_number(j, "score");
  if (const Json* r = json::find(j, "reasons"); r && r->is_array()) d.reasons = *r;
  return d;
}

// ── UnitQuery ───────────────────────────────────────────────────────
Result<UnitQuery> UnitQuery::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "unit query: expected an object");
  UnitQuery q;
  if (auto s = json::get_opt_string(j, "label")) q.label = *s;
  if (auto s = json::get_opt_string(j, "project")) q.project = *s;
  if (auto s = json::get_opt_string(j, "text")) q.text = *s;
  if (const Json* sel = json::find(j, "selected"); sel && sel->is_boolean()) q.selected = sel->get<bool>();
  q.run_id = json::get_string(j, "run_id");
  q.sort = json::get_string(j, "sort", "score");
  q.limit = static_cast<int>(json::get_int(j, "limit", 100));
  q.offset = static_cast<int>(json::get_int(j, "offset", 0));
  if (q.sort != "score" && q.sort != "date" && q.sort != "id") {
    return Error(Errc::InvalidArgument, "unit query: sort must be score|date|id");
  }
  return q;
}
Json UnitQuery::to_json() const {
  return Json{{"label", label ? Json(*label) : Json(nullptr)},
              {"project", project ? Json(*project) : Json(nullptr)},
              {"text", text ? Json(*text) : Json(nullptr)},
              {"selected", selected ? Json(*selected) : Json(nullptr)},
              {"run_id", run_id},
              {"sort", sort},
              {"limit", limit},
              {"offset", offset}};
}

// ── ImportOptions ───────────────────────────────────────────────────
Result<ImportOptions> ImportOptions::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "import options: expected an object");
  ImportOptions o;
  o.run_id = json::get_string(j, "run_id");
  o.dry_run = json::get_bool(j, "dry_run");
  o.import_messages = json::get_bool(j, "import_messages", true);
  o.mode = json::get_string(j, "mode", "selective");
  o.include_project_siblings = json::get_bool(j, "include_project_siblings");
  o.related_time_window_hours = static_cast<int>(json::get_int(j, "related_time_window_hours"));
  o.store_mode = json::get_string(j, "store_mode", "copy");
  if (o.mode != "selective" && o.mode != "full") return Error(Errc::InvalidArgument, "import options: mode must be selective|full");
  if (o.store_mode != "copy" && o.store_mode != "link") return Error(Errc::InvalidArgument, "import options: store_mode must be copy|link");
  if (o.related_time_window_hours < 0) return Error(Errc::InvalidArgument, "import options: related_time_window_hours must be >= 0");
  return o;
}
Json ImportOptions::to_json() const {
  return Json{{"run_id", run_id},
              {"dry_run", dry_run},
              {"import_messages", import_messages},
              {"mode", mode},
              {"include_project_siblings", include_project_siblings},
              {"related_time_window_hours", related_time_window_hours},
              {"store_mode", store_mode}};
}

// ── Catalog ─────────────────────────────────────────────────────────
Catalog::Catalog(Runtime& rt, std::shared_ptr<const kb::Pack> pack) : rt_(rt), pack_(std::move(pack)) {}

// knowledge.catalog stage (knowledge.h): scan -> build_profile (if needed) ->
// score -> select -> import the selected units. Every sub-step already
// checkpoints itself in the loom_cat_* tables, so a paused/interrupted stage
// simply re-enters the same call on the next run.
Result<Json> run_stage(knowledge::StageContext& ctx) {
  Catalog cat(ctx.rt, ctx.pack);
  LOOM_TRY(Catalog::ensure_schema(ctx.rt.db()));

  ScanConfig scan_cfg;
  scan_cfg.sources = ctx.config.sources;
  if (ctx.config.repo) scan_cfg.sources.push_back(*ctx.config.repo);
  scan_cfg.force = ctx.config.force;
  if (const Json* sp = json::find(ctx.params, "scan")) {
    LOOM_TRY_ASSIGN(scan_cfg, ScanConfig::from_json(*sp));
    if (scan_cfg.sources.empty()) {
      scan_cfg.sources = ctx.config.sources;
      if (ctx.config.repo) scan_cfg.sources.push_back(*ctx.config.repo);
    }
  }
  auto progress = [&](std::string_view step, std::int64_t cur, std::int64_t total, std::string_view msg) {
    if (ctx.progress) ctx.progress(cur, total, std::string(step) + ": " + std::string(msg));
  };
  if (ctx.should_stop && ctx.should_stop()) return Error(Errc::Paused, "catalog: paused before scan");
  LOOM_TRY_ASSIGN(Json scan_stats, cat.scan(scan_cfg, progress));

  ProfileConfig profile_cfg;
  if (ctx.config.repo) profile_cfg.repo = *ctx.config.repo;
  profile_cfg.priors = ctx.priors;
  if (const Json* pp = json::find(ctx.params, "profile")) {
    LOOM_TRY_ASSIGN(profile_cfg, ProfileConfig::from_json(*pp));
  }
  if (ctx.should_stop && ctx.should_stop()) return Error(Errc::Paused, "catalog: paused before profile");
  LOOM_TRY_ASSIGN(SelfProfile profile, cat.build_profile(profile_cfg));

  ScoreConfig score_cfg;
  score_cfg.profile_id = profile.id;
  if (const Json* sc = json::find(ctx.params, "score")) {
    LOOM_TRY_ASSIGN(score_cfg, ScoreConfig::from_json(*sc));
    if (score_cfg.profile_id.empty()) score_cfg.profile_id = profile.id;
  }
  if (ctx.should_stop && ctx.should_stop()) return Error(Errc::Paused, "catalog: paused before score");
  LOOM_TRY_ASSIGN(Json score_stats, cat.score(score_cfg, progress));
  std::string run_id = json::get_string(score_stats, "run_id");

  LOOM_TRY_ASSIGN(std::vector<Decision> decisions, cat.select(run_id));
  std::vector<std::string> selected_units;
  for (auto& d : decisions) {
    if (d.selected) selected_units.push_back(d.unit_id);
  }

  ImportOptions import_opts;
  import_opts.run_id = run_id;
  if (const Json* ip = json::find(ctx.params, "import")) {
    LOOM_TRY_ASSIGN(import_opts, ImportOptions::from_json(*ip));
    if (import_opts.run_id.empty()) import_opts.run_id = run_id;
  }
  if (ctx.should_stop && ctx.should_stop()) return Error(Errc::Paused, "catalog: paused before import");
  LOOM_TRY_ASSIGN(Json import_stats, cat.import_selected(import_opts, progress));

  Json stats{{"scan", scan_stats}, {"score", score_stats}, {"selected", static_cast<std::int64_t>(selected_units.size())},
            {"import", import_stats}};
  std::string output = Sha256::hex(json::canonical(stats));
  Json units_j = Json::array();
  for (auto& u : selected_units) units_j.push_back(u);
  return Json{{"output", output}, {"stats", stats}, {"units", units_j}, {"profile_id", profile.id}, {"score_run", run_id}};
}

}  // namespace loom::catalog
