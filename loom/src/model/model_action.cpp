// model.h: Layer D (goals, context sets) and Layer E (decisions, forks,
// status records, predictions, products, judgements).
#include <algorithm>
#include <cmath>
#include <map>

#include "model/model_json.h"

namespace loom::model {

using detail::join_key;
using detail::Rd;

// ── Goals and context ───────────────────────────────────────────────
Json GoalType::to_json() const {
  Json res = Json::object();
  for (const auto& [k, v] : resolutions) res[k] = std::string(to_string(v));
  Json bud = Json::object();
  for (auto b : all<ContextBand>()) {
    auto it = budget.find(b);
    bud[std::string(to_string(b))] = it == budget.end() ? 0.0 : it->second;
  }
  return Json{{"id", id},
              {"labels", text_to_json(labels)},
              {"description", description},
              {"cues", cues},
              {"roles", detail::ens(roles)},
              {"principle_levels", detail::ens(principle_levels)},
              {"evidence", detail::ens(evidence)},
              {"resolutions", res},
              {"budget", bud}};
}
Result<GoalType> GoalType::from_json(const Json& j) {
  Rd r(j, "goal_type");
  GoalType x;
  x.id = r.str("id", true);
  x.labels = r.text("labels");
  x.description = r.str("description");
  x.cues = r.object("cues");
  x.roles = r.ens<Role>("roles");
  x.principle_levels = r.ens<PrincipleLevel>("principle_levels");
  x.evidence = r.ens<EvidenceClass>("evidence");
  Json res = r.object("resolutions");
  for (auto it = res.begin(); r.ok() && it != res.end(); ++it) {
    if (it.key() != "*" && !from_string<Role>(it.key())) {
      r.fail("resolutions/" + it.key(), "keys are universal roles or '*'");
      break;
    }
    if (!it.value().is_string()) {
      r.fail("resolutions/" + it.key(), "expected a resolution");
      break;
    }
    auto v = parse<Resolution>(it.value().get<std::string>(), r.sub("resolutions/" + it.key()));
    if (!v) {
      r.fail_with(v.error());
      break;
    }
    x.resolutions[it.key()] = *v;
  }
  Json bud = r.object("budget");
  double sum = 0;
  for (auto it = bud.begin(); r.ok() && it != bud.end(); ++it) {
    auto b = parse<ContextBand>(it.key(), r.sub("budget"));
    if (!b) {
      r.fail_with(b.error());
      break;
    }
    if (!it.value().is_number() || it.value().get<double>() < 0) {
      r.fail("budget/" + it.key(), "expected a non-negative share");
      break;
    }
    x.budget[*b] = it.value().get<double>();
    sum += it.value().get<double>();
  }
  if (r.ok() && !x.budget.empty() && std::fabs(sum - 1.0) > 1e-6) r.fail("budget", "band shares sum to 1");
  if (r.ok() && x.roles.empty()) r.fail("roles", "a goal type names its relevant roles");
  if (!r.ok()) return r.error();
  return x;
}

std::string Goal::make_id(std::string_view type, std::string_view text, const std::vector<std::string>& targets) {
  std::vector<std::string> t = targets;
  std::sort(t.begin(), t.end());
  std::string tj;
  for (const auto& s : t) tj += s + ",";
  return kb::stable_id("g_", join_key({type, text, tj}));
}
Json Goal::to_json() const {
  return Json{{"id", id},           {"type", type},       {"text", text},     {"targets", detail::strs(targets)},
              {"project", project}, {"confidence", confidence}, {"params", params}};
}
Result<Goal> Goal::from_json(const Json& j) {
  Rd r(j, "goal");
  Goal x;
  x.id = r.str("id");
  x.type = r.str("type", true);
  x.text = r.str("text");
  x.targets = r.strs("targets");
  x.project = r.str("project");
  x.confidence = r.unit("confidence", 1.0);
  x.params = r.object("params");
  if (!r.ok()) return r.error();
  return x;
}

Json ContextItem::to_json() const {
  return Json{{"ref_kind", detail::en(ref_kind)},
              {"ref", ref},
              {"band", detail::en(band)},
              {"resolution", detail::en(resolution)},
              {"score", score},
              {"factors", factors},
              {"tokens", tokens},
              {"why", why},
              {"required_by", detail::strs(required_by)},
              {"text", text}};
}
Result<ContextItem> ContextItem::from_json(const Json& j) {
  Rd r(j, "context_item");
  ContextItem x;
  x.ref_kind = r.en<RefKind>("ref_kind", RefKind::Claim, true);
  x.ref = r.str("ref", true);
  x.band = r.en<ContextBand>("band", ContextBand::Goal, true);
  x.resolution = r.en<Resolution>("resolution", Resolution::Summary, true);
  x.score = r.num("score", 0.0);
  x.factors = r.object("factors");
  x.tokens = r.integer("tokens", 0);
  x.why = r.str("why", true);
  x.required_by = r.strs("required_by");
  x.text = r.str("text");
  if (r.ok() && x.tokens < 0) r.fail("tokens", "must be >= 0");
  if (!r.ok()) return r.error();
  return x;
}

std::string ContextSet::make_id(std::string_view goal_id, int budget_tokens, std::string_view pack_hash) {
  return kb::stable_id("cx_", join_key({goal_id, std::to_string(budget_tokens), pack_hash}));
}
Status ContextSet::validate() const {
  int prev = -1;
  long sum = 0;
  for (const auto& it : items) {
    int b = static_cast<int>(it.band);
    if (b < prev) return Error(Errc::InvalidArgument, "context set: items are ordered stable -> project -> goal");
    prev = b;
    sum += it.tokens;
    if (it.why.empty()) return Error(Errc::InvalidArgument, "context set: every item says why it is included");
  }
  if (sum != used_tokens) return Error(Errc::InvalidArgument, "context set: used_tokens is the sum of item tokens");
  if (budget_tokens > 0 && used_tokens > budget_tokens) return Error(Errc::InvalidArgument, "context set: over budget");
  return {};
}
Json ContextSet::to_json() const {
  return Json{{"id", id},
              {"goal", goal.to_json()},
              {"budget_tokens", budget_tokens},
              {"used_tokens", used_tokens},
              {"pack_hash", pack_hash},
              {"items", detail::list(items)},
              {"dropped", detail::list(dropped)}};
}
Result<ContextSet> ContextSet::from_json(const Json& j) {
  Rd r(j, "context_set");
  ContextSet x;
  x.id = r.str("id", true);
  x.goal = r.obj<Goal>("goal", true);
  x.budget_tokens = r.integer("budget_tokens", 0);
  x.used_tokens = r.integer("used_tokens", 0);
  x.pack_hash = r.str("pack_hash");
  x.items = r.list<ContextItem>("items");
  x.dropped = r.list<ContextItem>("dropped");
  if (!r.ok()) return r.error();
  return x;
}

// ── Decisions and forks ─────────────────────────────────────────────
Json ValueImpact::to_json() const {
  return Json{{"value", value}, {"effect", detail::en(effect)}, {"rationale", rationale}};
}
Result<ValueImpact> ValueImpact::from_json(const Json& j) {
  Rd r(j, "value_impact");
  ValueImpact x;
  x.value = r.str("value", true);
  x.effect = r.en<ValueEffect>("effect", ValueEffect::Neutral, true);
  x.rationale = r.str("rationale");
  if (!r.ok()) return r.error();
  return x;
}

Json DecisionAlternative::to_json() const {
  return Json{{"label", label}, {"object", object}, {"value", value}, {"effects", detail::list(effects)}, {"chosen", chosen}};
}
Result<DecisionAlternative> DecisionAlternative::from_json(const Json& j) {
  Rd r(j, "alternative");
  DecisionAlternative x;
  x.label = r.str("label", true);
  x.object = r.str("object");
  x.value = r.raw("value");
  x.effects = r.list<ValueImpact>("effects");
  x.chosen = r.boolean("chosen", false);
  if (!r.ok()) return r.error();
  return x;
}

const DecisionAlternative* Decision::chosen() const noexcept {
  for (const auto& a : alternatives) {
    if (a.chosen) return &a;
  }
  return nullptr;
}
Json Decision::to_json() const {
  return Json{{"id", id},
              {"subject", subject},
              {"alternatives", detail::list(alternatives)},
              {"principles", detail::strs(principles)},
              {"constraints", detail::strs(constraints)},
              {"date", date},
              {"status", detail::en(status)},
              {"superseded_by", superseded_by},
              {"forks", detail::strs(forks)}};
}
Result<Decision> Decision::from_json(const Json& j) {
  Rd r(j, "decision");
  Decision x;
  x.id = r.str("id", true);
  x.subject = r.str("subject", true);
  x.alternatives = r.list<DecisionAlternative>("alternatives", true);
  x.principles = r.strs("principles");
  x.constraints = r.strs("constraints");
  x.date = r.str("date");
  x.status = r.en<DecisionStatus>("status", DecisionStatus::Active);
  x.superseded_by = r.str("superseded_by");
  x.forks = r.strs("forks");
  if (r.ok()) {
    int n = 0;
    for (const auto& a : x.alternatives) n += a.chosen ? 1 : 0;
    if (n != 1) r.fail("alternatives", "a decision records exactly one chosen alternative");
    if (x.status == DecisionStatus::Superseded && x.superseded_by.empty()) {
      r.fail("superseded_by", "a superseded decision names its successor");
    }
  }
  if (!r.ok()) return r.error();
  return x;
}

Json ForkSide::to_json() const {
  return Json{{"ref", ref}, {"label", label}, {"chosen", chosen}, {"abandoned", abandoned}, {"date", date}};
}
Result<ForkSide> ForkSide::from_json(const Json& j) {
  Rd r(j, "fork_side");
  ForkSide x;
  x.ref = r.str("ref", true);
  x.label = r.str("label");
  x.chosen = r.boolean("chosen", false);
  x.abandoned = r.boolean("abandoned", false);
  x.date = r.str("date");
  if (r.ok() && x.chosen && x.abandoned) r.fail("", "a side is not both chosen and abandoned");
  if (!r.ok()) return r.error();
  return x;
}

std::string Fork::make_id(ForkKind kind, std::string_view subject, std::string_view base,
                          const std::vector<ForkSide>& sides) {
  std::vector<std::string> refs;
  for (const auto& s : sides) refs.push_back(s.ref);
  std::sort(refs.begin(), refs.end());
  std::string rj;
  for (const auto& s : refs) rj += s + ",";
  return kb::stable_id("fk_", join_key({to_string(kind), subject, base, rj}));
}
Json Fork::to_json() const {
  return Json{{"id", id},     {"kind", detail::en(kind)}, {"subject", subject},
              {"base", base}, {"sides", detail::list(sides)}, {"date", date}};
}
Result<Fork> Fork::from_json(const Json& j) {
  Rd r(j, "fork");
  Fork x;
  x.id = r.str("id", true);
  x.kind = r.en<ForkKind>("kind", ForkKind::Design, true);
  x.subject = r.str("subject", true);
  x.base = r.str("base");
  x.sides = r.list<ForkSide>("sides", true);
  x.date = r.str("date");
  if (r.ok() && x.sides.size() < 2) r.fail("sides", "a fork keeps at least two sides");
  if (!r.ok()) return r.error();
  return x;
}

// ── Status per branch and version ───────────────────────────────────
std::string StatusRecord::make_id(std::string_view entity, std::string_view branch, std::string_view version,
                                  StatusValue status, std::string_view date) {
  return kb::stable_id("sr_", join_key({entity, branch, version, to_string(status), date}));
}
Json StatusRecord::to_json() const {
  return Json{{"id", id},
              {"entity", entity},
              {"branch", branch},
              {"version", version},
              {"status", detail::en(status)},
              {"date", date},
              {"claim", claim},
              {"previous", detail::opt_en(previous)},
              {"oscillation", oscillation}};
}
Result<StatusRecord> StatusRecord::from_json(const Json& j) {
  Rd r(j, "status_record");
  StatusRecord x;
  x.id = r.str("id", true);
  x.entity = r.str("entity", true);
  x.branch = r.str("branch");
  x.version = r.str("version");
  x.status = r.en<StatusValue>("status", StatusValue::Planned, true);
  x.date = r.str("date");
  x.claim = r.str("claim");
  x.previous = r.opt_en<StatusValue>("previous");
  x.oscillation = r.boolean("oscillation", false);
  if (!r.ok()) return r.error();
  return x;
}

std::vector<StatusRecord> order_status_history(std::vector<StatusRecord> records) {
  auto less = [](const StatusRecord& a, const StatusRecord& b) {
    if (a.entity != b.entity) return a.entity < b.entity;
    if (a.branch != b.branch) return a.branch < b.branch;
    int c = kb::compare_versions(a.version, b.version);
    if (c != 0) return c < 0;
    if (a.version != b.version) return a.version < b.version;  // unparseable versions: text order
    if (a.date != b.date) return a.date < b.date;
    return a.id < b.id;
  };
  std::stable_sort(records.begin(), records.end(), less);
  auto present = [](StatusValue s) {
    return s == StatusValue::Implemented || s == StatusValue::Partial || s == StatusValue::Restored;
  };
  std::size_t i = 0;
  while (i < records.size()) {
    std::size_t j = i;
    bool cycle_done = false;         // a lost -> back cycle happened
    bool returned_after_lost = false;
    std::optional<StatusValue> prev;
    while (j < records.size() && records[j].entity == records[i].entity && records[j].branch == records[i].branch) {
      StatusRecord& rec = records[j];
      rec.previous = prev;
      rec.oscillation = false;
      if (rec.status == StatusValue::Lost) {
        if (returned_after_lost) rec.oscillation = true;  // lost again after being restored
      } else if (present(rec.status) && prev == StatusValue::Lost) {
        if (cycle_done) rec.oscillation = true;  // back again after a second loss
        cycle_done = true;
        returned_after_lost = true;
      }
      prev = rec.status;
      ++j;
    }
    i = j;
  }
  return records;
}

// ── Predictions / products / judgements ─────────────────────────────
std::string Prediction::make_id(std::string_view op, std::string_view situation, std::string_view cut) {
  return kb::stable_id("pn_", join_key({op, situation, cut}));
}
Json Prediction::to_json() const {
  return Json{{"id", id},
              {"situation", situation},
              {"features", features},
              {"solution", solution},
              {"operator", op},
              {"principles", detail::strs(principles)},
              {"confidence", confidence},
              {"cut", cut},
              {"claim", claim},
              {"outcome", detail::en(outcome)},
              {"evaluated_against", detail::strs(evaluated_against)}};
}
Result<Prediction> Prediction::from_json(const Json& j) {
  Rd r(j, "prediction");
  Prediction x;
  x.id = r.str("id", true);
  x.situation = r.str("situation", true);
  x.features = r.object("features");
  x.solution = r.str("solution", true);
  x.op = r.str("operator");
  x.principles = r.strs("principles");
  x.confidence = r.unit("confidence", 0.0);
  x.cut = r.str("cut");
  x.claim = r.str("claim");
  x.outcome = r.en<CheckState>("outcome", CheckState::Pending);
  x.evaluated_against = r.strs("evaluated_against");
  if (!r.ok()) return r.error();
  return x;
}

Json ProductCheck::to_json() const {
  return Json{{"check", check}, {"principle", principle}, {"passed", passed}, {"detail", detail}};
}
Result<ProductCheck> ProductCheck::from_json(const Json& j) {
  Rd r(j, "product_check");
  ProductCheck x;
  x.check = r.str("check", true);
  x.principle = r.str("principle");
  x.passed = r.boolean("passed", false);
  x.detail = r.str("detail");
  if (!r.ok()) return r.error();
  return x;
}

bool Product::passed() const noexcept {
  return std::all_of(checks.begin(), checks.end(), [](const ProductCheck& c) { return c.passed; });
}
std::string Product::make_id(std::string_view kind, std::string_view instance, std::string_view run) {
  return kb::stable_id("pd_", join_key({kind, instance, run}));
}
Json Product::to_json() const {
  return Json{{"id", id},
              {"kind", kind},
              {"instance", instance},
              {"artifact", artifact},
              {"claims", detail::strs(claims)},
              {"principles", detail::strs(principles)},
              {"checks", detail::list(checks)},
              {"run", run},
              {"passed", passed()}};
}
Result<Product> Product::from_json(const Json& j) {
  Rd r(j, "product");
  Product x;
  x.id = r.str("id", true);
  x.kind = r.str("kind", true);
  x.instance = r.str("instance");
  x.artifact = r.str("artifact");
  x.claims = r.strs("claims");
  x.principles = r.strs("principles");
  x.checks = r.list<ProductCheck>("checks");
  x.run = r.str("run");
  if (!r.ok()) return r.error();
  return x;
}

std::string Judgement::make_id(std::string_view created, std::string_view target, Verdict v, const Json& payload) {
  return kb::stable_id("ju_", join_key({created, target, to_string(v), json::canonical(payload)}));
}
Status Judgement::validate() const {
  auto bad = [&](const std::string& m) {
    return Error(Errc::InvalidArgument, "judgement " + std::string(to_string(verdict)) + " on " +
                                            std::string(to_string(target_kind)) + " " + target + ": " + m);
  };
  if (target.empty()) return bad("target is required");
  if (!payload.is_object()) return bad("payload is an object");
  switch (verdict) {
    case Verdict::Confirm:
    case Verdict::Reject:
      return {};
    case Verdict::Edit:
      if (payload.empty()) return bad("edit carries the new fields in the payload");
      if (target_kind == RefKind::Claim && !payload.contains("value") && !payload.contains("object")) {
        return bad("editing a claim sets 'value' or 'object'");
      }
      if (target_kind == RefKind::Unit || target_kind == RefKind::Observation) {
        return bad("sources and observations are immutable (I1)");
      }
      return {};
    case Verdict::Merge:
      if (target_kind != RefKind::Entity) return bad("only entities are merged");
      if (json::get_string(payload, "into").empty()) return bad("merge names the entity to merge 'into'");
      if (json::get_string(payload, "into") == target) return bad("an entity is not merged into itself");
      return {};
    case Verdict::Split: {
      if (target_kind != RefKind::Entity) return bad("only entities are split");
      const Json* a = json::find(payload, "aliases");
      if (!a || !a->is_array() || a->empty()) return bad("split lists the alias keys that move out");
      if (json::get_string(payload, "key").empty()) return bad("split names the new canonical 'key'");
      return {};
    }
  }
  return {};
}
Json Judgement::to_json() const {
  return Json{{"id", id},         {"seq", seq},     {"target_kind", detail::en(target_kind)},
              {"target", target}, {"verdict", detail::en(verdict)}, {"payload", payload},
              {"author", author}, {"reason", reason}, {"created", created}};
}
Result<Judgement> Judgement::from_json(const Json& j) {
  Rd r(j, "judgement");
  Judgement x;
  x.id = r.str("id");
  x.seq = r.i64("seq", 0);
  x.target_kind = r.en<RefKind>("target_kind", RefKind::Claim, true);
  x.target = r.str("target", true);
  x.verdict = r.en<Verdict>("verdict", Verdict::Confirm, true);
  x.payload = r.object("payload");
  x.author = r.str("author", false, "user");
  x.reason = r.str("reason");
  x.created = r.str("created");
  if (!r.ok()) return r.error();
  LOOM_TRY(x.validate());
  return x;
}

}  // namespace loom::model
