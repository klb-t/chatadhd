// model.h: closed-set semantics, shared small types, Layer A (units,
// observations) and Layer B (entities, claims, assessments, models).
#include <algorithm>
#include <cmath>
#include <exception>

#include "loom/util/sha256.h"
#include "model/model_json.h"
#include "model/model_profile.h"

namespace loom::model {

using detail::Rd;

namespace detail {
std::string join_key(std::initializer_list<std::string_view> parts) {
  std::string out;
  bool first = true;
  for (auto p : parts) {
    if (!first) out += '\x1f';
    first = false;
    out += p;
  }
  return out;
}
}  // namespace detail

using detail::join_key;

// ── closed-set semantics ────────────────────────────────────────────
bool may_be_premise(EvidenceClass e) noexcept {
  switch (e) {
    case EvidenceClass::Observed:
    case EvidenceClass::Derived:
    case EvidenceClass::Inferred:
    case EvidenceClass::User:
      return true;
    case EvidenceClass::Extrapolated:
    case EvidenceClass::Absent:
      return false;
  }
  return false;
}

int authority_rank(Origin o) noexcept {
  struct Ranks { std::array<int, count<Origin>()> known{}; int unknown = 0; };
  static const Ranks ranks = [] {
    auto profile = RuntimeProfile::builtin("model");
    // A malformed embedded descriptor is a build defect. noexcept cannot
    // return an error, and inventing replacement arbitration would be wrong.
    if (!profile) std::terminate();
    Ranks out;
    const auto& values = profile->values();
    for (const auto origin : all<Origin>())
      out.known[static_cast<std::size_t>(origin)] = json::find(values.at("authority_ranks"), to_string(origin))->get<int>();
    out.unknown = values.at("unknown_origin_rank").get<int>();
    return out;
  }();
  const auto index = static_cast<std::size_t>(o);
  return index < ranks.known.size() ? ranks.known[index] : ranks.unknown;
}

Result<int> authority_rank(Origin o, const RuntimeProfile& profile) {
  LOOM_TRY_ASSIGN(auto checked, detail::checked_model_profile(profile));
  const auto* rank = json::find(checked.values().at("authority_ranks"), to_string(o));
  return rank ? rank->get<int>() : checked.values().at("unknown_origin_rank").get<int>();
}

bool is_producible(EvidenceClass e) noexcept {
  return e == EvidenceClass::Derived || e == EvidenceClass::Inferred || e == EvidenceClass::Extrapolated;
}

// ── Text / Reference ────────────────────────────────────────────────
Json text_to_json(const Text& t) {
  Json o = Json::object();
  for (const auto& [k, v] : t) o[k] = v;
  return o;
}

Result<Text> text_from_json(const Json& j, std::string_view field) {
  if (j.is_null()) return Text{};
  if (!j.is_object()) return Error(Errc::InvalidArgument, std::string(field) + ": expected {\"en\":...,\"pl\":...}");
  Text t;
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (!it.value().is_string()) {
      return Error(Errc::InvalidArgument, std::string(field) + "/" + it.key() + ": expected a string");
    }
    t[it.key()] = it.value().get<std::string>();
  }
  return t;
}

bool Reference::empty() const noexcept {
  return doc.empty() && section.empty() && claim.empty() && observation.empty() && note.empty();
}
Json Reference::to_json() const {
  return Json{{"doc", doc},     {"section", section},         {"date", date},
              {"claim", claim}, {"observation", observation}, {"note", note}};
}
Result<Reference> Reference::from_json(const Json& j) {
  Rd r(j, "reference");
  Reference x;
  x.doc = r.str("doc");
  x.section = r.str("section");
  x.date = r.str("date");
  if (r.ok() && !x.date.empty()) {
    const std::string& d = x.date;
    bool iso = d.size() >= 10 && d[4] == '-' && d[7] == '-';
    for (std::size_t i = 0; iso && i < 10; ++i) iso = (i == 4 || i == 7) || (d[i] >= '0' && d[i] <= '9');
    if (!iso) r.fail("date", "expected YYYY-MM-DD");
  }
  x.claim = r.str("claim");
  x.observation = r.str("observation");
  x.note = r.str("note");
  if (r.ok() && x.empty()) r.fail("", "a reference names a doc, a claim, an observation or a note");
  if (!r.ok()) return r.error();
  return x;
}

// ── Layer A ─────────────────────────────────────────────────────────
bool Locator::empty() const noexcept {
  return source.empty() && member.empty() && json_pointer.empty() && !byte_start && !byte_len && !time_start &&
         !time_end && !line;
}
Json Locator::to_json() const {
  auto oi = [](const std::optional<std::int64_t>& v) { return v ? Json(*v) : Json(nullptr); };
  auto od = [](const std::optional<double>& v) { return v ? Json(*v) : Json(nullptr); };
  return Json{{"source", source},
              {"member", member},
              {"json_pointer", json_pointer},
              {"byte_start", oi(byte_start)},
              {"byte_len", oi(byte_len)},
              {"time_start", od(time_start)},
              {"time_end", od(time_end)},
              {"line", line ? Json(*line) : Json(nullptr)}};
}
Result<Locator> Locator::from_json(const Json& j) {
  Rd r(j, "locator");
  Locator x;
  x.source = r.str("source");
  x.member = r.str("member");
  x.json_pointer = r.str("json_pointer");
  x.byte_start = r.opt_i64("byte_start");
  x.byte_len = r.opt_i64("byte_len");
  x.time_start = r.opt_num("time_start");
  x.time_end = r.opt_num("time_end");
  if (auto l = r.opt_i64("line")) x.line = static_cast<int>(*l);
  if (r.ok() && ((x.byte_start && *x.byte_start < 0) || (x.byte_len && *x.byte_len < 0))) {
    r.fail("byte_start", "byte ranges are non-negative");
  }
  if (r.ok() && x.time_start && x.time_end && *x.time_end < *x.time_start) r.fail("time_end", "ends before it starts");
  if (!r.ok()) return r.error();
  return x;
}

std::string Unit::make_id(std::string_view src, const Locator& loc) {
  return kb::stable_id("un_", join_key({src, json::canonical(loc.to_json())}));
}
Json Unit::to_json() const {
  return Json{{"id", id},       {"source", source},     {"kind", kind},  {"locator", locator.to_json()},
              {"title", title}, {"date", date},         {"artifact_type", artifact_type},
              {"lang", lang},   {"bytes", bytes},       {"attrs", attrs}};
}
Result<Unit> Unit::from_json(const Json& j) {
  Rd r(j, "unit");
  Unit x;
  x.id = r.str("id", true);
  x.source = r.str("source", true);
  x.kind = r.str("kind", true);
  x.locator = r.obj<Locator>("locator");
  x.title = r.str("title");
  x.date = r.str("date");
  x.artifact_type = r.str("artifact_type");
  x.lang = r.str("lang");
  x.bytes = r.i64("bytes", 0);
  x.attrs = r.object("attrs");
  if (!r.ok()) return r.error();
  return x;
}

std::string Observation::make_id(std::string_view u, const Locator& loc, std::string_view text) {
  return kb::stable_id("ob_", join_key({u, json::canonical(loc.to_json()), Sha256::hex(text)}));
}
Json Observation::to_json() const {
  return Json{{"id", id},
              {"unit", unit},
              {"kind", detail::en(kind)},
              {"text", text},
              {"locator", locator.to_json()},
              {"lang", lang},
              {"date", date},
              {"ordinal", ordinal},
              {"artifact_type", artifact_type},
              {"speaker", speaker},
              {"attrs", attrs}};
}
Result<Observation> Observation::from_json(const Json& j) {
  Rd r(j, "observation");
  Observation x;
  x.id = r.str("id", true);
  x.unit = r.str("unit", true);
  x.kind = r.en<ObservationKind>("kind", ObservationKind::Sentence, true);
  x.text = r.str("text");
  x.locator = r.obj<Locator>("locator");
  x.lang = r.str("lang");
  x.date = r.str("date");
  x.ordinal = r.integer("ordinal", 0);
  x.artifact_type = r.str("artifact_type");
  x.speaker = r.str("speaker");
  x.attrs = r.object("attrs");
  if (!r.ok()) return r.error();
  return x;
}

// ── Layer B ─────────────────────────────────────────────────────────
Json Alias::to_json() const {
  return Json{{"key", key},     {"surface", surface}, {"lang", lang},
              {"method", method}, {"count", count},   {"confidence", confidence}};
}
Result<Alias> Alias::from_json(const Json& j) {
  Rd r(j, "alias");
  Alias x;
  x.key = r.str("key", true);
  x.surface = r.str("surface");
  x.lang = r.str("lang");
  x.method = r.str("method", false, "mined");
  x.count = r.integer("count", 0);
  x.confidence = r.unit("confidence", 1.0);
  if (!r.ok()) return r.error();
  return x;
}

std::string Entity::make_id(std::string_view kind, std::string_view canonical_key) {
  return kb::stable_id("e_", join_key({kind, canonical_key}));
}
Json Entity::to_json() const {
  return Json{{"id", id},
              {"kind", kind},
              {"canonical_key", canonical_key},
              {"label", label},
              {"labels", text_to_json(labels)},
              {"aliases", detail::list(aliases)},
              {"parent", parent},
              {"first_seen", first_seen},
              {"last_seen", last_seen},
              {"evidence_class", detail::en(evidence)},
              {"origin", detail::en(origin)},
              {"confidence", confidence},
              {"status", detail::en(status)},
              {"attrs", attrs}};
}
Result<Entity> Entity::from_json(const Json& j) {
  Rd r(j, "entity");
  Entity x;
  x.id = r.str("id", true);
  x.kind = r.str("kind", true);
  x.canonical_key = r.str("canonical_key", true);
  x.label = r.str("label");
  x.labels = r.text("labels");
  x.aliases = r.list<Alias>("aliases");
  x.parent = r.str("parent");
  x.first_seen = r.str("first_seen");
  x.last_seen = r.str("last_seen");
  x.evidence = r.en<EvidenceClass>("evidence_class", EvidenceClass::Observed);
  x.origin = r.en<Origin>("origin", Origin::Archive);
  x.confidence = r.unit("confidence", 1.0);
  x.status = r.en<ClaimStatus>("status", ClaimStatus::Active);
  x.attrs = r.object("attrs");
  if (!r.ok()) return r.error();
  return x;
}

Json Support::to_json() const {
  return Json{{"observation", observation},
              {"locator", locator.to_json()},
              {"quote", quote},
              {"extractor", extractor},
              {"quality", quality}};
}
Result<Support> Support::from_json(const Json& j) {
  Rd r(j, "support");
  Support x;
  x.observation = r.str("observation", true);
  x.locator = r.obj<Locator>("locator");
  x.quote = r.str("quote");
  x.extractor = r.str("extractor");
  x.quality = r.unit("quality", 1.0);
  if (!r.ok()) return r.error();
  return x;
}

Json Derivation::to_json() const {
  return Json{{"operator", op}, {"operator_version", op_version}, {"morphism", morphism}, {"depth", depth}};
}
Result<Derivation> Derivation::from_json(const Json& j) {
  Rd r(j, "derivation");
  Derivation x;
  x.op = r.str("operator", true);
  x.op_version = r.integer("operator_version", 1);
  x.morphism = r.str("morphism");
  x.depth = r.integer("depth", 0);
  if (r.ok() && (x.depth < 0 || x.op_version < 1)) r.fail("depth", "depth >= 0 and operator_version >= 1");
  if (!r.ok()) return r.error();
  return x;
}

Json Alternative::to_json() const { return Json{{"object", object}, {"value", value}, {"score", score}}; }
Result<Alternative> Alternative::from_json(const Json& j) {
  Rd r(j, "alternative");
  Alternative x;
  x.object = r.str("object");
  x.value = r.raw("value");
  x.score = r.num("score", 0.0);
  if (r.ok() && x.object.empty() && x.value.is_null()) r.fail("", "an alternative has an object or a value");
  if (!r.ok()) return r.error();
  return x;
}

bool Premises::empty() const noexcept { return claims.empty() && principles.empty() && assumptions.empty(); }
Json Premises::to_json() const {
  return Json{{"claims", detail::strs(claims)},
              {"principles", detail::strs(principles)},
              {"assumptions", detail::strs(assumptions)}};
}
Result<Premises> Premises::from_json(const Json& j) {
  Rd r(j, "premises");
  Premises x;
  x.claims = r.strs("claims");
  x.principles = r.strs("principles");
  x.assumptions = r.strs("assumptions");
  if (!r.ok()) return r.error();
  return x;
}

bool Counter::empty() const noexcept { return observations.empty() && claims.empty(); }
Json Counter::to_json() const {
  return Json{{"observations", detail::strs(observations)}, {"claims", detail::strs(claims)}};
}
Result<Counter> Counter::from_json(const Json& j) {
  Rd r(j, "counter");
  Counter x;
  x.observations = r.strs("observations");
  x.claims = r.strs("claims");
  if (!r.ok()) return r.error();
  return x;
}

bool Consequences::empty() const noexcept { return claims.empty() && predictions.empty() && checks.empty(); }
Json Consequences::to_json() const {
  return Json{{"claims", detail::strs(claims)},
              {"predictions", detail::strs(predictions)},
              {"checks", detail::strs(checks)}};
}
Result<Consequences> Consequences::from_json(const Json& j) {
  Rd r(j, "consequences");
  Consequences x;
  x.claims = r.strs("claims");
  x.predictions = r.strs("predictions");
  x.checks = r.strs("checks");
  if (!r.ok()) return r.error();
  return x;
}

bool Open::empty() const noexcept { return slots.empty() && questions.empty() && fill_query.is_null(); }
Json Open::to_json() const {
  return Json{{"slots", detail::strs(slots)}, {"questions", detail::strs(questions)}, {"fill_query", fill_query}};
}
Result<Open> Open::from_json(const Json& j) {
  Rd r(j, "open");
  Open x;
  x.slots = r.strs("slots");
  x.questions = r.strs("questions");
  x.fill_query = r.raw("fill_query");
  if (!r.ok()) return r.error();
  return x;
}

Status Assessment::validate() const {
  auto bad = [](const std::string& m) { return Error(Errc::InvalidArgument, "assessment: " + m); };
  if (!std::isfinite(confidence) || confidence < 0.0 || confidence > 1.0) return bad("confidence must be in [0, 1]");
  switch (evidence) {
    case EvidenceClass::Observed:
      if (support.empty()) return bad("an observed claim needs at least one supporting observation");
      break;
    case EvidenceClass::Absent:
      if (!support.empty()) return bad("an absent claim has no support");
      break;
    case EvidenceClass::Derived:
    case EvidenceClass::Inferred:
    case EvidenceClass::Extrapolated:
      if (!derivation || derivation->op.empty()) {
        return bad(std::string(to_string(evidence)) + " claims name the operator that produced them");
      }
      break;
    case EvidenceClass::User:
      break;
  }
  if (evidence == EvidenceClass::Inferred) {
    if (!expected) return bad("an inferred claim carries an expected property (§2.4)");
    if (check == CheckState::NotApplicable) return bad("an inferred claim has a check state (pending|holds|violated)");
  }
  if (check != CheckState::NotApplicable && !expected) return bad("a check state needs an expected property");
  for (const auto& a : alternatives) {
    if (!std::isfinite(a.score)) return bad("alternative scores are finite");
  }
  return {};
}

Json Assessment::to_json() const {
  return Json{{"basis", Json{{"support", detail::list(support)}, {"derivation", detail::opt(derivation)}}},
              {"evidence_class", detail::en(evidence)},
              {"origin", detail::en(origin)},
              {"confidence", confidence},
              {"premises", premises.to_json()},
              {"counter", counter.to_json()},
              {"status", detail::en(status)},
              {"consequences", consequences.to_json()},
              {"open", open.to_json()},
              {"expected_property", expected ? expected->to_json() : Json(nullptr)},
              {"check_state", detail::en(check)},
              {"alternatives", detail::list(alternatives)}};
}

Result<Assessment> Assessment::from_json(const Json& j) {
  Rd r(j, "assessment");
  Assessment x;
  if (const Json* b = r.get("basis")) {
    Rd rb(*b, r.sub("basis"));
    x.support = rb.list<Support>("support");
    x.derivation = rb.opt_obj<Derivation>("derivation");
    if (!rb.ok()) return rb.error();
  }
  x.evidence = r.en<EvidenceClass>("evidence_class", EvidenceClass::Observed, true);
  x.origin = r.en<Origin>("origin", Origin::Archive, true);
  x.confidence = r.unit("confidence", 0.0, true);
  x.premises = r.obj<Premises>("premises");
  x.counter = r.obj<Counter>("counter");
  x.status = r.en<ClaimStatus>("status", ClaimStatus::Active);
  x.consequences = r.obj<Consequences>("consequences");
  x.open = r.obj<Open>("open");
  x.expected = r.expected("expected_property");
  x.check = r.en<CheckState>("check_state", CheckState::NotApplicable);
  x.alternatives = r.list<Alternative>("alternatives");
  if (!r.ok()) return r.error();
  return x;
}

bool Qualifiers::empty() const noexcept {
  return valid_from.empty() && valid_to.empty() && version.empty() && branch.empty() && scope.empty() &&
         lang.empty() && (extra.is_null() || extra.empty());
}
Json Qualifiers::to_json() const {
  return Json{{"valid_from", valid_from}, {"valid_to", valid_to}, {"version", version}, {"branch", branch},
              {"scope", scope},           {"lang", lang},         {"extra", extra}};
}
Result<Qualifiers> Qualifiers::from_json(const Json& j) {
  Rd r(j, "qualifiers");
  Qualifiers x;
  x.valid_from = r.str("valid_from");
  x.valid_to = r.str("valid_to");
  x.version = r.str("version");
  x.branch = r.str("branch");
  x.scope = r.str("scope");
  x.lang = r.str("lang");
  x.extra = r.object("extra");
  if (!r.ok()) return r.error();
  return x;
}

std::string Claim::make_id(std::string_view subject, std::string_view predicate, std::string_view object,
                           const Json& value, const Qualifiers& q) {
  std::string obj = object.empty() ? "v:" + json::canonical(value) : "o:" + std::string(object);
  return kb::stable_id("cl_", join_key({subject, predicate, obj, json::canonical(q.to_json())}));
}
std::string Claim::content_key() const {
  std::string obj = object.empty() ? "v:" + json::canonical(value) : "o:" + object;
  return join_key({subject, predicate, obj, json::canonical(qualifiers.to_json())});
}
Status Claim::validate() const {
  auto bad = [&](const std::string& m) { return Error(Errc::InvalidArgument, "claim " + id + ": " + m); };
  if (subject.empty()) return bad("subject is required");
  if (predicate.empty()) return bad("predicate is required");
  bool has_obj = !object.empty();
  bool has_val = !value.is_null();
  if (is_absent()) {
    if (has_obj || has_val) return bad("an absent claim has neither object nor value");
  } else if (has_obj == has_val) {
    return bad("a claim has exactly one of object (entity) and value (literal)");
  }
  return assessment.validate();
}
Json Claim::to_json() const {
  return Json{{"id", id},       {"subject", subject},
              {"predicate", predicate}, {"object", object},
              {"value", value}, {"qualifiers", qualifiers.to_json()},
              {"assessment", assessment.to_json()}};
}
Result<Claim> Claim::from_json(const Json& j) {
  Rd r(j, "claim");
  Claim x;
  x.id = r.str("id", true);
  x.subject = r.str("subject", true);
  x.predicate = r.str("predicate", true);
  x.object = r.str("object");
  x.value = r.raw("value");
  x.qualifiers = r.obj<Qualifiers>("qualifiers");
  x.assessment = r.obj<Assessment>("assessment", true);
  if (!r.ok()) return r.error();
  return x;
}

std::string Model::make_id(std::string_view name) { return kb::stable_id("md_", name); }
Json Model::to_json() const {
  return Json{{"id", id},
              {"name", name},
              {"description", description},
              {"claims", detail::strs(claims)},
              {"principles", detail::strs(principles)},
              {"explains", detail::strs(explains)},
              {"explanatory", explanatory},
              {"predictive", predictive},
              {"validation_status", detail::en(validation)}};
}
Result<Model> Model::from_json(const Json& j) {
  Rd r(j, "model");
  Model x;
  x.id = r.str("id", true);
  x.name = r.str("name", true);
  x.description = r.str("description");
  x.claims = r.strs("claims");
  x.principles = r.strs("principles");
  x.explains = r.strs("explains");
  x.explanatory = r.num("explanatory", 0.0);
  x.predictive = r.num("predictive", 0.0);
  x.validation = r.en<ValidationStatus>("validation_status", ValidationStatus::Candidate);
  if (!r.ok()) return r.error();
  return x;
}

}  // namespace loom::model
