// materialize.h: products of the knowledge layer (LOOM_CONCEPTUAL_MODEL §5,
// §6.6; R2, R6, R11; I8). See the header for the full contract.
#include "loom/materialize.h"

#include <algorithm>
#include <map>
#include <set>

#include "loom/knowledge.h"
#include "loom/provenance.h"
#include "loom/re/regex.h"
#include "loom/runtime.h"
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

std::string language_of(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
  if (ext == ".py") return "python";
  if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" || ext == ".hpp") return "cpp";
  if (ext == ".c") return "c";
  if (ext == ".kt" || ext == ".kts") return "kotlin";
  if (ext == ".java") return "java";
  if (ext == ".ts" || ext == ".tsx" || ext == ".js" || ext == ".jsx") return "typescript";
  return "";
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
                    std::vector<CheckHit>& hits) {
  const Json& params = check.contains("params") ? check.at("params") : Json::object();
  if (!languages_match(params, lang)) return {};
  std::string detector = json::get_string(check, "detector");
  std::int64_t max_hits = json::get_int(params, "max_hits_per_file", 1000000);

  if (detector == "regex_line") {
    LOOM_TRY_ASSIGN(auto re, re::Regex::compile(json::get_string(params, "pattern"), re::kNone));
    int line_no = 0;
    std::size_t start = 0;
    while (start <= content.size()) {
      ++line_no;
      std::size_t nl = content.find('\n', start);
      std::string_view line = content.substr(start, (nl == std::string_view::npos ? content.size() : nl) - start);
      if (re.search_utf8(line)) {
        if (static_cast<std::int64_t>(hits.size()) < max_hits) {
          hits.push_back({std::string(file), line_no, std::string(utf8::prefix(line, 160))});
        }
      }
      if (nl == std::string_view::npos) break;
      start = nl + 1;
    }
  } else if (detector == "regex_block") {
    LOOM_TRY_ASSIGN(auto re, re::Regex::compile(json::get_string(params, "pattern"), re::kMultiline));
    std::u32string u = utf8::decode(content);
    for (const auto& m : re.finditer(u)) {
      if (static_cast<std::int64_t>(hits.size()) >= max_hits) break;
      std::ptrdiff_t s = m.start();
      int line_no = 1 + static_cast<int>(std::count(u.begin(), u.begin() + std::max<std::ptrdiff_t>(0, s), U'\n'));
      hits.push_back({std::string(file), line_no, utf8::prefix(utf8::encode(m.group()), 160).empty()
                                                       ? ""
                                                       : std::string(utf8::prefix(utf8::encode(m.group()), 160))});
    }
  } else if (detector == "string_array_literal") {
    std::int64_t min_entries = json::get_int(params, "min_entries", 20);
    std::int64_t repeats = std::max<std::int64_t>(0, min_entries - 1);
    std::string pattern = "(\"(?:[^\"\\\\]|\\\\.)*\"\\s*,\\s*){" + std::to_string(repeats) + ",}\"(?:[^\"\\\\]|\\\\.)*\"";
    LOOM_TRY_ASSIGN(auto re, re::Regex::compile(pattern, re::kNone));
    std::u32string u = utf8::decode(content);
    for (const auto& m : re.finditer(u)) {
      if (static_cast<std::int64_t>(hits.size()) >= max_hits) break;
      std::ptrdiff_t s = m.start();
      int line_no = 1 + static_cast<int>(std::count(u.begin(), u.begin() + std::max<std::ptrdiff_t>(0, s), U'\n'));
      hits.push_back({std::string(file), line_no, "literal string array (>= " + std::to_string(min_entries) + " entries)"});
    }
  }
  return {};
}

std::string mime_for(std::string_view name) {
  if (name.ends_with(".json")) return "application/json";
  return "text/markdown";
}

}  // namespace

Json Rendered::to_json() const {
  return Json{{"kind", kind}, {"title", title}, {"markdown", markdown}, {"data", data}, {"product", product.to_json()}};
}

Materializer::Materializer(Runtime& rt, kb::KnowledgeStore& store, std::shared_ptr<const kb::Pack> pack)
    : rt_(rt), store_(store), pack_(std::move(pack)) {}

// ── self_description ────────────────────────────────────────────────

Result<Rendered> Materializer::self_description(std::string_view run_arg) {
  LOOM_TRY_ASSIGN(std::string run, ctx::resolve_run(store_, run_arg));
  std::string md = "# SELF.md\n\nGenerated by Loom's knowledge layer for run `" + run + "`.\n\n";
  Json data = Json::object();

  kb::EntityQuery pq;
  pq.kind = "project";
  LOOM_TRY_ASSIGN(auto projects, store_.query_entities(run, pq));
  md += "## Projects\n\n";
  Json projects_j = Json::array();
  std::vector<std::string> dep_claims, dep_principles;
  for (const auto& proj : projects) {
    md += "### " + proj.label + " (" + proj.kind + ")\n";
    md += "- status: " + std::string(model::to_string(proj.status)) + ", confidence " +
          std::to_string(proj.confidence) + "\n";
    LOOM_TRY_ASSIGN(auto decisions, store_.list_decisions(run, proj.id));
    LOOM_TRY_ASSIGN(auto forks, store_.list_forks(run, proj.id));
    LOOM_TRY_ASSIGN(auto areas, store_.list_areas(run, proj.id));
    kb::EntityQuery cq;
    cq.parent = proj.id;
    LOOM_TRY_ASSIGN(auto children, store_.query_entities(run, cq));
    Json comps = Json::array();
    md += "- components:\n";
    for (const auto& comp : children) {
      LOOM_TRY_ASSIGN(auto hist, store_.status_history(run, comp.id));
      std::string last_status = hist.empty() ? "unknown" : std::string(model::to_string(hist.back().status));
      bool osc = std::any_of(hist.begin(), hist.end(), [](const model::StatusRecord& r) { return r.oscillation; });
      md += "  - " + comp.label + " (" + comp.kind + "): " + last_status + (osc ? " [oscillates]" : "") + "\n";
      Json versions = Json::array();
      for (const auto& r : hist) {
        versions.push_back(Json{{"branch", r.branch}, {"version", r.version}, {"status", std::string(model::to_string(r.status))},
                                {"date", r.date}, {"oscillation", r.oscillation}});
        if (!r.claim.empty()) dep_claims.push_back(r.claim);
      }
      comps.push_back(Json{{"id", comp.id}, {"label", comp.label}, {"kind", comp.kind}, {"versions", versions}});
    }
    md += "- decisions:\n";
    Json dec_j = Json::array();
    for (const auto& d : decisions) {
      const model::DecisionAlternative* chosen = d.chosen();
      md += "  - " + d.date + ": chose " + (chosen ? chosen->label : "?") + (d.superseded_by.empty() ? "" : " (superseded by " + d.superseded_by + ")") + "\n";
      dep_claims.push_back(d.id);
      dec_j.push_back(d.to_json());
    }
    md += "- forks:\n";
    Json fork_j = Json::array();
    for (const auto& f : forks) {
      md += "  - " + std::string(model::to_string(f.kind)) + " at " + f.base + " (" + std::to_string(f.sides.size()) + " sides)\n";
      fork_j.push_back(f.to_json());
    }
    Json area_j = Json::array();
    for (const auto& a : areas) {
      md += std::string("- area: ") + a.statement + (a.gap ? " [GAP: no members found]" : "") + "\n";
      area_j.push_back(a.to_json());
    }
    projects_j.push_back(Json{{"entity", proj.to_json()}, {"components", comps}, {"decisions", dec_j}, {"forks", fork_j}, {"areas", area_j}});
    md += "\n";
  }
  data["projects"] = projects_j;

  LOOM_TRY_ASSIGN(auto principles, store_.list_principles(run));
  md += "## Principles\n\n";
  Json principles_j = Json::array();
  for (const auto& p : principles) {
    bool seed = p.sources.empty() || p.validation == model::ValidationStatus::Candidate;
    md += "- [" + std::string(model::to_string(p.level)) + "/" + std::string(model::to_string(p.form)) + "] " +
          ctx::pick_text(p.statement, "en") + " (" + (seed ? "seed" : "discovered") + ", " +
          std::string(model::to_string(p.validation)) + ")\n";
    dep_principles.push_back(p.id);
    principles_j.push_back(p.to_json());
  }
  data["principles"] = principles_j;

  LOOM_TRY_ASSIGN(auto operators, store_.list_operators(run));
  md += "\n## Operators\n\n";
  Json operators_j = Json::array();
  for (const auto& o : operators) {
    md += "- " + ctx::pick_text(o.situation, "en") + " -> " + ctx::pick_text(o.solution, "en") + " (confidence " +
          std::to_string(o.confidence) + ")\n";
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
  md += "\n## Open questions\n\n";
  Json oq_j = Json::array();
  for (const auto& q : open_qs) {
    md += "- " + q + "\n";
    oq_j.push_back(q);
  }
  data["open_questions"] = oq_j;

  Rendered r;
  r.kind = "self_description";
  r.title = "SELF.md";
  r.markdown = md;
  r.data = data;
  r.product.kind = "self_description";
  r.product.instance = "";
  r.product.run = run;
  r.product.claims = dep_claims;
  r.product.principles = dep_principles;
  r.product.id = model::Product::make_id(r.product.kind, r.product.instance, run);
  data["input_hash"] = input_hash(pack_->hash(), run, dep_claims, dep_principles);
  r.data = data;
  return r;
}

// ── dossier ──────────────────────────────────────────────────────────

Result<Rendered> Materializer::dossier(std::string_view run_arg, std::string_view instance_id) {
  LOOM_TRY_ASSIGN(std::string run, ctx::resolve_run(store_, run_arg));
  LOOM_TRY_ASSIGN(auto inst_opt, store_.get_instance(run, instance_id));
  if (!inst_opt) return Error(Errc::NotFound, "no instance '" + std::string(instance_id) + "' in run " + run);
  const model::Instance& inst = *inst_opt;

  kb::SlotQuery sq;
  sq.instance = inst.id;
  LOOM_TRY_ASSIGN(auto rows, store_.query_slots(run, sq));

  std::string md = "# Dossier: " + inst.subject_label + " (" + inst.paradigm + ")\n\n";
  md += "Coverage: " + json::dump(inst.coverage) + "\n\n";
  md += "## Slots\n\n";
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
    std::string text = row.value.slot + " = " + obj;
    std::string marked = ctx::evidence_markdown(*pack_, claim.assessment.evidence, claim.assessment.origin,
                                                claim.assessment.confidence, text, claim.assessment.expected,
                                                claim.assessment.derivation ? claim.assessment.derivation->op : "",
                                                claim.assessment.open.fill_query);
    md += "- " + marked + (row.value.conflict ? " [CONFLICT]" : "") + "\n";
    slots_j.push_back(Json{{"slot", row.value.slot}, {"claim", claim.id}, {"conflict", row.value.conflict}, {"text", marked}});
    if (row.value.conflict) conflicts_j.push_back(row.value.to_json());
    if (claim.assessment.derivation && !claim.assessment.derivation->morphism.empty()) {
      analogies_j.push_back(Json{{"slot", row.value.slot}, {"morphism", claim.assessment.derivation->morphism}, {"claim", claim.id}});
    }
  }
  Json data{{"instance", inst.to_json()}, {"slots", slots_j}, {"conflicts", conflicts_j}, {"analogies", analogies_j}};

  Rendered r;
  r.kind = "dossier";
  r.title = "Dossier: " + inst.subject_label;
  r.markdown = md;
  r.product.kind = "dossier";
  r.product.instance = inst.id;
  r.product.run = run;
  r.product.claims = dep_claims;
  r.product.id = model::Product::make_id(r.product.kind, r.product.instance, run);
  data["input_hash"] = input_hash(pack_->hash(), run, dep_claims, {});
  r.data = data;
  return r;
}

// ── backlog ──────────────────────────────────────────────────────────

Result<Rendered> Materializer::backlog(std::string_view run_arg) {
  LOOM_TRY_ASSIGN(std::string run, ctx::resolve_run(store_, run_arg));
  std::string md = "# Backlog\n\n";
  Json data = Json::object();
  std::vector<std::string> dep_claims;

  kb::ClaimQuery contested_q;
  contested_q.status = model::ClaimStatus::Contested;
  LOOM_TRY_ASSIGN(auto contested, store_.query_claims(run, contested_q));
  md += "## Contested claims\n\n";
  Json contested_j = Json::array();
  for (const auto& c : contested) {
    md += "- " + c.subject + " " + c.predicate + " (contested)\n";
    dep_claims.push_back(c.id);
    contested_j.push_back(c.to_json());
  }
  data["contested"] = contested_j;

  kb::ClaimQuery inferred_q;
  inferred_q.evidence = model::EvidenceClass::Inferred;
  LOOM_TRY_ASSIGN(auto inferred, store_.query_claims(run, inferred_q));
  md += "\n## Violated expected properties\n\n";
  Json violated_j = Json::array();
  for (const auto& c : inferred) {
    if (c.assessment.check != model::CheckState::Violated) continue;
    md += "- " + c.subject + " " + c.predicate + ": " + (c.assessment.expected ? c.assessment.expected->render() : "") + " [violated]\n";
    dep_claims.push_back(c.id);
    violated_j.push_back(c.to_json());
  }
  data["violated_expected_properties"] = violated_j;

  kb::ClaimQuery violates_q;
  violates_q.predicate = "violates";
  LOOM_TRY_ASSIGN(auto violates, store_.query_claims(run, violates_q));
  md += "\n## Principle / preference violations\n\n";
  Json violates_j = Json::array();
  for (const auto& c : violates) {
    md += "- " + c.subject + " violates " + json::dump(c.value) + "\n";
    dep_claims.push_back(c.id);
    violates_j.push_back(c.to_json());
  }
  data["violations"] = violates_j;

  md += "\n## Absent required slots\n\n";
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
      md += "- " + inst.subject_label + " / " + row.value.slot + ": absent\n";
      absent_j.push_back(Json{{"instance", inst.id}, {"slot", row.value.slot}, {"claim", row.value.claim}});
    }
  }
  data["absent_required_slots"] = absent_j;

  md += "\n## Lost features\n\n";
  Json lost_j = Json::array();
  LOOM_TRY_ASSIGN(auto entities, store_.query_entities(run, kb::EntityQuery{}));
  for (const auto& e : entities) {
    LOOM_TRY_ASSIGN(auto hist, store_.status_history(run, e.id));
    if (hist.empty()) continue;
    const auto& last = hist.back();
    if (last.status != model::StatusValue::Lost) continue;
    md += "- " + e.label + " (branch " + (last.branch.empty() ? "main" : last.branch) + "): lost as of " + last.date +
          (last.oscillation ? " [oscillates]" : "") + "\n";
    lost_j.push_back(last.to_json());
  }
  data["lost_features"] = lost_j;

  Rendered r;
  r.kind = "backlog";
  r.title = "Backlog";
  r.markdown = md;
  r.product.kind = "backlog";
  r.product.run = run;
  r.product.claims = dep_claims;
  r.product.id = model::Product::make_id(r.product.kind, r.product.instance, run);
  data["input_hash"] = input_hash(pack_->hash(), run, dep_claims, {});
  r.data = data;
  return r;
}

// ── extrapolated_spec ────────────────────────────────────────────────

Result<Rendered> Materializer::extrapolated_spec(std::string_view run_arg, std::string_view instance_id) {
  LOOM_TRY_ASSIGN(std::string run, ctx::resolve_run(store_, run_arg));
  LOOM_TRY_ASSIGN(auto inst_opt, store_.get_instance(run, instance_id));
  if (!inst_opt) return Error(Errc::NotFound, "no instance '" + std::string(instance_id) + "' in run " + run);
  const model::Instance& inst = *inst_opt;

  kb::SlotQuery sq;
  sq.instance = inst.id;
  LOOM_TRY_ASSIGN(auto rows, store_.query_slots(run, sq));

  std::string md = "# Extrapolated spec: " + inst.subject_label + "\n\nProposals only — never facts (I3).\n\n## Proposals\n\n";
  Json proposals = Json::array();
  std::vector<std::string> dep_claims;
  for (const auto& row : rows) {
    auto claim_r = store_.get_claim(run, row.value.claim);
    if (!claim_r || !*claim_r || (*claim_r)->assessment.evidence != model::EvidenceClass::Extrapolated) continue;
    const model::Claim& claim = **claim_r;
    dep_claims.push_back(claim.id);
    std::string basis = claim.assessment.derivation ? claim.assessment.derivation->op : "";
    std::string text = row.value.slot + " = " + (claim.object.empty() ? json::dump(claim.value) : claim.object);
    std::string marked = ctx::evidence_markdown(*pack_, claim.assessment.evidence, claim.assessment.origin,
                                                claim.assessment.confidence, text, claim.assessment.expected, basis,
                                                claim.assessment.open.fill_query);
    md += "- " + marked + "\n";
    proposals.push_back(Json{{"slot", row.value.slot}, {"claim", claim.id}, {"basis", basis}});
  }
  if (proposals.empty()) md += "(none)\n";

  Rendered r;
  r.kind = "extrapolated_spec";
  r.title = "Extrapolated spec: " + inst.subject_label;
  r.markdown = md;
  r.product.kind = "extrapolated_spec";
  r.product.instance = inst.id;
  r.product.run = run;
  r.product.claims = dep_claims;
  r.product.id = model::Product::make_id(r.product.kind, r.product.instance, run);
  Json data{{"instance", inst.id}, {"proposals", proposals}};
  data["input_hash"] = input_hash(pack_->hash(), run, dep_claims, {});
  r.data = data;
  return r;
}

// ── check_preferences ────────────────────────────────────────────────

Result<std::vector<model::ProductCheck>> Materializer::check_preferences(const model::Product&,
                                                                         const std::vector<std::string>& files) {
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
    std::string lang = language_of(p);
    if (lang.empty()) continue;
    auto content = fsutil::read_file(p);
    if (!content) continue;
    for (const auto& check : *checks) {
      std::string id = json::get_string(check, "id");
      std::vector<CheckHit> hits;
      auto st = run_detector(check, *content, lang, file, hits);
      if (!st) continue;  // a bad pattern in the pack is a data problem, not a test crash
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
      std::string detail = std::to_string(hits.size()) + " hit(s): ";
      std::size_t shown = 0;
      for (const auto& h : hits) {
        if (shown++ >= 5) {
          detail += "...";
          break;
        }
        detail += h.file + ":" + std::to_string(h.line) + " ";
      }
      pc.detail = detail;
    } else {
      pc.detail = "no hits";
    }
    out.push_back(std::move(pc));
  }
  return out;
}

// ── knowledge.materialize stage ──────────────────────────────────────

Result<Json> run_stage(knowledge::StageContext& ctx) {
  Materializer m(ctx.rt, ctx.store, ctx.pack);
  Json artifacts = Json::array();
  std::vector<model::Product> products;

  auto store_artifact = [&](const Rendered& r, std::string_view file_name) -> Status {
    LOOM_TRY_ASSIGN(BlobRef ref, ctx.rt.blobs().put(r.markdown, "text/markdown"));
    ArtifactRecord ar;
    ar.kind = "knowledge." + r.kind;
    ar.title = r.title;
    ar.blob_hash = ref.hash;
    ar.mime = mime_for(file_name);
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

  LOOM_TRY_ASSIGN(Rendered self, m.self_description(ctx.run));
  LOOM_TRY(store_artifact(self, "SELF.md"));

  LOOM_TRY_ASSIGN(auto instances, ctx.store.query_instances(ctx.run, "", ""));
  int n_dossiers = 0, n_specs = 0;
  for (const auto& inst : instances) {
    auto d = m.dossier(ctx.run, inst.id);
    if (d) {
      LOOM_TRY(store_artifact(*d, "dossier_" + inst.id + ".md"));
      ++n_dossiers;
    }
    // Materialized for every instance, proposals or not: an instance with
    // nothing extrapolated still gets a "(none)" file rather than silently
    // missing one.
    auto spec = m.extrapolated_spec(ctx.run, inst.id);
    if (spec) {
      LOOM_TRY(store_artifact(*spec, "extrapolated_" + inst.id + ".md"));
      ++n_specs;
    }
  }

  LOOM_TRY_ASSIGN(Rendered bl, m.backlog(ctx.run));
  LOOM_TRY(store_artifact(bl, "BACKLOG.md"));

  LOOM_TRY(ctx.store.put_products(ctx.run, products));

  std::vector<std::string> hashes;
  for (const auto& a : artifacts) hashes.push_back(json::get_string(a, "hash"));
  std::sort(hashes.begin(), hashes.end());
  std::string joined;
  for (auto& h : hashes) joined += h + "|";

  return Json{{"output", Sha256::hex(joined)},
              {"artifacts", artifacts},
              {"stats", Json{{"dossiers", n_dossiers}, {"extrapolated_specs", n_specs}, {"products", products.size()}}}};
}

}  // namespace loom::materialize
