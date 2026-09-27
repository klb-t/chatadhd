#include <doctest/doctest.h>
#include <cstdlib>
#include <iostream>
#include "loom/extract.h"
#include "loom/kb.h"
#include "test_helpers.h"
using namespace loom;
TEST_CASE("zz debug unit") {
  const char* conv = std::getenv("ZZ_CONV");
  if (!conv) return;
  auto p = loom::test::unwrap(kb::Pack::load_builtin());
  std::vector<extract::UnitContent> units;
  auto dir = std::filesystem::path(LOOM_TEST_FIXTURES) / "eval" / "synthetic_dev";
  for (const char* z : {"chatgpt_export.zip", "claude_export.zip"}) {
    for (auto& u : loom::test::unwrap(extract::read_units(dir / z))) units.push_back(std::move(u));
  }
  auto all = extract::extract_units(p, units);
  std::string unit;
  for (const auto& u : units) {
    if (json::get_string(u.unit.attrs, "ext_id") == conv) unit = u.unit.id;
  }
  std::set<std::string> obs;
  for (const auto& o : all.observations) {
    if (o.unit == unit) {
      obs.insert(o.id);
      std::cerr << "OBS " << json::get_string(o.attrs, "node") << " " << model::to_string(o.kind) << ": " << o.text << "\n";
    }
  }
  for (const auto& c : all.claims) {
    bool in = false;
    for (const auto& s : c.assessment.support) in = in || obs.count(s.observation);
    if (!in || c.predicate == "mentioned_in") continue;
    std::string subj = c.subject;
    for (const auto& e : all.entities) {
      if (e.id == c.subject) subj = e.kind + ":" + e.label;
    }
    std::string ob = c.object;
    for (const auto& e : all.entities) {
      if (e.id == c.object) ob = e.kind + ":" + e.label;
    }
    std::cerr << "CLAIM " << subj << " " << c.predicate << " " << ob << " " << c.value.dump() << " v=" << c.qualifiers.version
              << " b=" << c.qualifiers.branch << "\n";
  }
  for (const auto& e : all.entities) {
    for (const auto& o : e.attrs["observations"]) {
      if (obs.count(o.get<std::string>())) {
        std::cerr << "ENT " << e.kind << ":" << e.label << "\n";
        break;
      }
    }
  }
  for (const auto& f : all.forks) std::cerr << "FORK " << f.to_json().dump() << "\n";
  for (const auto& a : all.areas) {
    if (obs.count(a.observation)) std::cerr << "AREA " << a.to_json().dump() << "\n";
  }
}
