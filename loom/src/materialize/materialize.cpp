// materialize.h: products of the knowledge layer (LOOM_CONCEPTUAL_MODEL §5,
// §6.6; R2, R6, R11; I8). See the header for the full contract.
#include "loom/materialize.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

#include "loom/knowledge.h"
#include "loom/provenance.h"
#include "loom/re/regex.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"
#include "context/ctx_common.h"

namespace loom::materialize {

namespace {

std::string join(const std::vector<std::string>& v, std::string_view sep) {
  std::string out;
  for (const auto& s : v) {
    if (!out.empty()) out += sep;
    out += s;
  }
  return out;
}

// A deterministic fingerprint of what a materialized artifact depends on:
// the pack, the run and the sorted ids (+ confidence) of every claim and
// principle it lists (model::Product::claims/principles). Two runs with the
// same fingerprint would render byte-identical markdown; a caller may use
// this to skip regenerating an artifact whose fingerprint has not changed
// since the last materialize run (kept alongside the artifact as
// `data.input_hash`; the run-level cache in knowledge.h already skips the
// whole stage when nothing pack/run-relevant changed).
std::string input_hash(std::string_view pack_hash, std::string_view run, const std::vector<std::string>& claims,
                       const std::vector<std::string>& principles) {
  std::vector<std::string> c = claims, p = principles;
  std::sort(c.begin(), c.end());
  std::sort(p.begin(), p.end());
  std::string key = std::string(pack_hash) + "|" + std::string(run) + "|" + join(c, ",") + "|" + join(p, ",");
  return Sha256::hex(key);
}

Result<RuntimeProfile> profile_for(Runtime& rt, const Json& overrides = Json::object()) {
  return RuntimeProfile::load("materialize", rt.paths().root, overrides);
}

Result<std::string> render(const Json& policy, std::string_view key, const Json& variables = Json::object()) {
  return render_profile_template(policy.at("templates").at(std::string(key)).get<std::string>(), variables);
}

Status append(std::string& target, const Json& policy, std::string_view key, const Json& variables = Json::object()) {
  LOOM_TRY_ASSIGN(auto text, render(policy, key, variables));
  target += text;
  return {};
}

std::string language_of(const std::filesystem::path& path, const Json& policy) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
  return json::get_string(policy.at("languages"), ext);
}

std::string excerpt(std::string_view text, const Json& policy) {
  auto length = policy.at("detectors").at("excerpt_codepoints").get<std::size_t>();
  return length == 0 ? std::string(text) : std::string(utf8::prefix(text, length));
}

bool languages_match(const Json& params, std::string_view lang) {
  const Json* langs = json::find(params, "languages");
  if (!langs || !langs->is_array() || lang.empty()) return false;
  for (const auto& l : *langs) {
    if (l.is_string() && l.get<std::string>() == lang) return true;
  }
  return false;
}

struct CheckHit {
  std::string file;
  int line = 0;
  std::string excerpt;
};

// Runs one rules/checks.json entry over `content` (already read, `lang`
// already resolved from the extension), appending hits capped at
// params.max_hits_per_file (unbounded when absent).
Status run_detector(const Json& check, std::string_view content, std::string_view lang, std::string_view file,
                    std::vector<CheckHit>& hits, const Json& policy) {
  const Json& params = check.contains("params") ? check.at("params") : Json::object();
  if (!languages_match(params, lang)) return {};
  std::string detector = json::get_string(check, "detector");
  std::int64_t max_hits = json::get_int(params, "max_hits_per_file", policy.at("detectors").at("max_hits_per_file").get<std::int64_t>());

  if (detector == "regex_line") {
    LOOM_TRY_ASSIGN(auto re, re::Regex::compile(json::get_string(params, "pattern"), re::kNone));
    int line_no = 0;
    std::size_t start = 0;
    while (start <= content.size()) {
      ++line_no;
      std::size_t nl = content.find('\n', start);
      std::string_view line = content.substr(start, (nl == std::string_view::npos ? content.size() : nl) - start);
      if (re.search_utf8(line)) {
        if ((max_hits == 0 || static_cast<std::int64_t>(hits.size()) < max_hits)) {
          hits.push_back({std::string(file), line_no, excerpt(line, policy)});
        }
      }
      if (nl == std::string_view::npos) break;
      start = nl + 1;
    }
  } else if (detector == "regex_block") {
    LOOM_TRY_ASSIGN(auto re, re::Regex::compile(json::get_string(params, "pattern"), re::kMultiline));
    std::u32string u = utf8::decode(content);
    for (const auto& m : re.finditer(u)) {
      if (max_hits > 0 && static_cast<std::int64_t>(hits.size()) >= max_hits) break;
      std::ptrdiff_t s = m.start();
      int line_no = 1 + static_cast<int>(std::count(u.begin(), u.begin() + std::max<std::ptrdiff_t>(0, s), U'\n'));
      hits.push_back({std::string(file), line_no, excerpt(utf8::encode(m.group()), policy)});
    }
  } else if (detector == "string_array_literal") {
    std::int64_t min_entries = json::get_int(params, "min_entries", policy.at("detectors").at("min_array_entries").get<std::int64_t>());
    std::int64_t repeats = std::max<std::int64_t>(0, min_entries - 1);
    std::string pattern = "(\"(?:[^\"\\\\]|\\\\.)*\"\\s*,\\s*){" + std::to_string(repeats) + ",}\"(?:[^\"\\\\]|\\\\.)*\"";
    LOOM_TRY_ASSIGN(auto re, re::Regex::compile(pattern, re::kNone));
    std::u32string u = utf8::decode(content);
    for (const auto& m : re.finditer(u)) {
      if (max_hits > 0 && static_cast<std::int64_t>(hits.size()) >= max_hits) break;
      std::ptrdiff_t s = m.start();
      int line_no = 1 + static_cast<int>(std::count(u.begin(), u.begin() + std::max<std::ptrdiff_t>(0, s), U'\n'));
      LOOM_TRY_ASSIGN(auto detail, render(policy, "array_literal", Json{{"count", std::to_string(min_entries)}}));
      hits.push_back({std::string(file), line_no, detail});
    }
  }
  return {};
}

std::string mime_for(std::string_view name, const Json& policy) {
  const Json& mime = policy.at("mime");
  return mime.at(name.ends_with(mime.at("json_suffix").get<std::string>()) ? "json" : "markdown").get<std::string>();
}

}  // namespace

Json Rendered::to_json() const {
  return Json{{"kind", kind}, {"title", title}, {"markdown", markdown}, {"data", data}, {"product", product.to_json()}};
}

Materializer::Materializer(Runtime& rt, kb::KnowledgeStore& store, std::shared_ptr<const kb::Pack> pack)
    : Materializer(rt, store, std::move(pack), Json::object()) {}

Materializer::Materializer(Runtime& rt, kb::KnowledgeStore& store, std::shared_ptr<const kb::Pack> pack, Json overrides)
    : rt_(rt), store_(store), pack_(std::move(pack)), overrides_(std::move(overrides)) {}

// ── self_description ────────────────────────────────────────────────

Result<Rendered> Materializer::self_description(std::string_view run_arg) {
  LOOM_TRY_ASSIGN(auto profile, profile_for(rt_, overrides_));
  const Json& policy = profile.values();
  LOOM_TRY_ASSIGN(std::string run, ctx::resolve_run(store_, run_arg));
  LOOM_TRY_ASSIGN(auto md, render(policy, "self_header", Json{{"run", run}}));
  Json data = Json::object();

  kb::EntityQuery pq;
  pq.kind = policy.at("project_kind").get<std::string>();
  LOOM_TRY_ASSIGN(auto projects, store_.query_entities(run, pq));
  LOOM_TRY(append(md, policy, "projects_header"));
  Json projects_j = Json::array();
  std::vector<std::string> dep_claims, dep_principles;
  for (const auto& proj : projects) {
    LOOM_TRY(append(md, policy, "project_header", Json{{"label", proj.label}, {"kind", proj.kind}}));
    LOOM_TRY(append(md, policy, "project_status", Json{{"status", std::string(model::to_string(proj.status))}, {"confidence", std::to_string(proj.confidence)}}));
    LOOM_TRY_ASSIGN(auto decisions, store_.list_decisions(run, proj.id));
    LOOM_TRY_ASSIGN(auto forks, store_.list_forks(run, proj.id));
    LOOM_TRY_ASSIGN(auto areas, store_.list_areas(run, proj.id));
    kb::EntityQuery cq;
    cq.parent = proj.id;
    LOOM_TRY_ASSIGN(auto children, store_.query_entities(run, cq));
    Json comps = Json::array();
    LOOM_TRY(append(md, policy, "components_header"));
    for (const auto& comp : children) {
      LOOM_TRY_ASSIGN(auto hist, store_.status_history(run, comp.id));
      std::string last_status = hist.empty() ? policy.at("templates").at("unknown_status").get<std::string>() : std::string(model::to_string(hist.back().status));
      bool osc = std::any_of(hist.begin(), hist.end(), [](const model::StatusRecord& r) { return r.oscillation; });
      LOOM_TRY(append(md, policy, "component", Json{{"label", comp.label}, {"kind", comp.kind}, {"status", last_status}, {"oscillation", osc ? policy.at("templates").at("oscillation").get<std::string>() : ""}}));
      Json versions = Json::array();
      for (const auto& r : hist) {
        versions.push_back(Json{{"branch", r.branch}, {"version", r.version}, {"status", std::string(model::to_string(r.status))},
                                {"date", r.date}, {"oscillation", r.oscillation}});
        if (!r.claim.empty()) dep_claims.push_back(r.claim);
      }
      comps.push_back(Json{{"id", comp.id}, {"label", comp.label}, {"kind", comp.kind}, {"versions", versions}});
    }
    LOOM_TRY(append(md, policy, "decisions_header"));
    Json dec_j = Json::array();
    for (const auto& d : decisions) {
      const model::DecisionAlternative* chosen = d.chosen();
      std::string superseded;
      if (!d.superseded_by.empty()) { LOOM_TRY_ASSIGN(superseded, render(policy, "superseded", Json{{"id", d.superseded_by}})); }
      LOOM_TRY(append(md, policy, "decision", Json{{"date", d.date}, {"chosen", chosen ? chosen->label : policy.at("templates").at("missing_chosen").get<std::string>()}, {"superseded", superseded}}));
      dep_claims.push_back(d.id);
      dec_j.push_back(d.to_json());
    }
    LOOM_TRY(append(md, policy, "forks_header"));
    Json fork_j = Json::array();
    for (const auto& f : forks) {
      LOOM_TRY(append(md, policy, "fork", Json{{"kind", std::string(model::to_string(f.kind))}, {"base", f.base}, {"count", std::to_string(f.sides.size())}}));
      fork_j.push_back(f.to_json());
    }
    Json area_j = Json::array();
    for (const auto& a : areas) {
      LOOM_TRY(append(md, policy, "area", Json{{"statement", a.statement}, {"gap", a.gap ? policy.at("templates").at("gap").get<std::string>() : ""}}));
      area_j.push_back(a.to_json());
    }
    projects_j.push_back(Json{{"entity", proj.to_json()}, {"components", comps}, {"decisions", dec_j}, {"forks", fork_j}, {"areas", area_j}});
    md += policy.at("templates").at("project_separator").get<std::string>();
  }
  data["projects"] = projects_j;

  LOOM_TRY_ASSIGN(auto principles, store_.list_principles(run));
  LOOM_TRY(append(md, policy, "principles_header"));
  Json principles_j = Json::array();
  for (const auto& p : principles) {
    bool seed = p.sources.empty() || p.validation == model::ValidationStatus::Candidate;
    LOOM_TRY(append(md, policy, "principle", Json{{"level", std::string(model::to_string(p.level))}, {"form", std::string(model::to_string(p.form))}, {"statement", ctx::pick_text(p.statement, policy.at("language").get<std::string>())}, {"source", policy.at("templates").at(seed ? "seed" : "discovered")}, {"validation", std::string(model::to_string(p.validation))}}));
    dep_principles.push_back(p.id);
    principles_j.push_back(p.to_json());
  }
  data["principles"] = principles_j;

  LOOM_TRY_ASSIGN(auto operators, store_.list_operators(run));
  LOOM_TRY(append(md, policy, "operators_header"));
  Json operators_j = Json::array();
  for (const auto& o : operators) {
    LOOM_TRY(append(md, policy, "operator", Json{{"situation", ctx::pick_text(o.situation, policy.at("language").get<std::string>())}, {"solution", ctx::pick_text(o.solution, policy.at("language").get<std::string>())}, {"confidence", std::to_string(o.confidence)}}));
    operators_j.push_back(o.to_json());
  }
  data["operators"] = operators_j;

  // Open questions: distinct texts of Assessment.open.questions across every
  // claim of the run (no dedicated store table for them yet — a known gap;
  // see the final report).
  LOOM_TRY_ASSIGN(auto all_claims, store_.query_claims(run, kb::ClaimQuery{}));
  std::set<std::string> open_qs;
  for (const auto& c : all_claims) {
    for (const auto& q : c.assessment.open.questions) open_qs.insert(q);
  }
  LOOM_TRY(append(md, policy, "questions_header"));
  Json oq_j = Json::array();
  for (const auto& q : open_qs) {
    LOOM_TRY(append(md, policy, "bullet", Json{{"text", q}}));
    oq_j.push_back(q);
  }
  data["open_questions"] = oq_j;

  Rendered r;
  r.kind = "self_description";
  LOOM_TRY_ASSIGN(r.title, render(policy, "self_title"));
  r.markdown = md;
  if (!profile.is_builtin()) data["runtime_profile_hash"] = profile.hash();
  r.data = data;
  r.product.kind = "self_description";
  r.product.instance = "";
  r.product.run = run;
  r.product.claims = dep_claims;
  r.product.principles = dep_principles;
  r.product.id = model::Product::make_id(r.product.kind, r.product.instance, run);
  data["input_hash"] = input_hash(profile.is_builtin() ? pack_->hash() : pack_->hash() + "|" + profile.hash(), run, dep_claims, dep_principles);
  if (!profile.is_builtin()) data["runtime_profile_hash"] = profile.hash();
  r.data = data;
  return r;
}

// ── dossier ──────────────────────────────────────────────────────────

Result<Rendered> Materializer::dossier(std::string_view run_arg, std::string_view instance_id) {
  LOOM_TRY_ASSIGN(auto profile, profile_for(rt_, overrides_));
  const Json& policy = profile.values();
  LOOM_TRY_ASSIGN(std::string run, ctx::resolve_run(store_, run_arg));
  LOOM_TRY_ASSIGN(auto inst_opt, store_.get_instance(run, instance_id));
  if (!inst_opt) return Error(Errc::NotFound, "no instance '" + std::string(instance_id) + "' in run " + run);
  const model::Instance& inst = *inst_opt;

  kb::SlotQuery sq;
  sq.instance = inst.id;
  LOOM_TRY_ASSIGN(auto rows, store_.query_slots(run, sq));

  LOOM_TRY_ASSIGN(auto md, render(policy, "dossier_header", Json{{"label", inst.subject_label}, {"paradigm", inst.paradigm}}));
  LOOM_TRY(append(md, policy, "coverage", Json{{"coverage", json::dump(inst.coverage)}}));
  LOOM_TRY(append(md, policy, "slots_header"));
  Json slots_j = Json::array();
  Json conflicts_j = Json::array();
  Json analogies_j = Json::array();
  std::vector<std::string> dep_claims{};
  for (const auto& row : rows) {
    auto claim_r = store_.get_claim(run, row.value.claim);
    if (!claim_r || !*claim_r) continue;
    const model::Claim& claim = **claim_r;
    dep_claims.push_back(claim.id);
    std::string obj = claim.object.empty() ? json::dump(claim.value) : claim.object;
    LOOM_TRY_ASSIGN(auto text, render(policy, "slot", Json{{"slot", row.value.slot}, {"object", obj}}));
    std::string marked = ctx::evidence_markdown(*pack_, claim.assessment.evidence, claim.assessment.origin,
                                                claim.assessment.confidence, text, claim.assessment.expected,
                                                claim.assessment.derivation ? claim.assessment.derivation->op : "",
                                                claim.assessment.open.fill_query);
    LOOM_TRY(append(md, policy, "slot_line", Json{{"text", marked}, {"conflict", row.value.conflict ? policy.at("templates").at("conflict").get<std::string>() : ""}}));
    slots_j.push_back(Json{{"slot", row.value.slot}, {"claim", claim.id}, {"conflict", row.value.conflict}, {"text", marked}});
    if (row.value.conflict) conflicts_j.push_back(row.value.to_json());
    if (claim.assessment.derivation && !claim.assessment.derivation->morphism.empty()) {
      analogies_j.push_back(Json{{"slot", row.value.slot}, {"morphism", claim.assessment.derivation->morphism}, {"claim", claim.id}});
    }
  }
  Json data{{"instance", inst.to_json()}, {"slots", slots_j}, {"conflicts", conflicts_j}, {"analogies", analogies_j}};

  Rendered r;
  r.kind = "dossier";
  LOOM_TRY_ASSIGN(r.title, render(policy, "dossier_title", Json{{"label", inst.subject_label}}));
  r.markdown = md;
  r.product.kind = "dossier";
  r.product.instance = inst.id;
  r.product.run = run;
  r.product.claims = dep_claims;
  r.product.id = model::Product::make_id(r.product.kind, r.product.instance, run);
  data["input_hash"] = input_hash(profile.is_builtin() ? pack_->hash() : pack_->hash() + "|" + profile.hash(), run, dep_claims, {});
  if (!profile.is_builtin()) data["runtime_profile_hash"] = profile.hash();
  r.data = data;
  return r;
}

// ── backlog ──────────────────────────────────────────────────────────

Result<Rendered> Materializer::backlog(std::string_view run_arg) {
  LOOM_TRY_ASSIGN(auto profile, profile_for(rt_, overrides_));
  const Json& policy = profile.values();
  LOOM_TRY_ASSIGN(std::string run, ctx::resolve_run(store_, run_arg));
  LOOM_TRY_ASSIGN(auto md, render(policy, "backlog_header"));
  Json data = Json::object();
  std::vector<std::string> dep_claims;

  kb::ClaimQuery contested_q;
  contested_q.status = model::ClaimStatus::Contested;
  LOOM_TRY_ASSIGN(auto contested, store_.query_claims(run, contested_q));
  LOOM_TRY(append(md, policy, "contested_header"));
  Json contested_j = Json::array();
  for (const auto& c : contested) {
    LOOM_TRY(append(md, policy, "contested", Json{{"subject", c.subject}, {"predicate", c.predicate}}));
    dep_claims.push_back(c.id);
    contested_j.push_back(c.to_json());
  }
  data["contested"] = contested_j;

  kb::ClaimQuery inferred_q;
  inferred_q.evidence = model::EvidenceClass::Inferred;
  LOOM_TRY_ASSIGN(auto inferred, store_.query_claims(run, inferred_q));
  LOOM_TRY(append(md, policy, "violated_header"));
  Json violated_j = Json::array();
  for (const auto& c : inferred) {
    if (c.assessment.check != model::CheckState::Violated) continue;
    LOOM_TRY(append(md, policy, "violated", Json{{"subject", c.subject}, {"predicate", c.predicate}, {"expected", c.assessment.expected ? c.assessment.expected->render() : ""}}));
    dep_claims.push_back(c.id);
    violated_j.push_back(c.to_json());
  }
  data["violated_expected_properties"] = violated_j;

  kb::ClaimQuery violates_q;
  violates_q.predicate = policy.at("violations_predicate").get<std::string>();
  LOOM_TRY_ASSIGN(auto violates, store_.query_claims(run, violates_q));
  LOOM_TRY(append(md, policy, "violations_header"));
  Json violates_j = Json::array();
  for (const auto& c : violates) {
    LOOM_TRY(append(md, policy, "violation", Json{{"subject", c.subject}, {"value", json::dump(c.value)}}));
    dep_claims.push_back(c.id);
    violates_j.push_back(c.to_json());
  }
  data["violations"] = violates_j;

  LOOM_TRY(append(md, policy, "absent_header"));
  Json absent_j = Json::array();
  LOOM_TRY_ASSIGN(auto instances, store_.query_instances(run, "", ""));
  for (const auto& inst : instances) {
    kb::SlotQuery sq;
    sq.instance = inst.id;
    auto rows = store_.query_slots(run, sq);
    if (!rows) continue;
    for (const auto& row : *rows) {
      auto claim_r = store_.get_claim(run, row.value.claim);
      if (!claim_r || !*claim_r || !(*claim_r)->is_absent()) continue;
      LOOM_TRY(append(md, policy, "absent", Json{{"label", inst.subject_label}, {"slot", row.value.slot}}));
      absent_j.push_back(Json{{"instance", inst.id}, {"slot", row.value.slot}, {"claim", row.value.claim}});
    }
  }
  data["absent_required_slots"] = absent_j;

  LOOM_TRY(append(md, policy, "lost_header"));
  Json lost_j = Json::array();
  LOOM_TRY_ASSIGN(auto entities, store_.query_entities(run, kb::EntityQuery{}));
  for (const auto& e : entities) {
    LOOM_TRY_ASSIGN(auto hist, store_.status_history(run, e.id));
    if (hist.empty()) continue;
    const auto& last = hist.back();
    if (last.status != model::StatusValue::Lost) continue;
    LOOM_TRY(append(md, policy, "lost", Json{{"label", e.label}, {"branch", last.branch.empty() ? policy.at("templates").at("missing_branch").get<std::string>() : last.branch}, {"date", last.date}, {"oscillation", last.oscillation ? policy.at("templates").at("oscillation").get<std::string>() : ""}}));
    lost_j.push_back(last.to_json());
  }
  data["lost_features"] = lost_j;

  Rendered r;
  r.kind = "backlog";
  LOOM_TRY_ASSIGN(r.title, render(policy, "backlog_title"));
  r.markdown = md;
  r.product.kind = "backlog";
  r.product.run = run;
  r.product.claims = dep_claims;
  r.product.id = model::Product::make_id(r.product.kind, r.product.instance, run);
  data["input_hash"] = input_hash(profile.is_builtin() ? pack_->hash() : pack_->hash() + "|" + profile.hash(), run, dep_claims, {});
  if (!profile.is_builtin()) data["runtime_profile_hash"] = profile.hash();
  r.data = data;
  return r;
}

// ── extrapolated_spec ────────────────────────────────────────────────

Result<Rendered> Materializer::extrapolated_spec(std::string_view run_arg, std::string_view instance_id) {
  LOOM_TRY_ASSIGN(auto profile, profile_for(rt_, overrides_));
  const Json& policy = profile.values();
  LOOM_TRY_ASSIGN(std::string run, ctx::resolve_run(store_, run_arg));
  LOOM_TRY_ASSIGN(auto inst_opt, store_.get_instance(run, instance_id));
  if (!inst_opt) return Error(Errc::NotFound, "no instance '" + std::string(instance_id) + "' in run " + run);
  const model::Instance& inst = *inst_opt;

  kb::SlotQuery sq;
  sq.instance = inst.id;
  LOOM_TRY_ASSIGN(auto rows, store_.query_slots(run, sq));

  LOOM_TRY_ASSIGN(auto md, render(policy, "extrapolated_header", Json{{"label", inst.subject_label}}));
  Json proposals = Json::array();
  std::vector<std::string> dep_claims;
  for (const auto& row : rows) {
    auto claim_r = store_.get_claim(run, row.value.claim);
    if (!claim_r || !*claim_r || (*claim_r)->assessment.evidence != model::EvidenceClass::Extrapolated) continue;
    const model::Claim& claim = **claim_r;
    dep_claims.push_back(claim.id);
    std::string basis = claim.assessment.derivation ? claim.assessment.derivation->op : "";
    LOOM_TRY_ASSIGN(auto text, render(policy, "slot", Json{{"slot", row.value.slot}, {"object", claim.object.empty() ? json::dump(claim.value) : claim.object}}));
    std::string marked = ctx::evidence_markdown(*pack_, claim.assessment.evidence, claim.assessment.origin,
                                                claim.assessment.confidence, text, claim.assessment.expected, basis,
                                                claim.assessment.open.fill_query);
    LOOM_TRY(append(md, policy, "bullet", Json{{"text", marked}}));
    proposals.push_back(Json{{"slot", row.value.slot}, {"claim", claim.id}, {"basis", basis}});
  }
  if (proposals.empty()) LOOM_TRY(append(md, policy, "no_proposals"));

  Rendered r;
  r.kind = "extrapolated_spec";
  LOOM_TRY_ASSIGN(r.title, render(policy, "extrapolated_title", Json{{"label", inst.subject_label}}));
  r.markdown = md;
  r.product.kind = "extrapolated_spec";
  r.product.instance = inst.id;
  r.product.run = run;
  r.product.claims = dep_claims;
  r.product.id = model::Product::make_id(r.product.kind, r.product.instance, run);
  Json data{{"instance", inst.id}, {"proposals", proposals}};
  data["input_hash"] = input_hash(profile.is_builtin() ? pack_->hash() : pack_->hash() + "|" + profile.hash(), run, dep_claims, {});
  if (!profile.is_builtin()) data["runtime_profile_hash"] = profile.hash();
  r.data = data;
  return r;
}

// ── check_preferences ────────────────────────────────────────────────

Result<std::vector<model::ProductCheck>> Materializer::check_preferences(const model::Product&,
                                                                         const std::vector<std::string>& files) {
  LOOM_TRY_ASSIGN(auto profile, profile_for(rt_, overrides_));
  const Json& policy = profile.values();
  const Json& checks_doc = pack_->file("rules/checks.json");
  const Json* checks = json::find(checks_doc, "checks");
  if (!checks || !checks->is_array()) return std::vector<model::ProductCheck>{};

  std::map<std::string, std::vector<CheckHit>> hits_by_check;
  std::map<std::string, std::string> principle_of_check;
  for (const auto& check : *checks) {
    std::string id = json::get_string(check, "id");
    principle_of_check[id] = json::get_string(check, "principle");
    hits_by_check[id] = {};
  }

  for (const auto& file : files) {
    std::filesystem::path p(file);
    std::string lang = language_of(p, policy);
    if (lang.empty()) continue;
    auto content = fsutil::read_file(p);
    if (!content) continue;
    for (const auto& check : *checks) {
      std::string id = json::get_string(check, "id");
      std::vector<CheckHit> hits;
      auto st = run_detector(check, *content, lang, file, hits, policy);
      if (!st) return st.error();  // Invalid executable profile data must remain visible.
      for (auto& h : hits) hits_by_check[id].push_back(std::move(h));
    }
  }

  std::vector<model::ProductCheck> out;
  for (const auto& check : *checks) {
    std::string id = json::get_string(check, "id");
    const auto& hits = hits_by_check[id];
    model::ProductCheck pc;
    pc.check = id;
    pc.principle = principle_of_check[id];
    pc.passed = hits.empty();
    if (!hits.empty()) {
      LOOM_TRY_ASSIGN(auto detail, render(policy, "detail_header", Json{{"count", std::to_string(hits.size())}}));
      std::size_t shown = 0;
      for (const auto& h : hits) {
        auto limit = policy.at("detectors").at("detail_hits").get<std::size_t>();
        if (limit > 0 && shown++ >= limit) {
          LOOM_TRY(append(detail, policy, "detail_ellipsis"));
          break;
        }
        LOOM_TRY(append(detail, policy, "detail_hit", Json{{"file", h.file}, {"line", std::to_string(h.line)}}));
      }
      pc.detail = detail;
    } else {
      LOOM_TRY_ASSIGN(pc.detail, render(policy, "detail_no_hits"));
    }
    out.push_back(std::move(pc));
  }
  return out;
}

// ── knowledge.materialize stage ──────────────────────────────────────

Result<Json> run_stage(knowledge::StageContext& ctx) {
  LOOM_TRY_ASSIGN(auto profile, profile_for(ctx.rt));
  LOOM_TRY_ASSIGN(auto builtin_profile, RuntimeProfile::load("materialize"));
  if (ctx.expected_runtime_profiles) {
    const auto& expected = *ctx.expected_runtime_profiles;
    if (!expected.is_object()) return Error(Errc::InvalidArgument, "expected runtime profiles must be a hash object");
    const auto* hash = json::find(expected, "materialize");
    if (hash && !hash->is_string()) return Error(Errc::InvalidArgument, "invalid expected materialize profile hash");
    const auto selected_hash = hash ? hash->get<std::string>() : builtin_profile.hash();
    if (profile.hash() != selected_hash) {
      return Error(Errc::Conflict, "runtime profile recipe changed for materialize; expected " + selected_hash +
                                      ", actual " + profile.hash());
    }
  }
  const Json& policy = profile.values();
  Materializer m(ctx.rt, ctx.store, ctx.pack);
  struct Renderer {
    bool per_instance;
    std::string statistic;
    std::function<Result<Rendered>(std::string_view)> execute;
  };
  // These are executable capabilities, not domain-name policy. The recipe
  // selects and orders them; adding an executable renderer is a code change.
  const std::map<std::string, Renderer> registry{
      {"self_description", {false, "", [&](std::string_view) { return m.self_description(ctx.run); }}},
      {"dossier", {true, "dossiers", [&](std::string_view instance) { return m.dossier(ctx.run, instance); }}},
      {"extrapolated_spec", {true, "extrapolated_specs", [&](std::string_view instance) { return m.extrapolated_spec(ctx.run, instance); }}},
      {"backlog", {false, "", [&](std::string_view) { return m.backlog(ctx.run); }}},
  };
  struct Operation {
    std::string name;
    const Renderer* renderer;
    bool omit_error;
  };
  std::vector<Operation> operations;
  for (const auto& entry : policy.at("products")) {
    const auto name = entry.at("renderer").get<std::string>();
    auto found = registry.find(name);
    if (found == registry.end()) return Error(Errc::Unavailable, "unknown materialize renderer: " + name);
    if (entry.at("enabled").get<bool>())
      operations.push_back({name, &found->second, entry.at("on_error") == "omit"});
  }
  LOOM_TRY_ASSIGN(auto instances, ctx.store.query_instances(ctx.run, "", ""));
  struct Planned {
    Operation operation;
    std::string instance;
    std::string file_name;
  };
  std::vector<Planned> plan;
  auto add_to_plan = [&](const Operation& operation, std::string_view instance) -> Status {
    Json variables = instance.empty() ? Json::object() : Json{{"instance", instance}};
    LOOM_TRY_ASSIGN(auto file, render_profile_template(policy.at("outputs").at(operation.name).get<std::string>(), variables));
    plan.push_back({operation, std::string(instance), std::move(file)});
    return {};
  };
  // Consecutive instance operations interleave per instance. The default
  // recipe therefore keeps the original self, (dossier,spec)*, backlog order.
  for (std::size_t begin = 0; begin < operations.size();) {
    if (!operations[begin].renderer->per_instance) {
      LOOM_TRY(add_to_plan(operations[begin], ""));
      ++begin;
      continue;
    }
    std::size_t end = begin;
    while (end < operations.size() && operations[end].renderer->per_instance) ++end;
    for (const auto& instance : instances)
      for (std::size_t index = begin; index < end; ++index) LOOM_TRY(add_to_plan(operations[index], instance.id));
    begin = end;
  }
  Json artifacts = Json::array();
  std::vector<model::Product> products;

  auto store_artifact = [&](const Rendered& r, std::string_view file_name) -> Status {
    // Individual renderers load their recipe at entry. File names and MIME
    // came from the stage's outer recipe; require the body to share that
    // identity before writing a blob, provenance row or exported file.
    const auto* hash = json::find(r.data, "runtime_profile_hash");
    if (hash && !hash->is_string()) return Error(Errc::InvalidArgument, "invalid rendered materialize profile hash");
    const auto rendered_hash = hash ? hash->get<std::string>() : builtin_profile.hash();
    if (rendered_hash != profile.hash()) {
      return Error(Errc::Conflict, "runtime profile recipe changed while materializing; expected " + profile.hash() +
                                      ", actual " + rendered_hash);
    }
    LOOM_TRY_ASSIGN(BlobRef ref, ctx.rt.blobs().put(r.markdown, "text/markdown"));
    ArtifactRecord ar;
    ar.kind = "knowledge." + r.kind;
    ar.title = r.title;
    ar.blob_hash = ref.hash;
    ar.mime = mime_for(file_name, policy);
    ar.task_id = ctx.run;
    ar.metadata = r.data;
    LOOM_TRY_ASSIGN(std::string aid, ctx.rt.provenance().add_artifact(ar));
    model::Product product = r.product;
    product.artifact = aid;
    products.push_back(product);
    artifacts.push_back(Json{{"id", aid}, {"kind", r.kind}, {"name", file_name}, {"hash", ref.hash}});
    if (!ctx.config.out_dir.empty()) {
      auto od = fsutil::expand_user(ctx.config.out_dir);
      LOOM_TRY(fsutil::ensure_dir(od));
      LOOM_TRY(fsutil::atomic_write(od / std::string(file_name), r.markdown, {.fsync = false, .owner_only = false}));
    }
    return {};
  };

  Json statistics = Json::object();
  for (const auto& [name, renderer] : registry)
    if (!renderer.statistic.empty()) statistics[renderer.statistic] = 0;
  for (const auto& planned : plan) {
    auto rendered = planned.operation.renderer->execute(planned.instance);
    if (!rendered) {
      if (planned.operation.omit_error) continue;
      return rendered.error();
    }
    LOOM_TRY(store_artifact(*rendered, planned.file_name));
    if (!planned.operation.renderer->statistic.empty()) {
      auto& count = statistics[planned.operation.renderer->statistic];
      count = count.get<int>() + 1;
    }
  }

  LOOM_TRY(ctx.store.put_products(ctx.run, products));

  std::vector<std::string> hashes;
  for (const auto& a : artifacts) hashes.push_back(json::get_string(a, "hash"));
  std::sort(hashes.begin(), hashes.end());
  std::string joined;
  for (auto& h : hashes) joined += h + "|";
  statistics["products"] = products.size();

  return Json{{"output", Sha256::hex(joined)},
              {"artifacts", artifacts},
              {"stats", statistics}};
}

}  // namespace loom::materialize
