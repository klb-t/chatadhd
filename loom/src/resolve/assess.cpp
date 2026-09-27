// Assessment (resolve.h): calibrated confidence and conflict detection.
//
// calibrate(): observed claims -> raw = unit-deduplicated noisy-OR of
//   r(extractor) * quality (evidence from one conversation / file counts
//   once, its best support), r = Beta mean from policy/calibration.json;
//   derived/inferred/extrapolated keep their producer's raw confidence;
//   then the isotonic map of the evidence class. user -> 1, absent -> 0.
// detect_conflicts(): two or more active, supported, incompatible values of a
//   single-valued (subject, predicate, qualifiers) -> every value stays and is
//   marked contested; the conflict records the candidate resolution: user,
//   authority (origin rank), reversal (an explicit reversal cue), later
//   decision (the most recent), or open.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "loom/resolve.h"

namespace loom::resolve {

Json Conflict::to_json() const {
  Json c = Json::array();
  for (const auto& x : claims) c.push_back(x);
  return Json{{"subject", subject}, {"predicate", predicate}, {"claims", c}, {"resolution", resolution}, {"winner", winner}};
}

namespace {

std::string reliability_key(std::string_view ex) {
  static const std::map<std::string, std::string, std::less<>> kMap = {
      {"extract.alias", "extractor.alias"},         {"extract.gazetteer", "extractor.gazetteer"},
      {"extract.names", "extractor.enumeration"},   {"extract.relation_patterns", "extractor.pattern"},
      {"extract.versions", "extractor.version"},    {"extract.forks", "extractor.fork"},
      {"extract.code_symbols", "extractor.symbol"}, {"extract.citations", "extractor.pattern"},
      {"extract.dates", "extractor.pattern"},       {"extract.speakers", "extractor.section"},
      {"extract.headers", "extractor.section"},     {"extract.areas", "extractor.section"},
      {"extract.generalizations", "extractor.section"}, {"extract.options", "extractor.enumeration"},
      {"extract.items", "extractor.item"},          {"extract.decisions", "extractor.item"},
      {"extract.status_cues", "extractor.item"},    {"extract.normative", "extractor.item"},
      {"resolve.code_lineage", "extractor.codebase"}, {"resolve.same_as", "extractor.alias"},
  };
  std::string_view base = ex.substr(0, ex.find('@'));
  auto it = kMap.find(base);
  return it == kMap.end() ? "rule.default" : it->second;
}

double isotonic(const Json& points, double x) {
  if (!points.is_array() || points.empty()) return x;
  std::vector<std::pair<double, double>> p;
  for (const auto& pt : points) {
    if (pt.is_array() && pt.size() == 2 && pt[0].is_number() && pt[1].is_number()) p.emplace_back(pt[0].get<double>(), pt[1].get<double>());
  }
  if (p.empty()) return x;
  std::sort(p.begin(), p.end());
  if (x <= p.front().first) return p.front().second;
  if (x >= p.back().first) return p.back().second;
  for (std::size_t i = 1; i < p.size(); ++i) {
    if (x <= p[i].first) {
      double t = (x - p[i - 1].first) / std::max(1e-12, p[i].first - p[i - 1].first);
      return p[i - 1].second + t * (p[i].second - p[i - 1].second);
    }
  }
  return x;
}

// The unit a support comes from: source + member + first JSON pointer step
// (one conversation of an export).
std::string unit_key(const model::Locator& l) {
  std::string p = l.json_pointer;
  if (!p.empty() && p[0] == '/') {
    std::size_t s = p.find('/', 1);
    p = p.substr(0, s);
  }
  return l.source + "|" + l.member + "|" + p;
}

double round4(double x) { return std::round(x * 1e4) / 1e4; }

}  // namespace

Status calibrate(const kb::Pack& pack, std::vector<model::Claim>& claims) {
  const Json& cal = pack.policy("calibration");
  std::map<std::string, double> rel;
  if (const Json* r = json::find(cal, "reliability"); r && r->is_object()) {
    for (auto it = r->begin(); it != r->end(); ++it) {
      double a = json::get_number(it.value(), "alpha", 1), b = json::get_number(it.value(), "beta", 1);
      rel[it.key()] = a / std::max(1e-9, a + b);
    }
  }
  const Json* iso = json::find(cal, "isotonic");
  auto map_of = [&](model::EvidenceClass e) -> const Json& {
    static const Json empty = Json::array();
    if (!iso) return empty;
    const Json* p = json::find(*iso, model::to_string(e));
    return p ? *p : empty;
  };
  for (auto& c : claims) {
    auto& a = c.assessment;
    double raw = a.confidence;
    switch (a.evidence) {
      case model::EvidenceClass::User:
        a.confidence = 1.0;
        continue;
      case model::EvidenceClass::Absent:
        a.confidence = 0.0;
        continue;
      case model::EvidenceClass::Observed: {
        std::map<std::string, double> best;  // unit -> best r*q
        for (const auto& s : a.support) {
          auto it = rel.find(reliability_key(s.extractor));
          double r = it == rel.end() ? rel["rule.default"] : it->second;
          if (r <= 0) r = 0.7;
          double v = std::clamp(r * s.quality, 0.0, 1.0);
          auto& b = best[unit_key(s.locator)];
          b = std::max(b, v);
        }
        double miss = 1.0;
        for (const auto& [u, v] : best) miss *= 1.0 - v;
        raw = best.empty() ? raw : 1.0 - miss;
        break;
      }
      default:
        break;
    }
    a.confidence = round4(std::clamp(isotonic(map_of(a.evidence), std::clamp(raw, 0.0, 1.0)), 0.0, 1.0));
  }
  return {};
}

namespace {

// Single-valued predicates: two different values at the same qualifiers are
// incompatible. (Open relation types stay multi-valued unless listed here.)
bool functional(std::string_view p) { return p == "has_status" || p == "has_purpose" || p == "in_key" || p == "decides"; }

}  // namespace

Result<std::vector<Conflict>> detect_conflicts(std::vector<model::Claim>& claims) {
  std::map<std::string, std::vector<std::size_t>> groups;
  for (std::size_t i = 0; i < claims.size(); ++i) {
    const auto& c = claims[i];
    if (!functional(c.predicate) || c.assessment.status == model::ClaimStatus::Rejected ||
        c.assessment.status == model::ClaimStatus::Superseded || c.is_absent()) {
      continue;
    }
    // qualifiers other than dates / language define "the same slot"
    Json q{{"version", c.qualifiers.version}, {"branch", c.qualifiers.branch}, {"scope", c.qualifiers.scope}};
    if (c.predicate == "has_status" || c.predicate == "decides") q["valid_from"] = c.qualifiers.valid_from;
    groups[c.subject + '\x1f' + c.predicate + '\x1f' + json::canonical(q)].push_back(i);
  }
  std::vector<Conflict> out;
  for (auto& [key, idx] : groups) {
    std::set<std::string> values;
    for (std::size_t i : idx) values.insert(claims[i].object.empty() ? json::canonical(claims[i].value) : claims[i].object);
    if (values.size() < 2) continue;
    Conflict cf;
    cf.subject = claims[idx.front()].subject;
    cf.predicate = claims[idx.front()].predicate;
    for (std::size_t i : idx) cf.claims.push_back(claims[i].id);
    std::sort(cf.claims.begin(), cf.claims.end());
    // candidate resolution
    auto rank = [&](std::size_t i) { return model::authority_rank(claims[i].assessment.origin); };
    std::vector<std::size_t> users;
    for (std::size_t i : idx) {
      if (claims[i].assessment.evidence == model::EvidenceClass::User) users.push_back(i);
    }
    auto latest = [&](const std::vector<std::size_t>& v) {
      std::size_t w = v.front();
      for (std::size_t i : v) {
        std::string di = claims[i].qualifiers.valid_from, dw = claims[w].qualifiers.valid_from;
        if (di > dw || (di == dw && claims[i].id < claims[w].id)) w = i;
      }
      return w;
    };
    int top = -1;
    for (std::size_t i : idx) top = std::max(top, rank(i));
    std::vector<std::size_t> topv;
    for (std::size_t i : idx) {
      if (rank(i) == top) topv.push_back(i);
    }
    std::vector<std::size_t> reversals;
    for (std::size_t i : idx) {
      if (json::get_bool(claims[i].qualifiers.extra, "reversal")) reversals.push_back(i);
    }
    std::set<std::string> dates;
    for (std::size_t i : idx) dates.insert(claims[i].qualifiers.valid_from);
    if (!users.empty()) {
      cf.resolution = "user";
      cf.winner = claims[latest(users)].id;
    } else if (topv.size() < idx.size()) {
      cf.resolution = "authority";
      cf.winner = topv.size() == 1 ? claims[topv.front()].id : claims[latest(topv)].id;
    } else if (!reversals.empty()) {
      cf.resolution = "reversal";
      cf.winner = claims[latest(reversals)].id;
    } else if (dates.size() > 1 && !dates.count("")) {
      cf.resolution = "later_decision";
      cf.winner = claims[latest(idx)].id;
    } else {
      cf.resolution = "open";
    }
    for (std::size_t i : idx) {
      auto& a = claims[i].assessment;
      a.status = model::ClaimStatus::Contested;
      for (std::size_t j : idx) {
        if (j != i && std::find(a.counter.claims.begin(), a.counter.claims.end(), claims[j].id) == a.counter.claims.end()) {
          a.counter.claims.push_back(claims[j].id);
        }
      }
      std::sort(a.counter.claims.begin(), a.counter.claims.end());
    }
    out.push_back(std::move(cf));
  }
  std::sort(out.begin(), out.end(), [](const Conflict& a, const Conflict& b) {
    return std::tie(a.subject, a.predicate, a.claims) < std::tie(b.subject, b.predicate, b.claims);
  });
  return out;
}

}  // namespace loom::resolve
